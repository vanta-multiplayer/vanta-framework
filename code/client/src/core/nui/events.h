#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace VantaMP::Nui {
    namespace Event {
        inline constexpr const char *kLoaded = "loaded";
        inline constexpr const char *kExit   = "exit";

        inline constexpr const char *kConnectionConnect      = "connection:connect";
        inline constexpr const char *kConnectionDisconnect  = "connection:disconnect";
        inline constexpr const char *kConnectionAbort        = "connection:abort";
        inline constexpr const char *kConnectionIdle         = "connection:idle";
        inline constexpr const char *kConnectionSetServer    = "connection:setServer";
        inline constexpr const char *kConnectionConnecting   = "connection:connecting";
        inline constexpr const char *kConnectionConnected    = "connection:connected";
        inline constexpr const char *kConnectionDisconnected = "connection:disconnected";
        inline constexpr const char *kConnectionFailed       = "connection:failed";
        inline constexpr const char *kConnectionProgress     = "connection:progress";

        inline constexpr const char *kConsoleExecute  = "console:execute";
        inline constexpr const char *kConsoleSetState = "console:setState";
        inline constexpr const char *kConsolePush     = "console:push";
        inline constexpr const char *kConsoleClear    = "console:clear";
        inline constexpr const char *kConsoleOpen     = "console:open";

        inline constexpr const char *kServersUpdate = "servers:update";
        inline constexpr const char *kServersReload = "servers:reload";
        inline constexpr const char *kServersRecent = "servers:recent:update";
        inline constexpr const char *kServersFav    = "servers:favorite:update";

        inline constexpr const char *kSettingsUpdate = "settings:update";
        inline constexpr const char *kSettingsChange = "settings:change";
        inline constexpr const char *kSettingsDevicesUpdate = "settings:devices:update";
        inline constexpr const char *kSettingsDevicesReload = "settings:devices:reload";
        inline constexpr const char *kSettingsMicLevel      = "settings:currentVolume:update";
        inline constexpr const char *kSettingsMicToggle     = "settings:currentVolume:toggle";
        inline constexpr const char *kSettingsTalkToggle    = "settings:talk:toggle";

        inline constexpr const char *kVersionUpdate = "version:update";
        inline constexpr const char *kVersionReady  = "version:ready";

        inline constexpr const char *kUiReady  = "ui:ready";
        inline constexpr const char *kUiToggle = "ui:toggle";

        inline constexpr const char *kHotkeyEscape = "hotkey:escape";

        inline constexpr const char *kNetgraphEnable = "netgraph:enable";
        inline constexpr const char *kNetgraphUpdate = "netgraph:update";
    } // namespace Event

    // Core -> page. Queued for GET /events.
    void Emit(const char *event, const nlohmann::json &detail = nullptr);
} // namespace VantaMP::Nui
