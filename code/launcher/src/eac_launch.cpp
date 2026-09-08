#include "eac_launch.h"

#include <logging/logger.h>
#include <utils/safe_win32.h>

#include <TlHelp32.h>

#include <filesystem>
#include <string>

namespace VantaMP::Launcher {
    namespace {
        constexpr const wchar_t *kBootstrapper = L"start_protected_game.exe";

        std::filesystem::path LauncherDirectory() {
            wchar_t modulePath[MAX_PATH] {};
            GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
            return std::filesystem::path(modulePath).parent_path();
        }

        bool ParentIsStartProtectedGame() {
            const DWORD self = GetCurrentProcessId();
            DWORD parentPid  = 0;

            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snap == INVALID_HANDLE_VALUE) {
                return false;
            }

            PROCESSENTRY32W entry {};
            entry.dwSize = sizeof(entry);
            if (Process32FirstW(snap, &entry)) {
                do {
                    if (entry.th32ProcessID == self) {
                        parentPid = entry.th32ParentProcessID;
                        break;
                    }
                } while (Process32NextW(snap, &entry));
            }

            bool match = false;
            if (parentPid != 0) {
                entry.dwSize = sizeof(entry);
                if (Process32FirstW(snap, &entry)) {
                    do {
                        if (entry.th32ProcessID == parentPid) {
                            match = _wcsicmp(entry.szExeFile, kBootstrapper) == 0;
                            break;
                        }
                    } while (Process32NextW(snap, &entry));
                }
            }

            CloseHandle(snap);
            return match;
        }

        bool StartProtectedGameBootstrapper(const std::filesystem::path &bootstrapper) {
            const std::wstring exe = bootstrapper.wstring();
            const std::wstring dir = bootstrapper.parent_path().wstring();

            STARTUPINFOW si {};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi {};

            std::wstring cmd = L"\"" + exe + L"\"";

            const BOOL ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi);
            if (!ok) {
                Framework::Logging::GetLogger("EAC")->error("CreateProcess({}) failed ({})", bootstrapper.string(), GetLastError());
                return false;
            }

            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return true;
        }
    } // namespace

    bool PinWorkingDirectoryToLauncher() {
        const auto clientRoot = LauncherDirectory();
        std::error_code ec;
        std::filesystem::current_path(clientRoot, ec);
        return !ec;
    }

    bool EnsureEacProtectedProcess() {
        auto log              = Framework::Logging::GetLogger("EAC");
        const auto clientRoot = LauncherDirectory();
        const auto eacRoot    = clientRoot / "EasyAntiCheat";

        log->info("Client runtime: {}", clientRoot.string());
        log->info("EAC runtime: {}", eacRoot.string());

        if (ParentIsStartProtectedGame()) {
            (void)PinWorkingDirectoryToLauncher();
            return true;
        }

        const auto bootstrapper = clientRoot / kBootstrapper;
        if (!std::filesystem::is_regular_file(bootstrapper)) {
            return true;
        }

        if (!std::filesystem::is_regular_file(eacRoot / "Settings.json")) {
            return true;
        }

        if (!StartProtectedGameBootstrapper(bootstrapper)) {
            return true;
        }

        return false;
    }
} // namespace VantaMP::Launcher
