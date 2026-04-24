// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "AIPaneContent.h"
#include "AIPaneContent.g.cpp"

#include "../../cascadia/AIService/AIPrompts.h"
#include "../../cascadia/AIService/CopilotClient.h"

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::UI;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml::Input;
using namespace winrt::Windows::UI::Xaml::Media;

namespace winrt::TerminalApp::implementation
{
    AIPaneContent::AIPaneContent()
    {
        InitializeComponent();
    }

    void AIPaneContent::Initialize(
        const winrt::Microsoft::Terminal::Settings::Model::CascadiaSettings& settings,
        const winrt::hstring& initialQuery,
        const winrt::hstring& cwd)
    {
        _settings = settings;
        _cwd = std::wstring{ cwd };

        // Seed the system prompt once at the start of the conversation.
        _conversationHistory.emplace_back(
            Microsoft::Terminal::AI::ChatMessage{ L"system", std::wstring{ Microsoft::Terminal::AI::InlineQuerySystemPrompt } });

        if (!initialQuery.empty())
        {
            // Auto-submit the initial query if one was provided (from `?? <question>`).
            _submitQuery(std::wstring{ initialQuery });
        }
        else
        {
            InputBox().Focus(FocusState::Programmatic);
        }
    }

    // IPaneContent
    FrameworkElement AIPaneContent::GetRoot()
    {
        return *this;
    }

    void AIPaneContent::UpdateSettings(const winrt::Microsoft::Terminal::Settings::Model::CascadiaSettings& settings)
    {
        _settings = settings;
    }

    void AIPaneContent::Focus(FocusState reason)
    {
        InputBox().Focus(reason);
    }

    void AIPaneContent::Close()
    {
        CloseRequested.raise(*this, nullptr);
    }

    winrt::Microsoft::Terminal::Settings::Model::INewContentArgs AIPaneContent::GetNewTerminalArgs(BuildStartupKind /*kind*/) const
    {
        return BaseContentArgs(L"x-ai-chat");
    }

    hstring AIPaneContent::Icon() const
    {
        // Sparkle / AI icon in Segoe Fluent Icons
        return L"\xe946";
    }

    // Send button click handler.
    void AIPaneContent::_sendClicked(const IInspectable& /*sender*/, const RoutedEventArgs& /*args*/)
    {
        const auto text = std::wstring{ InputBox().Text() };
        if (text.empty())
        {
            return;
        }
        InputBox().Text(L"");
        _submitQuery(text);
    }

    // Close button click handler.
    void AIPaneContent::_closeClicked(const IInspectable& /*sender*/, const RoutedEventArgs& /*args*/)
    {
        Close();
    }

    // Key handler: Enter submits, Shift+Enter inserts a newline, Escape closes.
    void AIPaneContent::_inputKeyDown(const IInspectable& /*sender*/, const KeyRoutedEventArgs& args)
    {
        const auto key = args.Key();
        const auto modifiers = args.KeyStatus();

        if (key == winrt::Windows::System::VirtualKey::Escape)
        {
            Close();
            args.Handled(true);
        }
        else if (key == winrt::Windows::System::VirtualKey::Enter && !modifiers.IsMenuKeyDown)
        {
            // Shift+Enter: let the TextBox handle it (inserts newline).
            // Plain Enter: submit.
            const auto shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (!shiftPressed)
            {
                const auto text = std::wstring{ InputBox().Text() };
                if (!text.empty())
                {
                    InputBox().Text(L"");
                    _submitQuery(text);
                }
                args.Handled(true);
            }
        }
    }

    // Append a user message bubble to the conversation view.
    void AIPaneContent::_appendUserBubble(std::wstring_view text)
    {
        Border bubble;
        bubble.Margin(ThicknessHelper::FromLengths(32, 0, 0, 0)); // indent from left
        bubble.Padding(ThicknessHelper::FromUniformLength(8));
        bubble.CornerRadius({ 8, 8, 2, 8 });
        bubble.HorizontalAlignment(HorizontalAlignment::Right);
        // Use the theme-aware UserBubbleBackground resource defined in AIPaneContent.xaml.
        bubble.Background(unbox_value<Brush>(Resources().Lookup(box_value(L"UserBubbleBackground"))));

        TextBlock tb;
        tb.Text(hstring{ text });
        tb.TextWrapping(TextWrapping::Wrap);
        tb.FontSize(13);
        tb.Foreground(SolidColorBrush{ Colors::White() });
        tb.IsTextSelectionEnabled(true);
        bubble.Child(tb);

        MessageList().Children().Append(bubble);
        _scrollToBottom();
    }

    // Append an empty assistant bubble and return the TextBlock so it can be updated live during streaming.
    TextBlock AIPaneContent::_appendAssistantBubble()
    {
        Border bubble;
        bubble.Margin(ThicknessHelper::FromLengths(0, 0, 32, 0));
        bubble.Padding(ThicknessHelper::FromUniformLength(8));
        bubble.CornerRadius({ 2, 8, 8, 8 });
        bubble.HorizontalAlignment(HorizontalAlignment::Left);
        // Use the theme-aware AssistantBubbleBackground resource defined in AIPaneContent.xaml.
        bubble.Background(unbox_value<Brush>(Resources().Lookup(box_value(L"AssistantBubbleBackground"))));

        TextBlock tb;
        tb.TextWrapping(TextWrapping::Wrap);
        tb.FontSize(13);
        tb.IsTextSelectionEnabled(true);
        bubble.Child(tb);

        MessageList().Children().Append(bubble);
        _scrollToBottom();
        return tb;
    }

    void AIPaneContent::_appendTextToBlock(const TextBlock& block, std::wstring_view text)
    {
        block.Text(block.Text() + hstring{ text });
        _scrollToBottom();
    }

    void AIPaneContent::_scrollToBottom()
    {
        MessageScrollViewer().UpdateLayout();
        MessageScrollViewer().ChangeView(nullptr, MessageScrollViewer().ScrollableHeight(), nullptr);
    }

    // Submit a user message: add it to history, show bubbles, stream the response.
    winrt::fire_and_forget AIPaneContent::_submitQuery(std::wstring query)
    {
        auto weakThis = get_weak();

        // Show user bubble and mark busy on UI thread.
        _appendUserBubble(query);
        IsIdle(false);
        PropertyChanged.raise(*this, Windows::UI::Xaml::Data::PropertyChangedEventArgs{ L"IsIdle" });

        // Add the user turn to conversation history.
        _conversationHistory.emplace_back(Microsoft::Terminal::AI::ChatMessage{ L"user", query });

        // Add an assistant bubble to fill in during streaming.
        const auto assistantBlock = _appendAssistantBubble();

        // Grab dispatcher before switching to background.
        const auto dispatcher = Dispatcher();

        co_await winrt::resume_background();

        // Build client with token from settings or env var.
        Microsoft::Terminal::AI::CopilotClient client{
            _settings ? std::wstring{ _settings.GlobalSettings().AIToken() } : std::wstring{}
        };

        const auto model = _settings ? std::wstring{ _settings.GlobalSettings().AIModel() } : std::wstring{ L"gpt-4o" };

        if (!client.HasToken())
        {
            co_await wil::resume_foreground(dispatcher);
            if (auto strong = weakThis.get())
            {
                strong->_appendTextToBlock(assistantBlock, L"[AI token not configured. Set experimental.aiToken in settings.]");
                strong->IsIdle(true);
                strong->PropertyChanged.raise(*strong, Windows::UI::Xaml::Data::PropertyChangedEventArgs{ L"IsIdle" });
            }
            co_return;
        }

        // Keep a snapshot of history for this request (don't hold the vector across await points).
        const auto historyCopy = _conversationHistory;

        // Stream the response.
        std::wstring fullResponse;
        co_await client.StreamChat(
            historyCopy,
            model,
            [weakThis, dispatcher, assistantBlock](std::wstring_view chunk) {
                [](auto weakThis, auto dispatcher, auto assistantBlock, std::wstring chunk) -> winrt::fire_and_forget {
                    co_await wil::resume_foreground(dispatcher);
                    if (auto strong = weakThis.get())
                    {
                        strong->_appendTextToBlock(assistantBlock, chunk);
                    }
                }(weakThis, dispatcher, assistantBlock, std::wstring{ chunk });
            },
            nullptr);

        // Add the full assistant response to history for future turns.
        const auto finalText = std::wstring{ assistantBlock.Text() };
        _conversationHistory.emplace_back(Microsoft::Terminal::AI::ChatMessage{ L"assistant", finalText });

        co_await wil::resume_foreground(dispatcher);
        if (auto strong = weakThis.get())
        {
            strong->IsIdle(true);
            strong->PropertyChanged.raise(*strong, Windows::UI::Xaml::Data::PropertyChangedEventArgs{ L"IsIdle" });
            strong->InputBox().Focus(FocusState::Programmatic);
        }
    }
}
