#include "internal.h"

#include "core/application.h"
#include "core/client_config.h"
#include "core/globals.h"
#include "core/nui/events.h"
#include "shared/version.h"

#include <utils/safe_win32.h>

#include <logging/logger.h>
#include <spdlog/spdlog.h>

#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>

namespace VantaMP::Nui::Internal {
    namespace {
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

        std::string LowerAscii(std::string value) {
            for (char &ch : value) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }

            return value;
        }

        bool ParseBoolToken(const std::string &token, bool &out) {
            const auto value = LowerAscii(token);

            if (value == "1" || value == "on" || value == "true" || value == "yes") {
                out = true;
                return true;
            }

            if (value == "0" || value == "off" || value == "false" || value == "no") {
                out = false;
                return true;
            }

            return false;
        }

        void Reply(const char *level, const std::string &message) {
            PushConsoleLine(level, "Console", message, ClockTime());
        }

        std::string TrimCopy(std::string value) {
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
                value.erase(value.begin());
            }

            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
                value.pop_back();
            }

            return value;
        }

        std::string LastServerAddress() {
            const auto &config = Core::GetClientConfig();
            return config.serverHost + ":" + std::to_string(config.serverPort);
        }

        struct BuiltinCommand {
            const char *name;
            const char *aliases;
            const char *usage;
            const char *hint;
            const char *args;
        };

        constexpr BuiltinCommand kBuiltinCommands[] = {
            {"help", "?", "help", "List client commands", nullptr},
            {"clear", "cls", "clear", "Clear the console", nullptr},
            {"connect", nullptr, "connect <host[:port]>", "Join a server", nullptr},
            {"disconnect", "dc", "disconnect", "Leave the current server", nullptr},
            {"reconnect", nullptr, "reconnect", "Join the last server again", nullptr},
            {"quit", "exit q", "quit", "Close the game", nullptr},
            {"name", "nick", "name <nickname>", "Set your player name", nullptr},
            {"netgraph", "shownetgraph togglenetgraph", "netgraph [on|off]", "Toggle the netgraph (saved)", "on off"},
            {"status", nullptr, "status", "Show connection and client info", nullptr},
            {"version", nullptr, "version", "Print the client version", nullptr},
        };

        bool CommandMatches(const BuiltinCommand &command, const std::string &verb) {
            if (verb == command.name) {
                return true;
            }

            if (!command.aliases) {
                return false;
            }

            std::istringstream aliases(command.aliases);
            std::string alias;

            while (aliases >> alias) {
                if (verb == alias) {
                    return true;
                }
            }

            return false;
        }

        const BuiltinCommand *FindBuiltin(const std::string &verb) {
            for (const auto &command : kBuiltinCommands) {
                if (CommandMatches(command, verb)) {
                    return &command;
                }
            }

            return nullptr;
        }

        void JoinAddress(const std::string &address) {
            const auto target = address.empty() ? LastServerAddress() : address;
            const auto nick   = Core::GetClientConfig().nickname.empty() ? "Player" : Core::GetClientConfig().nickname;

            if (gInWorld) {
                gReconnectTo      = target;
                gReconnectPending = true;
                Reply("info", "Leaving, then joining " + target);
                Core::RequestDisconnect();
                return;
            }

            Reply("info", "Connecting to " + target);
            Core::RequestConnect(target, nick);
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
                std::string time     = ClockTime();
                std::string category = fallbackCategory;
                std::string level    = "info";
                std::string message  = raw;

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
                            } else if (level == "warning" || level == "warn") {
                                level = "warn";
                            } else if (level == "critical" || level == "fatal") {
                                level = "fatal";
                            } else if (level == "trace") {
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
    } // namespace

    void SetNetgraphEnabled(bool enabled, bool announce) {
        gNetgraphOn = enabled;
        Core::SaveNetgraphSetting(enabled);
        Emit(Event::kSettingsUpdate, nlohmann::json::array({CurrentSettings()}));

        if (announce) {
            Reply("info", enabled ? "Netgraph on" : "Netgraph off");
        }
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

    void EmitCommandCatalog() {
        nlohmann::json list = nlohmann::json::array();

        for (const auto &command : kBuiltinCommands) {
            nlohmann::json item;
            item["name"]    = command.name;
            item["usage"]   = command.usage;
            item["hint"]    = command.hint;
            item["aliases"] = nlohmann::json::array();
            item["args"]    = nlohmann::json::array();

            if (command.aliases) {
                std::istringstream aliases(command.aliases);
                std::string alias;

                while (aliases >> alias) {
                    item["aliases"].push_back(alias);
                }
            }

            if (command.args) {
                std::istringstream args(command.args);
                std::string arg;

                while (args >> arg) {
                    item["args"].push_back(arg);
                }
            }

            list.push_back(std::move(item));
        }

        Emit(Event::kConsoleCommands, nlohmann::json::array({list}));
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

    void ExecuteClientCommand(const std::string &command) {
        std::istringstream stream(command);
        std::string verb;
        stream >> verb;
        verb = LowerAscii(verb);

        if (verb.empty()) {
            return;
        }

        std::string rest;
        std::getline(stream, rest);
        rest = TrimCopy(rest);

        const auto *found = FindBuiltin(verb);

        if (!found) {
            Reply("warn", "Unknown command. Type help.");
            return;
        }

        const std::string name = found->name;

        if (name == "help") {
            for (const auto &entry : kBuiltinCommands) {
                std::string line = entry.usage;

                if (line.size() < 24) {
                    line.append(24 - line.size(), ' ');
                } else {
                    line += "  ";
                }

                line += entry.hint;
                Reply("info", line);
            }

            return;
        }

        if (name == "clear") {
            Emit(Event::kConsoleClear);
            return;
        }

        if (name == "connect") {
            if (rest.empty()) {
                Reply("warn", "Usage: connect <host[:port]>");
                return;
            }

            JoinAddress(rest);
            return;
        }

        if (name == "disconnect") {
            if (!gInWorld) {
                Reply("warn", "Not connected.");
                return;
            }

            Reply("info", "Disconnecting");
            Core::RequestDisconnect();
            return;
        }

        if (name == "reconnect") {
            JoinAddress(LastServerAddress());
            return;
        }

        if (name == "quit") {
            Reply("info", "Quitting");
            PostQuitMessage(0);
            return;
        }

        if (name == "name") {
            if (rest.empty()) {
                Reply("info", "Name is " + Core::GetClientConfig().nickname);
                return;
            }

            if (rest.size() > 24) {
                rest.resize(24);
            }

            const auto &config = Core::GetClientConfig();
            Core::SaveConnectSettings(config.serverHost, config.serverPort, rest);
            Emit(Event::kSettingsUpdate, nlohmann::json::array({CurrentSettings()}));
            Reply("info", "Name set to " + rest);
            return;
        }

        if (name == "netgraph") {
            bool enabled = !gNetgraphOn;

            if (!rest.empty() && !ParseBoolToken(rest, enabled)) {
                Reply("warn", "Usage: netgraph [on|off]");
                return;
            }

            SetNetgraphEnabled(enabled, true);
            return;
        }

        if (name == "status") {
            const auto &config = Core::GetClientConfig();
            Reply("info", std::string("Vanta ") + Version::Label());
            Reply("info", std::string("Name ") + config.nickname);
            Reply("info", "Server " + LastServerAddress());
            Reply("info", gInWorld ? "In world" : "Not connected");
            Reply("info", gNetgraphOn ? "Netgraph on" : "Netgraph off");
            return;
        }

        if (name == "version") {
            Reply("info", std::string("Vanta ") + Version::Label());
        }
    }
}
