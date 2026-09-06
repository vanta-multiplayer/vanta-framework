#pragma once

#include <string>

namespace VantaMP::Nui {
    void BringUpInterface(int width, int height);
    void SetInterfaceVisible(bool visible);

    [[nodiscard]] bool IsInterfaceVisible();
    [[nodiscard]] bool IsMenuOpen();
    [[nodiscard]] bool IsInWorld();
    [[nodiscard]] bool IsCapturingInput();

    // Window-proc hotkeys. CEF drops keys once the view loses focus, so F1 / Esc
    // / F8 / ~ have to be handled here or they die the moment the menu closes.
    void ToggleMenu();
    void HandleEscape();
    void ToggleConsole();
    void TickNetgraph();

    void ReportConnected(const std::string &serverName);
    void ReportConnectionState(const std::string &state, const std::string &detail);
    void ShutdownInterface();
} // namespace VantaMP::Nui
