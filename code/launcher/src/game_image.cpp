#include "game_image.h"

#include <Windows.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "shared/constants.h"

namespace VantaMP::Launcher {
    namespace {
        // Where the Rockstar Games Launcher records the title's install folder.
        // A 32-bit launcher on a 64-bit host is redirected into WOW6432Node, so
        // both are tried rather than assuming which one exists.
        std::wstring RockstarInstallFolder() {
            const std::wstring subKeys[] = {
                std::wstring(L"SOFTWARE\\Rockstar Games\\") + Shared::kRockstarTitleKey,
                std::wstring(L"SOFTWARE\\WOW6432Node\\Rockstar Games\\") + Shared::kRockstarTitleKey,
            };

            for (const auto &subKey : subKeys) {
                HKEY key {};
                if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subKey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
                    continue;
                }

                wchar_t value[MAX_PATH] {};
                DWORD size = sizeof(value);
                DWORD type = 0;

                const auto status = RegQueryValueExW(key, L"InstallFolder", nullptr, &type, reinterpret_cast<LPBYTE>(value), &size);
                RegCloseKey(key);

                if (status == ERROR_SUCCESS && type == REG_SZ) {
                    return value;
                }
            }

            return {};
        }

        std::vector<std::uint8_t> ReadFileBytes(const std::wstring &path) {
            const auto file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

            if (file == INVALID_HANDLE_VALUE) {
                return {};
            }

            LARGE_INTEGER size {};
            if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0) {
                CloseHandle(file);
                return {};
            }

            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size.QuadPart));

            DWORD read    = 0;
            const bool ok = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size();

            CloseHandle(file);
            return ok ? bytes : std::vector<std::uint8_t> {};
        }

        // A virtual address, walked back to where it sits in the file. The image
        // has not been laid out by the loader at this point, so the two differ.
        const std::uint8_t *AtRva(const std::vector<std::uint8_t> &image, const IMAGE_NT_HEADERS32 *nt, std::uint32_t rva) {
            const auto *section = IMAGE_FIRST_SECTION(nt);

            for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
                if (rva >= section->VirtualAddress && rva < section->VirtualAddress + section->Misc.VirtualSize) {
                    const auto offset = section->PointerToRawData + (rva - section->VirtualAddress);
                    return offset < image.size() ? image.data() + offset : nullptr;
                }
            }

            return nullptr;
        }

        bool ImportsWrapper(const std::vector<std::uint8_t> &image) {
            if (image.size() < sizeof(IMAGE_DOS_HEADER)) {
                return false;
            }

            const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(image.data());

            if (dos->e_magic != IMAGE_DOS_SIGNATURE || static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS32) > image.size()) {
                return false;
            }

            const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(image.data() + dos->e_lfanew);

            if (nt->Signature != IMAGE_NT_SIGNATURE) {
                return false;
            }

            const auto &directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

            if (directory.VirtualAddress == 0) {
                return false;
            }

            const auto *descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR *>(AtRva(image, nt, directory.VirtualAddress));

            if (!descriptor) {
                return false;
            }

            for (; descriptor->Name != 0; ++descriptor) {
                const auto *name = reinterpret_cast<const char *>(AtRva(image, nt, descriptor->Name));

                // Case-insensitive: the name in an import table is whatever the
                // linker that wrote it chose.
                if (name && _strnicmp(name, "MTLX", 4) == 0) {
                    return true;
                }
            }

            return false;
        }
    } // namespace

    bool GameNeedsImageSnapshot() {
        const auto installFolder = RockstarInstallFolder();

        if (installFolder.empty()) {
            // Nothing installed through the Rockstar Games Launcher. Either the
            // player owns the game elsewhere - in which case there is no wrapper
            // - or the framework's own lookup will resolve it and the snapshot
            // is wanted after all.
            return false;
        }

        const auto executable = std::filesystem::path(installFolder) / Shared::kGameExecutable;
        const auto image      = ReadFileBytes(executable.wstring());

        if (image.empty()) {
            // The registry names a game that is not readable from here. Leave
            // the snapshot on and let the framework report what it finds.
            return true;
        }

        return ImportsWrapper(image);
    }
} // namespace VantaMP::Launcher
