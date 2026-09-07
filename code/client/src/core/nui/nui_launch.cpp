#include "internal.h"

#include "core/nui/events.h"

#include <utils/safe_win32.h>

#include <cctype>
#include <exception>
#include <sstream>
#include <string>
#include <string_view>

namespace VantaMP::Nui::Internal {
    namespace {
        std::string UrlDecode(std::string_view input) {
            std::string out;
            out.reserve(input.size());

            for (size_t i = 0; i < input.size(); ++i) {
                if (input[i] == '%' && i + 2 < input.size() && std::isxdigit(static_cast<unsigned char>(input[i + 1])) &&
                    std::isxdigit(static_cast<unsigned char>(input[i + 2]))) {
                    auto hex = [](char ch) -> int {
                        if (ch >= '0' && ch <= '9') {
                            return ch - '0';
                        }

                        return 10 + (std::tolower(static_cast<unsigned char>(ch)) - 'a');
                    };

                    out.push_back(static_cast<char>((hex(input[i + 1]) << 4) | hex(input[i + 2])));
                    i += 2;
                    continue;
                }

                out.push_back(input[i] == '+' ? ' ' : input[i]);
            }

            return out;
        }

        std::string Unquote(std::string value) {
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                return value.substr(1, value.size() - 2);
            }

            return value;
        }

        nlohmann::json ServerFromTarget(const std::string &raw) {
            auto target          = Unquote(raw);
            nlohmann::json query = nlohmann::json::object();
            std::string path     = target;

            if (const auto mark = target.find('?'); mark != std::string::npos) {
                path = target.substr(0, mark);
                std::stringstream stream(target.substr(mark + 1));
                std::string pair;

                while (std::getline(stream, pair, '&')) {
                    const auto eq    = pair.find('=');
                    const auto key   = UrlDecode(eq == std::string::npos ? pair : pair.substr(0, eq));
                    const auto value = UrlDecode(eq == std::string::npos ? "" : pair.substr(eq + 1));

                    if (!key.empty()) {
                        query[key] = value;
                    }
                }
            }

            constexpr std::string_view kScheme = "vanta://";

            if (path.rfind(kScheme.data(), 0) == 0) {
                path = path.substr(kScheme.size());
            }

            if (path.rfind("connect/", 0) == 0) {
                path = path.substr(8);
            } else if (path.rfind("join/", 0) == 0) {
                path = path.substr(5);
            }

            while (!path.empty() && path.back() == '/') {
                path.pop_back();
            }

            nlohmann::json server;
            server["address"]    = path;
            server["name"]       = query.value("name", path);
            server["players"]    = 0;
            server["maxPlayers"] = 0;
            server["ping"]       = 0;

            for (const char *key : {"language", "gamemode", "website", "description", "banner", "avatar", "tags"}) {
                if (query.contains(key) && query[key].is_string() && !query[key].get<std::string>().empty()) {
                    if (std::string(key) == "tags") {
                        nlohmann::json list = nlohmann::json::array();
                        std::stringstream tags(query[key].get<std::string>());
                        std::string tag;

                        while (std::getline(tags, tag, ',')) {
                            tag = UrlDecode(tag);

                            if (!tag.empty()) {
                                list.push_back(tag);
                            }
                        }

                        server["tags"] = list;
                    } else {
                        server[key] = query[key];
                    }
                }
            }

            auto asInt = [&](const char *key) {
                if (!query.contains(key) || !query[key].is_string()) {
                    return;
                }

                try {
                    server[key] = std::stoi(query[key].get<std::string>());
                } catch (const std::exception &) {
                }
            };

            asInt("players");
            asInt("maxPlayers");
            asInt("ping");
            return server;
        }
    } // namespace

    std::optional<nlohmann::json> ParseLaunchInvite() {
        const char *commandLine = GetCommandLineA();

        if (!commandLine) {
            return std::nullopt;
        }

        const std::string command = commandLine;

        if (const auto proto = command.find("vanta://"); proto != std::string::npos) {
            size_t end = proto;

            while (end < command.size() && command[end] != '"' && command[end] != ' ' && command[end] != '\t') {
                ++end;
            }

            auto server = ServerFromTarget(command.substr(proto, end - proto));

            if (server["address"].is_string() && !server["address"].get<std::string>().empty()) {
                return server;
            }
        }

        if (const auto flag = command.find("--connect"); flag != std::string::npos) {
            auto rest = command.substr(flag + 9);

            while (!rest.empty() && (rest.front() == '=' || rest.front() == ' ' || rest.front() == '\t')) {
                rest.erase(rest.begin());
            }

            rest = Unquote(rest.substr(0, rest.find_first_of(" \t")));

            if (!rest.empty()) {
                return ServerFromTarget(rest);
            }
        }

        return std::nullopt;
    }

    void RegisterLaunchProtocol() {
        wchar_t exe[MAX_PATH];

        if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) {
            return;
        }

        std::wstring command = L"\"";
        command += exe;
        command += L"\" \"%1\"";

        HKEY key = nullptr;

        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\vanta", 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
            return;
        }

        const wchar_t *desc = L"URL:Vanta Protocol";
        RegSetValueExW(key, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE *>(desc), static_cast<DWORD>((wcslen(desc) + 1) * sizeof(wchar_t)));
        const wchar_t empty[] = L"";
        RegSetValueExW(key, L"URL Protocol", 0, REG_SZ, reinterpret_cast<const BYTE *>(empty), sizeof(empty));
        RegCloseKey(key);

        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\vanta\\shell\\open\\command", 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
            return;
        }

        RegSetValueExW(key, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE *>(command.c_str()),
                       static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
    }
}
