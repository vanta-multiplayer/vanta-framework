#include "internal.h"

#include "core/application.h"
#include "core/client_config.h"
#include "core/nui/events.h"
#include "core/nui/nui.h"
#include "core/nui/page_events.h"
#include "core/voice_io.h"
#include "shared/version.h"

#include <logging/logger.h>
#include <utils/safe_win32.h>

namespace VantaMP::Nui::Internal {
    namespace {
        void PushStartup() {
            RegisterLaunchProtocol();
            gNetgraphOn = Core::GetClientConfig().netgraph;
            Emit(Event::kVersionUpdate, nlohmann::json::array({Version::rel, "develop", "", false, false}));
            EmitAudioDevices();
            Emit(Event::kSettingsUpdate, nlohmann::json::array({CurrentSettings()}));
            EmitCommandCatalog();
            Emit(Event::kServersRecent, nlohmann::json::array({nlohmann::json::array()}));
            Emit(Event::kServersFav, nlohmann::json::array({nlohmann::json::array()}));
            Emit(Event::kServersUpdate, nlohmann::json::array({nlohmann::json::array()}));
            FeedConsoleLogs();

            if (const auto invite = ParseLaunchInvite()) {
                Emit(Event::kConnectionInvite, nlohmann::json::array({*invite}));
            }

            Emit(Event::kVersionReady);
        }
    } // namespace

    void BindCoreHandlers() {
        On(Event::kLoaded, [](const std::string &) {
            PushStartup();
        });

        On(Event::kConnectionConnect, [](const std::string &payload) {
            const auto args = ParseArgs(payload);
            const auto host = args.size() > 0 && args[0].is_string() ? args[0].get<std::string>() : "";
            const auto port = args.size() > 1 && args[1].is_number() ? args[1].get<int>() : 22005;
            const auto name = args.size() > 6 && args[6].is_string() ? args[6].get<std::string>() : "Player";

            if (host.empty()) {
                return;
            }

            Emit(Event::kConnectionSetServer, nlohmann::json::array({host}));
            Core::RequestConnect(host + ":" + std::to_string(port), name.empty() ? "Player" : name);
        });

        On(Event::kConnectionAbort, [](const std::string &) {
            Core::RequestDisconnect();
        });

        On(Event::kConnectionDisconnect, [](const std::string &) {
            Core::RequestDisconnect();
        });

        On(Event::kConsoleExecute, [](const std::string &payload) {
            const auto args    = ParseArgs(payload);
            const auto command = !args.empty() && args[0].is_string() ? args[0].get<std::string>() : "";

            Framework::Logging::GetLogger("Console")->info("{}", command);
            ExecuteClientCommand(command);
        });

        On(Event::kConsoleSetState, [](const std::string &payload) {
            const auto args = ParseArgs(payload);

            if (!args.empty() && args[0].is_boolean()) {
                gConsoleOpen = args[0].get<bool>();
                ApplyViewFocus();
                Emit(Event::kConsoleOpen, nlohmann::json::array({gConsoleOpen}));
            }
        });

        On(Event::kServersReload, [](const std::string &) {
            Emit(Event::kServersUpdate, nlohmann::json::array({nlohmann::json::array()}));
        });

        On(Event::kSettingsChange, [](const std::string &payload) {
            const auto args  = ParseArgs(payload);
            const auto key   = !args.empty() && args[0].is_string() ? args[0].get<std::string>() : "";
            const auto value = args.size() > 1 ? args[1] : nlohmann::json();
            ApplySettingsChange(key, value);
        });

        On(Event::kSettingsDevicesReload, [](const std::string &) {
            EmitAudioDevices();
        });

        On(Event::kSettingsMicToggle, [](const std::string &payload) {
            const auto args    = ParseArgs(payload);
            const bool on      = !args.empty() && args[0].is_boolean() && args[0].get<bool>();
            const auto &config = Core::GetClientConfig();

            if (on) {
                ReportMicTest(Core::StartMicTest(config.voiceInputDevice, config.voiceOutputDevice));
            } else {
                Core::StopMicTest();
                ReportMicTest(false);
            }
        });

        On(Event::kSettingsTalkToggle, [](const std::string &payload) {
            const auto args = ParseArgs(payload);
            const bool on   = !args.empty() && args[0].is_boolean() && args[0].get<bool>();

            if (!on || !gInWorld) {
                Core::SetTalkLatched(false);
                return;
            }

            Core::SetTalkLatched(true);
            ReportMicTest(false);
            SetMenuOpen(false);
        });

        On(Event::kUiToggle, [](const std::string &payload) {
            const auto args = ParseArgs(payload);

            if (args.empty() || !args[0].is_boolean()) {
                return;
            }

            const bool want = args[0].get<bool>();

            // Offline, the menu is the only UI. Closing it leaves a black
            // screen and no keyboard, because CEF then drops every key.
            if (!want && !gInWorld) {
                SetMenuOpen(true);
                return;
            }

            gMenuOpen = want;
            ApplyViewFocus();
        });

        On(Event::kNetgraphEnable, [](const std::string &payload) {
            const auto args = ParseArgs(payload);
            gNetgraphOn     = args.empty() || !args[0].is_boolean() || args[0].get<bool>();
        });

        On(Event::kExit, [](const std::string &) {
            PostQuitMessage(0);
        });
    }
}
