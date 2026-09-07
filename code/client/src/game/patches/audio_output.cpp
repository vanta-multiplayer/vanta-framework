#include "game/patches/audio_output.h"

#include "core/client_config.h"

#include <utils/safe_win32.h>

#include <MinHook.h>
#include <logging/logger.h>
#include <utils/hooking/hook_function.h>

#include <cstdio>
#include <mutex>
#include <string>

namespace VantaMP::Game {
    namespace {
        using DirectSoundCreateFn  = HRESULT(WINAPI *)(const GUID *, void **, void *);
        using DirectSoundCreate8Fn = HRESULT(WINAPI *)(const GUID *, void **, void *);

        DirectSoundCreateFn gDirectSoundCreate   = nullptr;
        DirectSoundCreate8Fn gDirectSoundCreate8 = nullptr;

        std::mutex gLock;
        GUID gOutput {};
        bool gHasOutput = false;

        bool ParseGuid(const char *text, GUID &out) {
            unsigned d1 = 0;
            unsigned d2 = 0;
            unsigned d3 = 0;
            unsigned b[8] {};

            if (std::sscanf(text, "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", &d1, &d2, &d3, &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6], &b[7]) != 11) {
                return false;
            }

            out.Data1    = d1;
            out.Data2    = static_cast<unsigned short>(d2);
            out.Data3    = static_cast<unsigned short>(d3);
            out.Data4[0] = static_cast<unsigned char>(b[0]);
            out.Data4[1] = static_cast<unsigned char>(b[1]);
            out.Data4[2] = static_cast<unsigned char>(b[2]);
            out.Data4[3] = static_cast<unsigned char>(b[3]);
            out.Data4[4] = static_cast<unsigned char>(b[4]);
            out.Data4[5] = static_cast<unsigned char>(b[5]);
            out.Data4[6] = static_cast<unsigned char>(b[6]);
            out.Data4[7] = static_cast<unsigned char>(b[7]);
            return true;
        }

        bool GuidFromEndpoint(const std::string &deviceId, GUID &out) {
            if (deviceId.empty() || deviceId == "default") {
                return false;
            }

            const auto brace = deviceId.rfind('{');
            if (brace == std::string::npos) {
                return false;
            }

            return ParseGuid(deviceId.c_str() + brace, out);
        }

        HRESULT WINAPI DirectSoundCreate_Hook(const GUID *device, void **object, void *outer) {
            GUID copy {};
            bool have = false;

            {
                std::lock_guard lock(gLock);
                have = gHasOutput;
                if (have) {
                    copy = gOutput;
                }
            }

            return gDirectSoundCreate(have ? &copy : device, object, outer);
        }

        HRESULT WINAPI DirectSoundCreate8_Hook(const GUID *device, void **object, void *outer) {
            GUID copy {};
            bool have = false;

            {
                std::lock_guard lock(gLock);
                have = gHasOutput;
                if (have) {
                    copy = gOutput;
                }
            }

            return gDirectSoundCreate8(have ? &copy : device, object, outer);
        }
    } // namespace

    void SetGameOutputDevice(const std::string &deviceId) {
        GUID guid {};
        const bool have = GuidFromEndpoint(deviceId, guid);

        std::lock_guard lock(gLock);
        gHasOutput = have;
        if (have) {
            gOutput = guid;
            Framework::Logging::GetLogger("Audio")->info("Game output will use {} on the next DirectSound open", deviceId);
        }
        else {
            Framework::Logging::GetLogger("Audio")->info("Game output uses the system default");
        }
    }

    void InstallGameOutputHook() {
        SetGameOutputDevice(Core::GetClientConfig().voiceOutputDevice);

        const auto dsound = LoadLibraryW(L"dsound.dll");
        if (!dsound) {
            Framework::Logging::GetLogger("Audio")->warn("dsound.dll could not be loaded; game output stays on the default device");
            return;
        }

        if (const auto create = GetProcAddress(dsound, "DirectSoundCreate")) {
            MH_CreateHook(reinterpret_cast<LPVOID>(create), reinterpret_cast<LPVOID>(&DirectSoundCreate_Hook), reinterpret_cast<void **>(&gDirectSoundCreate));
            MH_EnableHook(reinterpret_cast<LPVOID>(create));
        }

        if (const auto create8 = GetProcAddress(dsound, "DirectSoundCreate8")) {
            MH_CreateHook(reinterpret_cast<LPVOID>(create8), reinterpret_cast<LPVOID>(&DirectSoundCreate8_Hook), reinterpret_cast<void **>(&gDirectSoundCreate8));
            MH_EnableHook(reinterpret_cast<LPVOID>(create8));
        }
    }
} // namespace VantaMP::Game

static InitFunction init(
    []() {
        VantaMP::Game::InstallGameOutputHook();
    },
    "GameOutput");
