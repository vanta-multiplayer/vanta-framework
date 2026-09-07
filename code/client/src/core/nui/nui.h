#pragma once

#include <cstdint>
#include <string>

namespace VantaMP::Nui {
    void BringUpInterface(int width, int height);
    void ResizeInterface(int width, int height);
    void SetInterfaceVisible(bool visible);

    [[nodiscard]] bool IsInterfaceVisible();
    [[nodiscard]] bool IsMenuOpen();
    [[nodiscard]] bool IsInWorld();
    [[nodiscard]] bool IsCapturingInput();
    void ReleaseVoiceFocus();

    // Re-assert CEF focus while the menu or console owns the keyboard.
    // ProcessKeyboardEvent drops every key when the view is unfocused.
    void EnsureViewFocus();

    // Window-proc hotkeys. CEF drops keys once the view loses focus, so F1 / Esc
    // / F8 / ~ have to be handled here or they die the moment the menu closes.
    void ToggleMenu();
    void HandleEscape();
    void ToggleConsole();
    void TickNetgraph();
    void RefreshSettings();
    void TickMicTest();
    void ReportMicTest(bool running);

    void ReportConnected(const std::string &serverName);
    void ReportConnectionState(const std::string &state, const std::string &detail);

    // Handshake / resource download, for the connect screen.
    struct ConnectionProgress {
        std::string phase;
        bool downloading           = false;
        float progress             = 0.0f;
        std::uint32_t filesDownloaded = 0;
        std::uint32_t filesTotal      = 0;
        std::uint64_t bytesDownloaded = 0;
        std::uint64_t bytesTotal      = 0;
        std::string currentFile;
    };

    void ReportConnectionProgress(const ConnectionProgress &progress);
    void ShutdownInterface();
} // namespace VantaMP::Nui
