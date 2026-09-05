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

            std::ofstream file(path);

            if (!file) {
                Framework::Logging::GetLogger("Config")->warn("{} could not be written; settings will not persist", path.string());
                return;
            }

            file << document.dump(4) << '\n';
        }
    } // namespace

    const ClientConfig &LoadClientConfig(const std::string &projectPath) {
        const auto path = std::filesystem::path(projectPath) / kFileName;
        const auto logger = Framework::Logging::GetLogger("Config");

        const auto contents = ReadTextFile(path);

        if (contents.empty()) {
            logger->info("No {} yet; writing one with the defaults", kFileName);
            WriteConfig(path, gConfig);
            return gConfig;
        }

        Framework::Utils::Config document;

        if (!document.Parse(contents) || !document.IsParsed()) {
            // NOT FATAL. A player who mistypes a brace gets a working game and a
            // warning; refusing to start over a settings file would be a far
            // worse outcome than ignoring it.
            logger->warn("{} could not be parsed ({}); using defaults", kFileName, document.GetLastError());
            return gConfig;
        }

        gConfig.displayWidth      = document.GetDefault<std::uint32_t>("display_width", gConfig.displayWidth);
        gConfig.displayHeight     = document.GetDefault<std::uint32_t>("display_height", gConfig.displayHeight);
        gConfig.displayBorderless = document.GetDefault<bool>("display_borderless", gConfig.displayBorderless);

        logger->info("Display: {} {}x{}", gConfig.displayBorderless ? "borderless" : "windowed", gConfig.displayWidth, gConfig.displayHeight);

        // Rewritten so a file that predates a new setting gains it rather than
        // silently keeping a default the player cannot see.
        WriteConfig(path, gConfig);

        return gConfig;
    }

    const ClientConfig &GetClientConfig() {
        return gConfig;
    }
} // namespace VantaMP::Core
