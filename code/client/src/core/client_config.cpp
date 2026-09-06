#include "client_config.h"

#include <utils/safe_win32.h>

#include <logging/logger.h>
#include <utils/config.h>

#include <nlohmann/json.hpp>

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
            document["display_width"]      = config.displayWidth;
            document["display_height"]     = config.displayHeight;
            document["display_borderless"] = config.displayBorderless;
            document["frame_limit"]        = config.frameLimit;
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
        gConfig.frameLimit        = parsed.GetDefault<std::uint32_t>("frame_limit", gConfig.frameLimit);
        gConfig.serverHost        = parsed.GetDefault<std::string>("server_host", gConfig.serverHost);
        gConfig.serverPort        = parsed.GetDefault<std::uint32_t>("server_port", gConfig.serverPort);
        gConfig.nickname          = parsed.GetDefault<std::string>("nickname", gConfig.nickname);

        logger->info("Display: {} {}x{}", gConfig.displayBorderless ? "borderless" : "windowed", gConfig.displayWidth, gConfig.displayHeight);

        // ONLY REWRITTEN WHEN SOMETHING WAS MISSING.
        //
        // The point is to add keys a file predating them does not have, so the
        // player can see every option. Writing unconditionally does something
        // much worse: an edit made while the client is running is overwritten
        // on the next start, so changing the resolution appears to do nothing
        // and the window comes back the size it was.
        auto *document = parsed.GetDocument();

        const bool complete = document && document->contains("display_width") && document->contains("display_height") && document->contains("display_borderless") && document->contains("frame_limit") && document->contains("server_host") && document->contains("server_port") && document->contains("nickname");

        if (!complete) {
            logger->info("{} was missing settings; filling them in", kFileName);
            WriteConfig(path, gConfig);
        }

        return gConfig;
    }

    void SaveDisplaySettings(std::uint32_t width, std::uint32_t height, bool borderless) {
        gConfig.displayWidth      = width;
        gConfig.displayHeight     = height;
        gConfig.displayBorderless = borderless;

        WriteConfig(std::filesystem::path(gConfigPath) / kFileName, gConfig);

        Framework::Logging::GetLogger("Config")->info("Display saved: {} {}x{}", borderless ? "borderless" : "windowed", width, height);
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
} // namespace VantaMP::Core
