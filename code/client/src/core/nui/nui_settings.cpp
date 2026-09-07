#include "internal.h"

#include "core/client_config.h"
#include "core/nui/events.h"
#include "core/nui/nui.h"
#include "core/voice_io.h"
#include "game/patches/audio.h"

#include <utils/safe_win32.h>

#include <algorithm>
#include <set>
#include <string>

namespace VantaMP::Nui::Internal {
    namespace {
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
    } // namespace

    nlohmann::json CurrentSettings() {
        const auto &config = Core::GetClientConfig();

        nlohmann::json settings;
        settings["nickname"]          = config.nickname;
        settings["lastServer"]        = config.serverHost + ":" + std::to_string(config.serverPort);
        settings["windowWidth"]       = config.displayWidth;
        settings["windowHeight"]      = config.displayHeight;
        settings["borderless"]        = config.displayBorderless;
        settings["exclusive"]         = config.displayExclusive;
        settings["fullscreen"]        = config.displayBorderless;
        settings["volumeMaster"]      = config.volumeMaster;
        settings["volumeSfx"]         = config.volumeSfx;
        settings["volumeRadio"]       = config.volumeRadio;
        settings["voiceInputDevice"]  = config.voiceInputDevice;
        settings["voiceOutputDevice"] = config.voiceOutputDevice;
        settings["voiceVolume"]       = config.voiceVolume;
        settings["voicePttKey"]       = config.voicePttKey;
        settings["netgraph"]          = config.netgraph;
        settings["display"]           = 0;
        settings["modes"]             = VideoModes();
        settings["displays"]          = nlohmann::json::array();
        return settings;
    }

    void EmitAudioDevices() {
        nlohmann::json data;
        data["input"]  = EndpointsJson(true);
        data["output"] = EndpointsJson(false);
        Emit(Event::kSettingsDevicesUpdate, nlohmann::json::array({data}));
    }

    void ApplySettingsChange(const std::string &key, const nlohmann::json &value) {
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
        } else if (key == "windowHeight" && value.is_number()) {
            height = value.get<unsigned>();
        } else if ((key == "borderless" || key == "fullscreen") && value.is_boolean()) {
            borderless = value.get<bool>();
        } else if (key == "exclusive" && value.is_boolean()) {
            exclusive = value.get<bool>();
        }

        if (key == "volumeMaster" && value.is_number()) {
            master = std::clamp(value.get<float>(), 0.0f, 1.0f);
        } else if (key == "volumeSfx" && value.is_number()) {
            sfx = std::clamp(value.get<float>(), 0.0f, 1.0f);
        } else if (key == "volumeRadio" && value.is_number()) {
            radio = std::clamp(value.get<float>(), 0.0f, 1.0f);
        } else if (key == "voiceInputDevice" && value.is_string()) {
            voiceIn = value.get<std::string>();
        } else if (key == "voiceOutputDevice" && value.is_string()) {
            voiceOut = value.get<std::string>();
        } else if (key == "voiceVolume" && value.is_number()) {
            voiceVol = std::clamp(value.get<float>(), 0.0f, 2.0f);
        } else if (key == "voicePttKey" && value.is_number()) {
            pttKey = value.get<int>();
        } else if (key == "netgraph" && value.is_boolean()) {
            SetNetgraphEnabled(value.get<bool>(), false);
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
                const bool started = Core::StartMicTest(Core::GetClientConfig().voiceInputDevice, Core::GetClientConfig().voiceOutputDevice);
                ReportMicTest(started);
            }

            Emit(Event::kSettingsUpdate, nlohmann::json::array({CurrentSettings()}));
        }
    }
}
