#include "streaming.h"

#include "sdk/game_sdk.h"
#include "sdk/patterns.h"

namespace VantaMP::SDK {
    namespace {
        // Flags RequestModel takes. The two the game itself passes when it needs
        // something now: GAME_REQUIRED marks the model as one that must not be
        // evicted while in use, PRIORITY_REQUEST puts it at the front of the
        // queue. MTA requests weapon models with the same pair.
        constexpr int kModelGameRequired    = 0x02;
        constexpr int kModelPriorityRequest = 0x08;

        using RequestModelFn           = void(__cdecl *)(int, int);
        using LoadAllRequestedModelsFn = void(__cdecl *)(bool);
        using HasModelLoadedFn         = bool(__cdecl *)(int);
        using GetWeaponInfoFn          = CWeaponInfo *(__cdecl *)(unsigned int, unsigned char);

        // Standard skill. Weapon stats vary by how good a player is with the
        // gun, but the MODEL does not, so the answer this asks for is the same
        // whichever is passed - and standard is the one every type defines.
        constexpr unsigned char kWeaponSkillStandard = 0;
    } // namespace

    bool IsModelLoaded(int modelId) {
        return reinterpret_cast<HasModelLoadedFn>(gPatterns.CStreaming__HasModelLoaded)(modelId);
    }

    void RequestModel(int modelId) {
        reinterpret_cast<RequestModelFn>(gPatterns.CStreaming__RequestModel)(modelId, kModelGameRequired | kModelPriorityRequest);
    }

    void LoadAllRequestedModels() {
        // The argument is "only load priority requests". False, because the
        // point of calling this is that everything asked for is needed.
        reinterpret_cast<LoadAllRequestedModelsFn>(gPatterns.CStreaming__LoadAllRequestedModels)(false);
    }

    void LoadModelNow(int modelId) {
        if (modelId <= 0 || IsModelLoaded(modelId)) {
            return;
        }

        RequestModel(modelId);
        LoadAllRequestedModels();
    }

    int WeaponModelId(unsigned int weaponType) {
        auto *info = reinterpret_cast<GetWeaponInfoFn>(gPatterns.CWeaponInfo__GetWeaponInfo)(weaponType, kWeaponSkillStandard);

        // Unarmed has an info record but no model, which is not a failure.
        return info ? info->m_nModelId : 0;
    }

    std::uint8_t CurrentInterior() {
        return static_cast<std::uint8_t>(*reinterpret_cast<const int *>(gPatterns.CGame__CurrArea));
    }

    void SetCurrentInterior(std::uint8_t interior) {
        const int area = static_cast<int>(interior);

        *reinterpret_cast<int *>(gPatterns.CGame__CurrArea) = area;
        reinterpret_cast<void(__cdecl *)(int)>(gPatterns.CStreaming__RemoveBuildingsNotInArea)(area);
    }
} // namespace VantaMP::SDK
