#pragma once

// Shared state and helpers for the NUI modules. nui.cpp is the public
// surface; settings, console, launch, netgraph, and page handlers live
// in their own translation units.

#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace Framework::GUI {
    class View;
}

namespace VantaMP::Nui::Internal {
    extern int gViewId;
    extern bool gVisible;
    extern bool gMenuOpen;
    extern bool gConsoleOpen;
    extern bool gInWorld;
    extern bool gNetgraphOn;
    extern bool gLogsAttached;
    extern bool gReconnectPending;
    extern float gFps;
    extern std::string gReconnectTo;

    Framework::GUI::View *FindView();
    nlohmann::json ParseArgs(const std::string &payload);
    void ApplyViewFocus();
    void SetMenuOpen(bool open);

    nlohmann::json CurrentSettings();
    void EmitAudioDevices();
    void ApplySettingsChange(const std::string &key, const nlohmann::json &value);

    void PushConsoleLine(const std::string &level, const std::string &category, const std::string &message, const std::string &timestamp);
    void FeedConsoleLogs();
    void EmitCommandCatalog();
    void ExecuteClientCommand(const std::string &command);

    void SetNetgraphEnabled(bool enabled, bool announce);

    void RegisterLaunchProtocol();
    std::optional<nlohmann::json> ParseLaunchInvite();

    void BindCoreHandlers();
}
