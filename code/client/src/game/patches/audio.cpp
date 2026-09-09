#include "game/patches/audio.h"

#include "core/client_config.h"
#include "sdk/patterns.h"

#include <utils/safe_win32.h>

#include <MinHook.h>

#include <logging/logger.h>
#include <utils/hooking/hooking.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace VantaMP::Game {
    namespace {
        constexpr std::ptrdiff_t kSfxVolumeOffset   = 0x4F;
        constexpr std::ptrdiff_t kRadioVolumeOffset = 0x50;
        constexpr float          kGameVolumeMax     = 64.0f;

        float gMaster = 1.0f;
        float gSfx    = 1.0f;
        float gRadio  = 1.0f;

        using LoadSettingsFn = void(__thiscall *)(void *);
        using SetVolumeFn    = void(__thiscall *)(void *engine, char volume);

        LoadSettingsFn gLoadSettings = nullptr;

        char Scaled(float channel) {
            const float value = std::clamp(channel, 0.0f, 1.0f) * std::clamp(gMaster, 0.0f, 1.0f);
            return static_cast<char>(std::lround(value * kGameVolumeMax));
        }

        void __fastcall LoadSettings_Hook(void *menu, void *) {
            gLoadSettings(menu);
            ApplyGameVolumes();
        }
    } // namespace

    void ApplyGameVolumes() {
        const auto &p = SDK::gPatterns;

        if (!p.CMenuManager || !p.CAudioEngine || !p.CAudioEngine__SetMusicMasterVolume || !p.CAudioEngine__SetEffectsMasterVolume) {
            return;
        }

        const auto sfx   = Scaled(gSfx);
        const auto radio = Scaled(gRadio);
        auto *menu       = reinterpret_cast<std::uint8_t *>(p.CMenuManager);

        menu[kSfxVolumeOffset]   = static_cast<std::uint8_t>(sfx);
        menu[kRadioVolumeOffset] = static_cast<std::uint8_t>(radio);

        auto *engine = reinterpret_cast<void *>(p.CAudioEngine);
        reinterpret_cast<SetVolumeFn>(p.CAudioEngine__SetMusicMasterVolume)(engine, radio);
        reinterpret_cast<SetVolumeFn>(p.CAudioEngine__SetEffectsMasterVolume)(engine, sfx);
    }

    void SetVolumeSettings(float master, float sfx, float radio) {
        gMaster = std::clamp(master, 0.0f, 1.0f);
        gSfx    = std::clamp(sfx, 0.0f, 1.0f);
        gRadio  = std::clamp(radio, 0.0f, 1.0f);
        ApplyGameVolumes();
    }

    void ApplyAudioPatches() {
        const auto &config = Core::GetClientConfig();
        gMaster            = std::clamp(config.volumeMaster, 0.0f, 1.0f);
        gSfx               = std::clamp(config.volumeSfx, 0.0f, 1.0f);
        gRadio             = std::clamp(config.volumeRadio, 0.0f, 1.0f);

        const auto loadSettings = SDK::gPatterns.CMenuManager__LoadSettings;

        if (!loadSettings) {
            return;
        }

        // AFTER the original. LoadSettings writes GTA's gta_sa.set volumes
        // into the engine; applying before that is undone on the same call.
        MH_CreateHook(
            reinterpret_cast<LPVOID>(loadSettings),
            reinterpret_cast<LPVOID>(&LoadSettings_Hook),
            reinterpret_cast<void **>(&gLoadSettings));
        MH_EnableHook(reinterpret_cast<LPVOID>(loadSettings));
    }
} // namespace VantaMP::Game
