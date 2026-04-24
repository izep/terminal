// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "AIPaneContent.g.h"
#include "BasicPaneEvents.h"

namespace winrt::TerminalApp::implementation
{
    struct AIPaneContent : AIPaneContentT<AIPaneContent>, BasicPaneEvents
    {
    public:
        AIPaneContent();

        void Initialize(
            const winrt::Microsoft::Terminal::Settings::Model::CascadiaSettings& settings,
            const winrt::hstring& initialQuery,
            const winrt::hstring& cwd);

        // Observable property: false while an AI request is in flight.
        til::property<bool> IsIdle{ true };

        // INotifyPropertyChanged
        til::event<winrt::Windows::UI::Xaml::Data::PropertyChangedEventHandler> PropertyChanged;

#pragma region IPaneContent
        winrt::Windows::UI::Xaml::FrameworkElement GetRoot();
        void UpdateSettings(const winrt::Microsoft::Terminal::Settings::Model::CascadiaSettings& settings);
        winrt::Windows::Foundation::Size MinimumSize() { return { 1, 1 }; }
        void Focus(winrt::Windows::UI::Xaml::FocusState reason = winrt::Windows::UI::Xaml::FocusState::Programmatic);
        void Close();
        winrt::Microsoft::Terminal::Settings::Model::INewContentArgs GetNewTerminalArgs(BuildStartupKind kind) const;
        winrt::hstring Title() { return L"AI Chat"; }
        uint64_t TaskbarState() { return 0; }
        uint64_t TaskbarProgress() { return 0; }
        bool ReadOnly() { return false; }
        winrt::hstring Icon() const;
        Windows::Foundation::IReference<winrt::Windows::UI::Color> TabColor() const noexcept { return nullptr; }
        winrt::Windows::UI::Xaml::Media::Brush BackgroundBrush() { return Background(); }
#pragma endregion

    private:
        friend struct AIPaneContentT<AIPaneContent>; // for Xaml to bind events

        winrt::Microsoft::Terminal::Settings::Model::CascadiaSettings _settings{ nullptr };
        std::wstring _cwd;

        // Running conversation history sent to the API on every turn.
        std::vector<Microsoft::Terminal::AI::ChatMessage> _conversationHistory;

        void _sendClicked(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs& args);
        void _closeClicked(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs& args);
        void _inputKeyDown(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::Input::KeyRoutedEventArgs& args);

        winrt::fire_and_forget _submitQuery(std::wstring query);

        void _appendUserBubble(std::wstring_view text);
        winrt::Windows::UI::Xaml::Controls::TextBlock _appendAssistantBubble();
        void _appendTextToBlock(const winrt::Windows::UI::Xaml::Controls::TextBlock& block, std::wstring_view text);
        void _scrollToBottom();
    };
}

namespace winrt::TerminalApp::factory_implementation
{
    BASIC_FACTORY(AIPaneContent);
}
