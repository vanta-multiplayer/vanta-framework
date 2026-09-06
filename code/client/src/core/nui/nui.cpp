#include "nui.h"

#include "core/application.h"
#include "core/client_config.h"
#include "core/nui/embedded_ui.h"
#include "core/nui/events.h"
#include "core/nui/page_events.h"
#include "shared/version.h"

#include <utils/safe_win32.h>

#include <core_modules.h>
#include <gui/manager.h>
#include <gui/view.h>
#include <logging/logger.h>

#include <integrations/client/networking/engine.h>
#include <mafianet/peerinterface.h>
#include <mafianet/statistics.h>
#include <nlohmann/json.hpp>

namespace VantaMP::Nui {
    namespace {
        int gViewId         = -1;
        bool gVisible       = false;
        bool gMenuOpen      = true;
        bool gConsoleOpen   = false;
        bool gInWorld       = false;
        bool gNetgraphOn    = false;
        float gFps          = 0.0f;

        Framework::GUI::View *FindView() {
            auto *manager = Framework::CoreModules::GetWebManager();

            if (!manager || gViewId < 0) {
                return nullptr;
            }

            for (auto *view : manager->GetAllViews()) {
                if (view && view->GetId() == gViewId) {
                    return view;
                }
            }

            return nullptr;
        }

        nlohmann::json ParseArgs(const std::string &payload) {
            if (payload.empty() || payload == "null") {
                return nlohmann::json::array();
            }

            try {
                auto parsed = nlohmann::json::parse(payload);

                if (parsed.is_array()) {
                    return parsed;
                }

                if (parsed.is_null()) {
                    return nlohmann::json::array();
                }

                return nlohmann::json::array({parsed});
            }
            catch (const std::exception &) {
                return nlohmann::json::array();
            }
        }

        nlohmann::json CurrentSettings() {
            const auto &config = Core::GetClientConfig();

            nlohmann::json settings;
            settings["nickname"]     = config.nickname;
            settings["lastServer"]   = config.serverHost + ":" + std::to_string(config.serverPort);
            settings["windowWidth"]  = config.displayWidth;
            settings["windowHeight"] = config.displayHeight;
            settings["fullscreen"]   = config.displayBorderless;
            settings["display"]      = 0;
            settings["modes"]        = nlohmann::json::array();
            settings["displays"]     = nlohmann::json::array();
            return settings;
        }

        void PushStartup() {
            Emit(Event::kVersionUpdate, nlohmann::json::array({Version::rel, "develop", "", false, false}));
            Emit(Event::kSettingsUpdate, nlohmann::json::array({CurrentSettings()}));
            Emit(Event::kServersRecent, nlohmann::json::array({nlohmann::json::array()}));
            Emit(Event::kServersFav, nlohmann::json::array({nlohmann::json::array()}));
            Emit(Event::kServersUpdate, nlohmann::json::array({nlohmann::json::array()}));
            Emit(Event::kVersionReady);
        }

        void ApplyViewFocus() {
            if (auto *target = FindView()) {
                target->Focus(gVisible && (gMenuOpen || gConsoleOpen));
            }
        }

        void SetMenuOpen(bool open) {
            if (gMenuOpen == open) {
                return;
            }

            gMenuOpen = open;
            ApplyViewFocus();
            Emit(Event::kUiToggle, nlohmann::json::array({open}));
        }

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
                Emit(Event::kConsolePush, nlohmann::json::array({14, command}));
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

                static unsigned width = 0, height = 0;
                static bool fullscreen = false;

                if (key == "windowWidth" && value.is_number()) {
                    width = value.get<unsigned>();
                }
                else if (key == "windowHeight" && value.is_number()) {
                    height = value.get<unsigned>();
                }
                else if (key == "fullscreen" && value.is_boolean()) {
                    fullscreen = value.get<bool>();
                }

                if (key == "nickname" && value.is_string()) {
                    const auto &config = Core::GetClientConfig();
                    Core::SaveConnectSettings(config.serverHost, config.serverPort, value.get<std::string>());
                }

                if (key == "windowWidth" || key == "windowHeight" || key == "fullscreen") {
                    const auto &config = Core::GetClientConfig();
                    Core::SaveDisplaySettings(width ? width : config.displayWidth, height ? height : config.displayHeight, fullscreen);
                }
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
    } // namespace

    void Emit(const char *event, const nlohmann::json &detail) {
        const auto args = detail.is_array() ? detail : detail.is_null() ? nlohmann::json::array() : nlohmann::json::array({detail});
        PushToPage(event, args.dump());
    }

    void BringUpInterface(int width, int height) {
        if (gViewId >= 0) {
            return;
        }

        auto *manager = Framework::CoreModules::GetWebManager();

        if (!manager || !manager->IsInitialized()) {
            Framework::Logging::GetLogger("NUI")->error("The web manager is not up; there will be no interface");
            return;
        }

        RegisterEmbeddedInterface(*manager);
        BindCoreHandlers();

        gViewId = manager->CreateView(kEmbeddedUiUrl, width, height);

        if (gViewId < 0) {
            Framework::Logging::GetLogger("NUI")->error("The interface view could not be created");
            return;
        }

        auto *view = FindView();

        if (!view) {
            return;
        }

        view->Display(false);

        Framework::Logging::GetLogger("NUI")->info("Interface up at {}x{} ({})", width, height, kEmbeddedUiUrl);
    }

    void SetInterfaceVisible(bool visible) {
        gVisible = visible;

        if (auto *view = FindView()) {
            view->Display(visible);
            view->Focus(visible && (gMenuOpen || gConsoleOpen));
        }

        if (visible) {
            Emit(Event::kUiReady);
        }
    }

    bool IsInterfaceVisible() {
        return gVisible;
    }

    bool IsMenuOpen() {
        return gVisible && gMenuOpen;
    }

    bool IsInWorld() {
        return gInWorld;
    }

    bool IsCapturingInput() {
        return gVisible && (gMenuOpen || gConsoleOpen);
    }

    void ToggleMenu() {
        if (!gVisible) {
            return;
        }

        if (!gInWorld) {
            SetMenuOpen(true);
            return;
        }

        SetMenuOpen(!gMenuOpen);
    }

    void HandleEscape() {
        if (!gVisible) {
            return;
        }

        if (gConsoleOpen) {
            gConsoleOpen = false;
            ApplyViewFocus();
            Emit(Event::kConsoleOpen, nlohmann::json::array({false}));
            return;
        }

        if (!gInWorld) {
            SetMenuOpen(true);
            Emit(Event::kHotkeyEscape);
            return;
        }

        if (!gMenuOpen) {
            SetMenuOpen(true);
            return;
        }

        Emit(Event::kHotkeyEscape);
    }

    void ToggleConsole() {
        if (!gVisible) {
            return;
        }

        gConsoleOpen = !gConsoleOpen;
        ApplyViewFocus();
        Emit(Event::kConsoleOpen, nlohmann::json::array({gConsoleOpen}));
    }

    void TickNetgraph() {
        static LARGE_INTEGER frequency {};
        static LARGE_INTEGER last {};
        static bool clockReady = false;

        if (!clockReady) {
            QueryPerformanceFrequency(&frequency);
            QueryPerformanceCounter(&last);
            clockReady = true;
        }

        LARGE_INTEGER now {};
        QueryPerformanceCounter(&now);

        const double dt = frequency.QuadPart > 0 ? static_cast<double>(now.QuadPart - last.QuadPart) / static_cast<double>(frequency.QuadPart) : 0.0;
        last = now;

        if (dt > 0.0 && dt < 1.0) {
            const auto instant = static_cast<float>(1.0 / dt);
            gFps += (instant - gFps) * 0.35f;
        }

        if (!gNetgraphOn || !gVisible) {
            return;
        }

        int rtt     = 0;
        double tx   = 0.0;
        double rx   = 0.0;

        if (Core::gApplication) {
            if (auto *engine = Core::gApplication->GetNetworkingEngine()) {
                if (auto *client = engine->GetNetworkClient()) {
                    rtt = client->GetPing();

                    if (auto *peer = client->GetPeer()) {
                        if (auto *stats = peer->GetStatistics(peer->GetSystemAddressFromIndex(0))) {
                            tx = static_cast<double>(stats->valueOverLastSecond[MafiaNet::ACTUAL_BYTES_SENT]);
                            rx = static_cast<double>(stats->valueOverLastSecond[MafiaNet::ACTUAL_BYTES_RECEIVED]);
                        }
                    }
                }
            }
        }

        nlohmann::json sample;
        sample["fps"]       = static_cast<int>(gFps + 0.5f);
        sample["rtt"]       = rtt;
        sample["tx"]        = tx;
        sample["rx"]        = rx;
        sample["connected"] = gInWorld;
        Emit(Event::kNetgraphUpdate, nlohmann::json::array({sample}));
    }

    void ReportConnected(const std::string &serverName) {
        gInWorld  = true;
        gMenuOpen = false;
        ApplyViewFocus();

        Emit(Event::kConnectionSetServer, nlohmann::json::array({serverName}));
        Emit(Event::kConnectionConnected, nlohmann::json::array({serverName}));
        Emit(Event::kUiToggle, nlohmann::json::array({false}));
    }

    void ReportConnectionState(const std::string &state, const std::string &detail) {
        if (state == "connecting") {
            Emit(Event::kConnectionConnecting, detail.empty() ? nlohmann::json::array() : nlohmann::json::array({detail}));
            return;
        }

        if (state == "failed") {
            gInWorld = false;
            SetMenuOpen(true);
            Emit(Event::kConnectionFailed, nlohmann::json::array({detail.empty() ? "Connection failed" : detail, true}));
            return;
        }

        if (state == "disconnected") {
            gInWorld  = false;
            gMenuOpen = true;
            ApplyViewFocus();
            Emit(Event::kConnectionDisconnected, nlohmann::json::array({detail.empty() ? "Disconnected" : detail}));
            Emit(Event::kUiToggle, nlohmann::json::array({true}));
            return;
        }

        if (state == "connected") {
            return;
        }
    }

    void ShutdownInterface() {
        if (gViewId < 0) {
            return;
        }

        if (auto *manager = Framework::CoreModules::GetWebManager()) {
            manager->DestroyView(gViewId);
        }

        ClearHandlers();
        gViewId = -1;
    }
} // namespace VantaMP::Nui
