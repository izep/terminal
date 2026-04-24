// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

namespace Microsoft::Terminal::AI
{
    // Prompt for AI error diagnosis: explain a failed command and suggest a fix.
    // The caller substitutes {command}, {exitCode}, and {output}.
    inline constexpr std::wstring_view ErrorDiagnosisSystemPrompt{
        LR"(You are a helpful terminal assistant. When given a shell command that failed, you:
1. Provide the corrected command on the FIRST line (and only the command, no explanation).
2. On the SECOND line, provide a brief one-sentence explanation of what went wrong.
Do not add any other text, markdown, or formatting.)"
    };

    inline std::wstring FormatErrorDiagnosisPrompt(
        std::wstring_view command,
        unsigned int exitCode,
        std::wstring_view output)
    {
        std::wstring result;
        result.reserve(512);
        result += L"The following shell command failed with exit code ";
        result += std::to_wstring(exitCode);
        result += L":\n\nCommand: ";
        result += command;
        if (!output.empty())
        {
            // Truncate output to keep costs low
            const auto truncated = output.substr(0, std::min(output.size(), static_cast<size_t>(2000)));
            result += L"\n\nOutput:\n";
            result += truncated;
        }
        result += L"\n\nPlease provide the corrected command on the first line, and a brief explanation on the second line.";
        return result;
    }

    // Prompt for next-command prediction: given recent history, suggest what to run next.
    inline constexpr std::wstring_view PredictNextCommandSystemPrompt{
        L"You are a helpful terminal assistant. Given the user's recent shell command history, current working directory, and shell, predict the single most useful next command. Reply with ONLY the command itself — no explanation, no markdown, no quotes."
    };

    inline std::wstring FormatPredictNextCommandPrompt(
        const std::vector<std::wstring>& recentCommands,
        std::wstring_view cwd,
        std::wstring_view shellName)
    {
        std::wstring result;
        result.reserve(512);
        result += L"Shell: ";
        result += shellName.empty() ? L"unknown" : shellName;
        result += L"\nCurrent directory: ";
        result += cwd.empty() ? L"unknown" : cwd;
        result += L"\nRecent commands (oldest to newest):\n";
        // Send at most the last 10 commands
        const auto start = recentCommands.size() > 10 ? recentCommands.size() - 10 : 0;
        for (auto i = start; i < recentCommands.size(); ++i)
        {
            result += L"  ";
            result += recentCommands[i];
            result += L"\n";
        }
        result += L"\nPredict the single most useful next command:";
        return result;
    }

    // Prompt for inline AI query: answer a one-shot question typed with the `?` prefix.
    inline constexpr std::wstring_view InlineQuerySystemPrompt{
        L"You are a helpful terminal assistant. Answer the user's question concisely. If the answer includes a shell command, put it on its own line prefixed with '$'. Keep responses short (under 200 words)."
    };

    inline std::wstring FormatInlineQueryPrompt(
        std::wstring_view question,
        std::wstring_view cwd,
        std::wstring_view shellName)
    {
        std::wstring result;
        result.reserve(256);
        if (!shellName.empty())
        {
            result += L"[Shell: ";
            result += shellName;
            result += L", CWD: ";
            result += cwd.empty() ? L"unknown" : cwd;
            result += L"]\n\n";
        }
        result += question;
        return result;
    }
}
