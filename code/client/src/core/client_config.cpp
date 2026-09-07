#include "client_config.h"

#include <utils/safe_win32.h>

#include <logging/logger.h>
#include <utils/config.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace VantaMP::Core {
    namespace {
        ClientConfig gConfig;

        // Remembered so a later save writes back to the same file.
        std::string gConfigPath;

        constexpr const char *kFileName = "VantaMP.json";

        std::string ReadTextFile(const std::filesystem::path &path) {
            std::ifstream file(path);

            if (!file) {
                return {};
            }

            std::ostringstream contents;
            contents << file.rdbuf();
            return contents.str();
        }

        // Writes the config back with every field present.
        //
        // So the file a player opens shows what they can change and what it is
        // currently set to, instead of only the handful of keys they happened to
        // type. A first run therefore documents the options by producing them.
        void WriteConfig(const std::filesystem::path &path, const ClientConfig &config) {
            nlohmann::json document;
            document["display_width"]       = config.displayWidth;
            document["display_height"]      = config.displayHeight;
            document["display_borderless"]  = config.displayBorderless;
            document["display_exclusive"]   = config.displayExclusive;
            document["display_adapter"]     = config.displayAdapter;
            document["display_setup_done"]  = config.displaySetupDone;
            document["frame_limit"]         = config.frameLimit;
            document["volume_master"]      = config.volumeMaster;
            document["volume_sfx"]         = config.volumeSfx;
            document["volume_radio"]       = config.volumeRadio;
            document["voice_input_device"]  = config.voiceInputDevice;
            document["voice_output_device"] = config.voiceOutputDevice;
            document["voice_volume"]        = config.voiceVolume;
            document["voice_ptt_key"]       = config.voicePttKey;
            document["server_host"]        = config.serverHost;
            document["server_port"]        = config.serverPort;
            document["nickname"]           = config.nickname;

            std::ofstream file(path);

            if (!file) {
                Framework::Logging::GetLogger("Config")->warn("{} could not be written; settings will not persist", path.string());
                return;
            }

            file << document.dump(4) << '\n';
        }
    } // namespace

    const ClientConfig &LoadClientConfig(const std::string &projectPath) {
        gConfigPath = projectPath;

        const auto path = std::filesystem::path(projectPath) / kFileName;
        const auto logger = Framework::Logging::GetLogger("Config");

        const auto contents = ReadTextFile(path);

        if (contents.empty()) {
            logger->info("No {} yet; writing one with the defaults", kFileName);
            WriteConfig(path, gConfig);
            return gConfig;
        }

        Framework::Utils::Config parsed;

        if (!parsed.Parse(contents) || !parsed.IsParsed()) {
            // NOT FATAL. A player who mistypes a brace gets a working game and a
            // warning; refusing to start over a settings file would be a far
            // worse outcome than ignoring it.
            logger->warn("{} could not be parsed ({}); using defaults", kFileName, parsed.GetLastError());
            return gConfig;
        }

        gConfig.displayWidth      = parsed.GetDefault<std::uint32_t>("display_width", gConfig.displayWidth);
        gConfig.displayHeight     = parsed.GetDefault<std::uint32_t>("display_height", gConfig.displayHeight);
        gConfig.displayBorderless = parsed.GetDefault<bool>("display_borderless", gConfig.displayBorderless);
        gConfig.displayExclusive  = parsed.GetDefault<bool>("display_exclusive", gConfig.displayExclusive);
        gConfig.displayAdapter    = parsed.GetDefault<std::uint32_t>("display_adapter", gConfig.displayAdapter);
        gConfig.frameLimit        = parsed.GetDefault<std::uint32_t>("frame_limit", gConfig.frameLimit);
        gConfig.volumeMaster      = std::clamp(parsed.GetDefault<float>("volume_master", gConfig.volumeMaster), 0.0f, 1.0f);
        gConfig.volumeSfx         = std::clamp(parsed.GetDefault<float>("volume_sfx", gConfig.volumeSfx), 0.0f, 1.0f);
        gConfig.volumeRadio       = std::clamp(parsed.GetDefault<float>("volume_radio", gConfig.volumeRadio), 0.0f, 1.0f);
        gConfig.voiceInputDevice  = parsed.GetDefault<std::string>("voice_input_device", gConfig.voiceInputDevice);
        gConfig.voiceOutputDevice = parsed.GetDefault<std::string>("voice_output_device", gConfig.voiceOutputDevice);
        gConfig.voiceVolume       = std::clamp(parsed.GetDefault<float>("voice_volume", gConfig.voiceVolume), 0.0f, 2.0f);
        gConfig.voicePttKey       = parsed.GetDefault<int>("voice_ptt_key", gConfig.voicePttKey);
        gConfig.serverHost        = parsed.GetDefault<std::string>("server_host", gConfig.serverHost);
        gConfig.serverPort        = parsed.GetDefault<std::uint32_t>("server_port", gConfig.serverPort);
        gConfig.nickname          = parsed.GetDefault<std::string>("nickname", gConfig.nickname);

        if (gConfig.voiceInputDevice.empty()) {
            gConfig.voiceInputDevice = "default";
        }

        if (gConfig.voiceOutputDevice.empty()) {
            gConfig.voiceOutputDevice = "default";
        }

        const char *mode = gConfig.displayExclusive ? "exclusive" : gConfig.displayBorderless ? "borderless" : "windowed";
        logger->info("Display: {} {}x{}", mode, gConfig.displayWidth, gConfig.displayHeight);
        logger->info("Voice input {} output {}", gConfig.voiceInputDevice, gConfig.voiceOutputDevice);

        // ONLY REWRITTEN WHEN SOMETHING WAS MISSING.
        //
        // The point is to add keys a file predating them does not have, so the
        // player can see every option. Writing unconditionally does something
        // much worse: an edit made while the client is running is overwritten
        // on the next start, so changing the resolution appears to do nothing
        // and the window comes back the size it was.
        auto *document = parsed.GetDocument();

        // An older file has already been through a session, so missing
        // display_setup_done means "done" — not "show the dialog again".
        if (document && document->contains("display_setup_done")) {
            gConfig.displaySetupDone = parsed.GetDefault<bool>("display_setup_done", false);
        } else {
            gConfig.displaySetupDone = true;
        }

        const bool complete = document && document->contains("display_width") && document->contains("display_height") && document->contains("display_borderless") && document->contains("display_exclusive") && document->contains("display_adapter") && document->contains("display_setup_done") && document->contains("frame_limit") && document->contains("volume_master") && document->contains("volume_sfx") && document->contains("volume_radio") && document->contains("voice_input_device") && document->contains("voice_output_device") && document->contains("voice_volume") && document->contains("voice_ptt_key") && document->contains("server_host") && document->contains("server_port") && document->contains("nickname");

        if (!complete) {
            logger->info("{} was missing settings; filling them in", kFileName);
            WriteConfig(path, gConfig);
        }

        return gConfig;
    }

    void SaveDisplaySettings(std::uint32_t width, std::uint32_t height, bool borderless, bool exclusive, std::uint32_t adapter) {
        gConfig.displayWidth      = width;
        gConfig.displayHeight     = height;
        gConfig.displayBorderless = borderless && !exclusive;
        gConfig.displayExclusive  = exclusive;
        gConfig.displaySetupDone  = true;

        if (adapter != 0xFFFFFFFFu) {
            gConfig.displayAdapter = adapter;
        }

        WriteConfig(std::filesystem::path(gConfigPath) / kFileName, gConfig);

        const char *mode = gConfig.displayExclusive ? "exclusive" : gConfig.displayBorderless ? "borderless" : "windowed";
        Framework::Logging::GetLogger("Config")->info("Display saved: {} {}x{}", mode, width, height);
    }

    void SaveVoiceSettings(const std::string &inputDevice, const std::string &outputDevice, float volume, int pttKey) {
        gConfig.voiceInputDevice  = inputDevice.empty() ? "default" : inputDevice;
        gConfig.voiceOutputDevice = outputDevice.empty() ? "default" : outputDevice;
        gConfig.voiceVolume       = std::clamp(volume, 0.0f, 2.0f);
        gConfig.voicePttKey       = pttKey;

        WriteConfig(std::filesystem::path(gConfigPath) / kFileName, gConfig);
    }

    void SaveVolumeSettings(float master, float sfx, float radio) {
        gConfig.volumeMaster = std::clamp(master, 0.0f, 1.0f);
        gConfig.volumeSfx    = std::clamp(sfx, 0.0f, 1.0f);
        gConfig.volumeRadio  = std::clamp(radio, 0.0f, 1.0f);

        WriteConfig(std::filesystem::path(gConfigPath) / kFileName, gConfig);
    }

    void SaveConnectSettings(const std::string &host, std::uint32_t port, const std::string &nickname) {
        gConfig.serverHost = host;
        gConfig.serverPort = port;

        if (!nickname.empty()) {
            gConfig.nickname = nickname;
        }

        WriteConfig(std::filesystem::path(gConfigPath) / kFileName, gConfig);
    }

    const ClientConfig &GetClientConfig() {
        return gConfig;
    }

    bool IsDisplaySetupDone() {
        return gConfig.displaySetupDone;
    }
} // namespace VantaMP::Core
