// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

// CopilotClient.h
// A minimal HTTP client for the GitHub Copilot Chat API.
// Uses Windows::Web::Http::HttpClient (WinRT) — no unofficial SDKs.
//
// API endpoint: https://api.githubcopilot.com/chat/completions
// Protocol: OpenAI-compatible chat completions (POST JSON).
//
// Token: read from the `experimental.aiToken` setting or the GITHUB_TOKEN env var.

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Storage.Streams.h>

namespace Microsoft::Terminal::AI
{
    // A message in a chat conversation.
    struct ChatMessage
    {
        std::wstring role;   // "system", "user", or "assistant"
        std::wstring content;
    };

    // Simple async Copilot chat client.
    // All methods are coroutines and must be awaited from a background thread.
    class CopilotClient
    {
    public:
        // Construct with a token (may be empty, in which case we try GITHUB_TOKEN env var).
        explicit CopilotClient(std::wstring token = {}) :
            _token(std::move(token))
        {
            if (_token.empty())
            {
                // Fall back to GITHUB_TOKEN environment variable.
                wchar_t buf[2048]{};
                if (GetEnvironmentVariableW(L"GITHUB_TOKEN", buf, static_cast<DWORD>(std::size(buf))) > 0)
                {
                    _token = buf;
                }
            }
        }

        bool HasToken() const noexcept { return !_token.empty(); }

        // Single-shot chat completion.
        // Returns the assistant's reply, or an empty string on error.
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> RequestChat(
            std::vector<ChatMessage> messages,
            std::wstring_view model = L"gpt-4o")
        {
            if (_token.empty())
            {
                co_return winrt::hstring{};
            }

            try
            {
                const auto requestBody = _BuildRequestBody(messages, model, /*stream=*/false);
                const auto response = co_await _PostJson(requestBody);
                co_return _ExtractContent(response);
            }
            catch (...)
            {
                co_return winrt::hstring{};
            }
        }

        // Streaming chat completion.
        // Calls `onChunk` for each text delta received, then calls `onDone`.
        // Both callbacks are called from whatever thread the coroutine resumes on.
        winrt::Windows::Foundation::IAsyncAction StreamChat(
            std::vector<ChatMessage> messages,
            std::wstring_view model,
            std::function<void(std::wstring_view)> onChunk,
            std::function<void()> onDone)
        {
            if (_token.empty())
            {
                if (onDone)
                {
                    onDone();
                }
                co_return;
            }

            try
            {
                const auto requestBody = _BuildRequestBody(messages, model, /*stream=*/true);
                co_await _StreamPost(requestBody, onChunk);
            }
            catch (...)
            {
                // Swallow; partial output may have already been delivered.
            }

            if (onDone)
            {
                onDone();
            }
        }

    private:
        std::wstring _token;

        static constexpr std::wstring_view ApiUrl{ L"https://api.githubcopilot.com/chat/completions" };
        static constexpr std::wstring_view UserAgent{ L"WindowsTerminal/1.0" };

        // Build the JSON body for a /chat/completions request.
        winrt::hstring _BuildRequestBody(
            const std::vector<ChatMessage>& messages,
            std::wstring_view model,
            bool stream) const
        {
            // Hand-roll JSON to avoid adding a JSON library dependency.
            // The payload is small and well-structured.
            std::wstring body;
            body.reserve(1024);
            body += L"{\"model\":\"";
            body += _JsonEscape(model);
            body += L"\",\"stream\":";
            body += stream ? L"true" : L"false";
            body += L",\"messages\":[";
            for (size_t i = 0; i < messages.size(); ++i)
            {
                if (i > 0)
                {
                    body += L",";
                }
                body += L"{\"role\":\"";
                body += _JsonEscape(messages[i].role);
                body += L"\",\"content\":\"";
                body += _JsonEscape(messages[i].content);
                body += L"\"}";
            }
            body += L"]}";
            return winrt::hstring{ body };
        }

        // Escape a string for embedding inside a JSON string value.
        static std::wstring _JsonEscape(std::wstring_view s)
        {
            std::wstring result;
            result.reserve(s.size() + 16);
            for (const auto ch : s)
            {
                switch (ch)
                {
                case L'"':
                    result += LR"(\")";
                    break;
                case L'\\':
                    result += LR"(\\)";
                    break;
                case L'\n':
                    result += LR"(\n)";
                    break;
                case L'\r':
                    result += LR"(\r)";
                    break;
                case L'\t':
                    result += LR"(\t)";
                    break;
                default:
                    result += ch;
                    break;
                }
            }
            return result;
        }

        // POST JSON to the API and return the raw response body as a string.
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> _PostJson(const winrt::hstring& body) const
        {
            using namespace winrt::Windows::Web::Http;
            using namespace winrt::Windows::Web::Http::Headers;

            HttpClient client;
            client.DefaultRequestHeaders().UserAgent().ParseAdd(UserAgent.data());
            client.DefaultRequestHeaders().Authorization(
                HttpCredentialsHeaderValue{ L"Bearer", winrt::hstring{ _token } });
            client.DefaultRequestHeaders().Append(L"Copilot-Integration-Id", L"windows-terminal");

            const auto content = HttpStringContent{
                body,
                winrt::Windows::Storage::Streams::UnicodeEncoding::Utf8,
                L"application/json"
            };

            const auto response = co_await client.PostAsync(
                winrt::Windows::Foundation::Uri{ ApiUrl.data() },
                content);

            response.EnsureSuccessStatusCode();
            co_return co_await response.Content().ReadAsStringAsync();
        }

        // POST JSON with streaming, calling onChunk for each content delta.
        winrt::Windows::Foundation::IAsyncAction _StreamPost(
            const winrt::hstring& body,
            const std::function<void(std::wstring_view)>& onChunk) const
        {
            using namespace winrt::Windows::Web::Http;
            using namespace winrt::Windows::Web::Http::Headers;
            using namespace winrt::Windows::Storage::Streams;

            HttpClient client;
            client.DefaultRequestHeaders().UserAgent().ParseAdd(UserAgent.data());
            client.DefaultRequestHeaders().Authorization(
                HttpCredentialsHeaderValue{ L"Bearer", winrt::hstring{ _token } });
            client.DefaultRequestHeaders().Append(L"Copilot-Integration-Id", L"windows-terminal");

            const auto content = HttpStringContent{
                body,
                UnicodeEncoding::Utf8,
                L"application/json"
            };

            const auto response = co_await client.PostAsync(
                winrt::Windows::Foundation::Uri{ ApiUrl.data() },
                content);

            response.EnsureSuccessStatusCode();

            // Read the response stream line by line (SSE format: "data: {...}\n\n")
            const auto stream = co_await response.Content().ReadAsInputStreamAsync();
            DataReader reader{ stream };
            reader.InputStreamOptions(InputStreamOptions::Partial);
            std::wstring lineBuffer;

            while (true)
            {
                const auto bytesRead = co_await reader.LoadAsync(4096);
                if (bytesRead == 0)
                {
                    break;
                }

                // Read the available bytes as UTF-8 and decode.
                const auto bytes = reader.ReadBuffer(bytesRead);
                const auto dataReader = DataReader::FromBuffer(bytes);
                dataReader.UnicodeEncoding(UnicodeEncoding::Utf8);
                const auto text = dataReader.ReadString(dataReader.UnconsumedBufferLength());

                lineBuffer += std::wstring_view{ text };

                // Process complete lines from the buffer.
                size_t pos = 0;
                while (true)
                {
                    const auto newline = lineBuffer.find(L'\n', pos);
                    if (newline == std::wstring::npos)
                    {
                        // Keep the partial line for the next iteration.
                        lineBuffer = lineBuffer.substr(pos);
                        break;
                    }

                    const auto line = lineBuffer.substr(pos, newline - pos);
                    pos = newline + 1;

                    // SSE format: "data: <json>" or "data: [DONE]"
                    if (line.starts_with(L"data: "))
                    {
                        const auto jsonPart = std::wstring_view{ line }.substr(6);
                        if (jsonPart == L"[DONE]")
                        {
                            break;
                        }
                        const auto delta = _ExtractStreamDelta(winrt::hstring{ jsonPart });
                        if (!delta.empty() && onChunk)
                        {
                            onChunk(delta);
                        }
                    }
                }
            }
        }

        // Extract the assistant's content from a non-streaming response JSON.
        // Parses: {"choices":[{"message":{"content":"..."}}]}
        static winrt::hstring _ExtractContent(const winrt::hstring& responseJson)
        {
            try
            {
                using namespace winrt::Windows::Data::Json;
                const auto obj = JsonObject::Parse(responseJson);
                const auto choices = obj.GetNamedArray(L"choices");
                if (choices.Size() == 0)
                {
                    return {};
                }
                const auto message = choices.GetObjectAt(0).GetNamedObject(L"message");
                return winrt::hstring{ message.GetNamedString(L"content") };
            }
            catch (...)
            {
                return {};
            }
        }

        // Extract content delta from a streaming SSE chunk.
        // Parses: {"choices":[{"delta":{"content":"..."}}]}
        static std::wstring _ExtractStreamDelta(const winrt::hstring& chunkJson)
        {
            try
            {
                using namespace winrt::Windows::Data::Json;
                const auto obj = JsonObject::Parse(chunkJson);
                const auto choices = obj.GetNamedArray(L"choices");
                if (choices.Size() == 0)
                {
                    return {};
                }
                const auto delta = choices.GetObjectAt(0).GetNamedObject(L"delta");
                if (delta.HasKey(L"content"))
                {
                    return std::wstring{ delta.GetNamedString(L"content") };
                }
            }
            catch (...)
            {
            }
            return {};
        }
    };
}
