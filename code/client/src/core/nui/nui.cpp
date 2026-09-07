#include "nui.h"

#include "core/application.h"
#include "core/client_config.h"
#include "core/globals.h"
#include "core/voice_io.h"
#include "game/patches/audio.h"
#include "game/patches/display.h"
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
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <set>
#include <sstream>
#include <vector>

namespace VantaMP::Nui {
    namespace {
        int gViewId         = -1;
        bool gVisible       = false;
        bool gMenuOpen      = true;
        bool gConsoleOpen   = false;
        bool gInWorld       = false;
        bool gNetgraphOn    = false;
        bool gLogsAttached  = false;
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

        nlohmann::json VideoModes() {
            nlohmann::json modes = nlohmann::json::array();
            std::set<std::pair<unsigned, unsigned>> seen;

            DEVMODEW mode {};
            mode.dmSize = sizeof(mode);

            for (DWORD index = 0; EnumDisplaySettingsW(nullptr, index, &mode); ++index) {
                if (mode.dmBitsPerPel != 32 || mode.dmPelsWidth < 800 || mode.dmPelsHeight < 600) {
                    continue;
                }

                if (!seen.emplace(mode.dmPelsWidth, mode.dmPelsHeight).second) {
                    continue;
                }

                modes.push_back({
                    {"width", mode.dmPelsWidth},
                    {"height", mode.dmPelsHeight},
                    {"label", std::to_string(mode.dmPelsWidth) + "\xC3\x97" + std::to_string(mode.dmPelsHeight)},
                });
            }

            std::sort(modes.begin(), modes.end(), [](const nlohmann::json &left, const nlohmann::json &right) {
                const auto leftWidth  = left["width"].get<unsigned>();
                const auto rightWidth = right["width"].get<unsigned>();

                if (leftWidth != rightWidth) {
                    return leftWidth > rightWidth;
                }

                return left["height"].get<unsigned>() > right["height"].get<unsigned>();
            });

            return modes;
        }

        nlohmann::json EndpointsJson(bool capture) {
            nlohmann::json list = nlohmann::json::array();

            for (const auto &endpoint : capture ? Core::ListVoiceInputs() : Core::ListVoiceOutputs()) {
                list.push_back({
                    {"id", endpoint.id},
                    {"name", endpoint.name},
                    {"isDefault", endpoint.isDefault},
                });
            }

            return list;
        }

        void EmitAudioDevices() {
            nlohmann::json data;
            data["input"]  = EndpointsJson(true);
            data["output"] = EndpointsJson(false);
            Emit(Event::kSettingsDevicesUpdate, nlohmann::json::array({data}));
        }

        nlohmann::json CurrentSettings() {
            const auto &config = Core::GetClientConfig();

            nlohmann::json settings;
            settings["nickname"]     = config.nickname;
            settings["lastServer"]   = config.serverHost + ":" + std::to_string(config.serverPort);
            settings["windowWidth"]  = config.displayWidth;
            settings["windowHeight"] = config.displayHeight;
            settings["borderless"]   = config.displayBorderless;
            settings["exclusive"]    = config.displayExclusive;
            settings["fullscreen"]   = config.displayBorderless;
            settings["volumeMaster"] = config.volumeMaster;
            settings["volumeSfx"]    = config.volumeSfx;
            settings["volumeRadio"]  = config.volumeRadio;
            settings["voiceInputDevice"]  = config.voiceInputDevice;
            settings["voiceOutputDevice"] = config.voiceOutputDevice;
            settings["voiceVolume"]       = config.voiceVolume;
            settings["voicePttKey"]       = config.voicePttKey;
            settings["display"]      = 0;
            settings["modes"]        = VideoModes();
            settings["displays"]     = nlohmann::json::array();
            return settings;
        }

        const char *LevelName(int level) {
            switch (static_cast<spdlog::level::level_enum>(level)) {
            case spdlog::level::trace:
            case spdlog::level::debug: return "debug";
            case spdlog::level::info: return "info";
            case spdlog::level::warn: return "warn";
            case spdlog::level::err: return "error";
            case spdlog::level::critical: return "fatal";
            default: return "info";
            }
        }

        std::string ClockTime(std::time_t when = 0) {
            if (when == 0) {
                when = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            }

            std::tm local {};
            localtime_s(&local, &when);

            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", local.tm_hour, local.tm_min, local.tm_sec);
            return buffer;
        }

        void PushConsoleLine(const std::string &level, const std::string &category, const std::string &message, const std::string &timestamp) {
            nlohmann::json line = {
                {"level", level},
                {"category", category},
                {"message", message},
                {"timestamp", timestamp},
            };

            Emit(Event::kConsolePush, nlohmann::json::array({line}));
        }

        void PushLogFileTail(const std::string &path, const std::string &fallbackCategory) {
            std::ifstream file(path, std::ios::binary);

            if (!file) {
                return;
            }

            file.seekg(0, std::ios::end);
            const auto size = static_cast<std::streamoff>(file.tellg());

            if (size <= 0) {
                return;
            }

            constexpr std::streamoff kTailBytes = 96 * 1024;
            const auto start = size > kTailBytes ? size - kTailBytes : std::streamoff {0};
            file.seekg(start);

            std::string text(static_cast<std::size_t>(size - start), '\0');
            file.read(text.data(), static_cast<std::streamsize>(text.size()));

            std::istringstream stream(text);
            std::string raw;

            if (start > 0) {
                std::getline(stream, raw);
            }

            while (std::getline(stream, raw)) {
                if (!raw.empty() && raw.back() == '\r') {
                    raw.pop_back();
                }

                if (raw.empty()) {
                    continue;
                }

                // [2026-09-06 23:31:07.666] [Config] [info] Display: ...
                std::string time = ClockTime();
                std::string category = fallbackCategory;
                std::string level = "info";
                std::string message = raw;

                if (raw.size() > 2 && raw.front() == '[') {
                    const auto timeEnd = raw.find(']');

                    if (timeEnd != std::string::npos && timeEnd >= 20) {
                        time = raw.substr(12, 8);

                        const auto catOpen = raw.find('[', timeEnd + 1);
                        const auto catEnd  = raw.find(']', catOpen == std::string::npos ? timeEnd : catOpen);
                        const auto lvlOpen = raw.find('[', catEnd == std::string::npos ? timeEnd : catEnd + 1);
                        const auto lvlEnd  = raw.find(']', lvlOpen == std::string::npos ? timeEnd : lvlOpen);

                        if (catOpen != std::string::npos && catEnd != std::string::npos) {
                            category = raw.substr(catOpen + 1, catEnd - catOpen - 1);
                        }

                        if (lvlOpen != std::string::npos && lvlEnd != std::string::npos) {
                            level = raw.substr(lvlOpen + 1, lvlEnd - lvlOpen - 1);

                            if (level == "err" || level == "error") {
                                level = "error";
                            }
                            else if (level == "warning" || level == "warn") {
                                level = "warn";
                            }
                            else if (level == "critical" || level == "fatal") {
                                level = "fatal";
                            }
                            else if (level == "trace") {
                                level = "debug";
                            }

                            const auto textStart = raw.find_first_not_of(' ', lvlEnd + 1);
                            if (textStart != std::string::npos) {
                                message = raw.substr(textStart);
                            }
                        }
                    }
                }

                PushConsoleLine(level, category, message, time);
            }
        }

        void FeedConsoleLogs() {
            if (gLogsAttached) {
                return;
            }

            gLogsAttached = true;

            // Launcher wrote this before inject. The client log is VantaMP-<pid>.log.
            PushLogFileTail(Core::gProjectPath + "/logs/VantaMP.log", "Launcher");

            if (auto *logging = Framework::Logging::GetInstance()) {
                if (auto ring = logging->GetRingBuffer()) {
                    for (const auto &msg : ring->last_raw()) {
                        PushConsoleLine(LevelName(static_cast<int>(msg.level)), std::string(msg.logger_name.data(), msg.logger_name.size()),
                                        std::string(msg.payload.data(), msg.payload.size()), ClockTime(spdlog::log_clock::to_time_t(msg.time)));
                    }
                }

                logging->SetLogForwarder(
                    [](int level, const std::string &name, const std::string &message) {
                        PushConsoleLine(LevelName(level), name, message, ClockTime());
                    },
                    static_cast<int>(spdlog::level::debug));
            }
        }

        void PushStartup() {
            Emit(Event::kVersionUpdate, nlohmann::json::array({Version::rel, "develop", "", false, false}));
            EmitAudioDevices();
            Emit(Event::kSettingsUpdate, nlohmann::json::array({CurrentSettings()}));
            Emit(Event::kServersRecent, nlohmann::json::array({nlohmann::json::array()}));
            Emit(Event::kServersFav, nlohmann::json::array({nlohmann::json::array()}));
            Emit(Event::kServersUpdate, nlohmann::json::array({nlohmann::json::array()}));
            FeedConsoleLogs();
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

            if (open) {
                Core::SetTalkLatched(false);
            }

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

                const auto &config = Core::GetClientConfig();

                static unsigned width      = 0;
                static unsigned height     = 0;
                static bool     borderless = false;
                static bool     exclusive  = false;
                static float        master     = 1.0f;
                static float        sfx        = 1.0f;
                static float        radio      = 1.0f;
                static std::string  voiceIn    = "default";
                static std::string  voiceOut   = "default";
                static float        voiceVol   = 1.0f;
                static int          pttKey     = 0x56;
                static bool         primed     = false;

                if (!primed) {
                    width      = config.displayWidth;
                    height     = config.displayHeight;
                    borderless = config.displayBorderless;
                    exclusive  = config.displayExclusive;
                    master     = config.volumeMaster;
                    sfx        = config.volumeSfx;
                    radio      = config.volumeRadio;
                    voiceIn    = config.voiceInputDevice;
                    voiceOut   = config.voiceOutputDevice;
                    voiceVol   = config.voiceVolume;
                    pttKey     = config.voicePttKey;
                    primed     = true;
                }

                if (key == "windowWidth" && value.is_number()) {
                    width = value.get<unsigned>();
                }
                else if (key == "windowHeight" && value.is_number()) {
                    height = value.get<unsigned>();
                }
                else if ((key == "borderless" || key == "fullscreen") && value.is_boolean()) {
                    borderless = value.get<bool>();
                }
                else if (key == "exclusive" && value.is_boolean()) {
                    exclusive = value.get<bool>();
                }

                if (key == "volumeMaster" && value.is_number()) {
                    master = std::clamp(value.get<float>(), 0.0f, 1.0f);
                }
                else if (key == "volumeSfx" && value.is_number()) {
                    sfx = std::clamp(value.get<float>(), 0.0f, 1.0f);
                }
                else if (key == "volumeRadio" && value.is_number()) {
                    radio = std::clamp(value.get<float>(), 0.0f, 1.0f);
                }
                else if (key == "voiceInputDevice" && value.is_string()) {
                    voiceIn = value.get<std::string>();
                }
                else if (key == "voiceOutputDevice" && value.is_string()) {
                    voiceOut = value.get<std::string>();
                }
                else if (key == "voiceVolume" && value.is_number()) {
                    voiceVol = std::clamp(value.get<float>(), 0.0f, 2.0f);
                }
                else if (key == "voicePttKey" && value.is_number()) {
                    pttKey = value.get<int>();
                }

                if (key == "nickname" && value.is_string()) {
                    Core::SaveConnectSettings(config.serverHost, config.serverPort, value.get<std::string>());
                }

                // Saved only. SetVideoMode and a homemade Reset both crash
                // on this exe when the back-buffer size changes. The next
                // launch reads VantaMP.json and creates the device there.
                if (key == "windowWidth" || key == "windowHeight" || key == "borderless" || key == "fullscreen" || key == "exclusive") {
                    Core::SaveDisplaySettings(width, height, borderless, exclusive);
                }

                if (key == "volumeMaster" || key == "volumeSfx" || key == "volumeRadio") {
                    Core::SaveVolumeSettings(master, sfx, radio);
                    Game::SetVolumeSettings(master, sfx, radio);
                    Emit(Event::kSettingsUpdate, nlohmann::json::array({CurrentSettings()}));
                }

                if (key == "voiceInputDevice" || key == "voiceOutputDevice" || key == "voiceVolume" || key == "voicePttKey") {
                    Core::SaveVoiceSettings(voiceIn, voiceOut, voiceVol, pttKey);
                    Core::ApplyVoiceSettings();

                    if (Core::IsMicTestRunning()) {
                        Core::StartMicTest(Core::GetClientConfig().voiceInputDevice, Core::GetClientConfig().voiceOutputDevice);
                    }

                    Emit(Event::kSettingsUpdate, nlohmann::json::array({CurrentSettings()}));
                }
            });

            On(Event::kSettingsDevicesReload, [](const std::string &) {
                EmitAudioDevices();
            });

            On(Event::kSettingsMicToggle, [](const std::string &payload) {
                const auto args = ParseArgs(payload);
                const bool on   = !args.empty() && args[0].is_boolean() && args[0].get<bool>();
                const auto &config = Core::GetClientConfig();

                if (on) {
                    if (gInWorld) {
                        return;
                    }

                    Core::StartMicTest(config.voiceInputDevice, config.voiceOutputDevice);
                }
                else {
                    Core::StopMicTest();
                    Emit(Event::kSettingsMicLevel, nlohmann::json::array({0}));
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

        // 0x0 = fill the viewport and follow WM_SIZE. A fixed size here
        // is why the page stayed at the launch resolution after a live
        // apply: Manager::Resize only moves auto-resize views.
        gViewId = manager->CreateView(kEmbeddedUiUrl, 0, 0);

        if (gViewId < 0) {
            Framework::Logging::GetLogger("NUI")->error("The interface view could not be created");
            return;
        }

        auto *view = FindView();

        if (!view) {
            return;
        }

        view->SetAutoResize(true);
        view->Display(false);

        Framework::Logging::GetLogger("NUI")->info("Interface up at {}x{} ({})", width, height, kEmbeddedUiUrl);
    }

    void ResizeInterface(int width, int height) {
        if (width <= 0 || height <= 0) {
            return;
        }

        auto *manager = Framework::CoreModules::GetWebManager();

        if (!manager || !manager->IsInitialized()) {
            return;
        }

        manager->SetViewportConfiguration({width, height});

        if (auto *view = FindView()) {
            view->Resize(width, height);
        }
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

    void ReleaseVoiceFocus() {
        if (auto *target = FindView()) {
            target->Focus(false);
        }
    }

    void EnsureViewFocus() {
        ApplyViewFocus();
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
        static double        emitWait  = 0.0;
        static bool          clockReady = false;

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
            emitWait = 0.0;
            return;
        }

        // FPS is sampled every frame. The page only needs a reading
        // once a second - tx/rx are already last-second totals, and
        // pushing a sample every PostUpdate is what made the corner
        // flicker.
        emitWait += dt;

        if (emitWait < 1.0) {
            return;
        }

        emitWait = 0.0;

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

    void RefreshSettings() {
        Emit(Event::kSettingsUpdate, nlohmann::json::array({CurrentSettings()}));
    }

    void TickMicTest() {
        if (Core::IsMicTestRunning()) {
            Emit(Event::kSettingsMicLevel, nlohmann::json::array({static_cast<int>(Core::MicTestLevel() + 0.5f)}));
            return;
        }

        if (!Core::gApplication) {
            return;
        }

        const auto level = Core::gApplication->GetVoiceClient().GetLocalLevel();
        Emit(Event::kSettingsMicLevel, nlohmann::json::array({static_cast<int>(level * 100.0f + 0.5f)}));
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
            Core::SetTalkLatched(false);
            ApplyViewFocus();
            Emit(Event::kConnectionDisconnected, nlohmann::json::array({detail.empty() ? "Disconnected" : detail}));
            Emit(Event::kUiToggle, nlohmann::json::array({true}));
            return;
        }

        if (state == "connected") {
            return;
        }
    }

    void ReportConnectionProgress(const ConnectionProgress &progress) {
        nlohmann::json payload = {
            {"phase", progress.phase},
            {"downloading", progress.downloading},
            {"progress", progress.progress},
            {"filesDownloaded", progress.filesDownloaded},
            {"filesTotal", progress.filesTotal},
            {"bytesDownloaded", progress.bytesDownloaded},
            {"bytesTotal", progress.bytesTotal},
            {"currentFile", progress.currentFile},
        };

        Emit(Event::kConnectionProgress, nlohmann::json::array({payload}));
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
