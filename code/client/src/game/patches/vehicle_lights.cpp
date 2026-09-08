#include "game/patches/vehicle_lights.h"

#include "sdk/patterns.h"
#include "shared/vehicle/colors.h"

#include <utils/safe_win32.h>

#include <utils/hooking/hooking.h>

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <vector>

namespace VantaMP::SDK {
    namespace {
        std::unordered_map<const void *, std::uint32_t> gVehicleHeadlight;

        const void *gLightsVehicle        = nullptr;
        const void *gHeadLightBeamVehicle = nullptr;

        unsigned long gHeadLightR = 255;
        unsigned long gHeadLightG = 255;
        unsigned long gHeadLightB = 255;

        std::uint32_t  gRegisterCorona  = 0;
        std::uint32_t  gStoreShadow     = 0;
        std::uint8_t  *gNightVisFlag    = nullptr;
        std::uint32_t *gIm3DRenderCount = nullptr;

        struct RwV3dColour {
            float         x, y, z;
            unsigned long color;
            float         u, v;
        };

        void ScaleHeadLight(const void *vehicle, float baseR, float baseG, float baseB) {
            unsigned char r = 255, g = 255, b = 255;

            if (vehicle) {
                const auto found = gVehicleHeadlight.find(vehicle);

                if (found != gVehicleHeadlight.end() && found->second != 0) {
                    r = Shared::RgbR(found->second);
                    g = Shared::RgbG(found->second);
                    b = Shared::RgbB(found->second);
                }
            }

            gHeadLightR = static_cast<unsigned long>((std::min)(255.0f, r * (1.0f / 255.0f) * baseR));
            gHeadLightG = static_cast<unsigned long>((std::min)(255.0f, g * (1.0f / 255.0f) * baseG));
            gHeadLightB = static_cast<unsigned long>((std::min)(255.0f, b * (1.0f / 255.0f) * baseB));
        }

        enum class Kind {
            VehicleLights,
            Beam,
            BeamVerts,
            Corona,
            ShadowTwin,
            ShadowSingle,
        };

        // Runtime-address inject_hook. DEFINE_INJECT_HOOK needs a compile-time
        // constant; patterns resolve later. Bodies are members so Ecx/Esp/JumpTo
        // (protected) are reachable.
        struct LightsHook: hook::inject_hook {
            Kind          kind = Kind::Beam;
            std::uint32_t cont = 0;

            LightsHook(std::uint32_t address, std::uint32_t continueAt, Kind k): hook::inject_hook(address), kind(k), cont(continueAt) {}

            void PatchStackRgb(std::uint32_t offset) {
                auto *stack = reinterpret_cast<unsigned long *>(Esp());

                stack[offset / 4u]     = gHeadLightR;
                stack[offset / 4u + 1] = gHeadLightG;
                stack[offset / 4u + 2] = gHeadLightB;
            }

            ReturnType run() override {
                switch (kind) {
                case Kind::VehicleLights:
                    gLightsVehicle = reinterpret_cast<const void *>(Ecx());

                    if (gNightVisFlag) {
                        Eax((Eax() & 0xFFFFFF00u) | *gNightVisFlag);
                    }

                    Esp(Esp() - 0x3C);
                    return JumpTo(cont);

                case Kind::Beam:
                    gHeadLightBeamVehicle = reinterpret_cast<const void *>(Ecx());
                    Esp(Esp() - 0x94);
                    return JumpTo(cont);

                case Kind::BeamVerts: {
                    auto *verts      = *reinterpret_cast<RwV3dColour **>(Esp());
                    const auto count = *reinterpret_cast<unsigned int *>(Esp() + 4);

                    ScaleHeadLight(gHeadLightBeamVehicle, 255.0f, 255.0f, 255.0f);

                    if (verts) {
                        for (unsigned int i = 0; i < count; ++i) {
                            const auto alpha = (verts[i].color >> 24) & 0xFFu;
                            verts[i].color   = (alpha << 24) | (gHeadLightR << 16) | (gHeadLightG << 8) | gHeadLightB;
                        }
                    }

                    if (gIm3DRenderCount) {
                        *gIm3DRenderCount = 5;
                    }

                    return JumpTo(cont);
                }

                case Kind::Corona:
                    ScaleHeadLight(gLightsVehicle, 160.0f, 160.0f, 140.0f);
                    PatchStackRgb(8);
                    Esp(Esp() - 4);
                    *reinterpret_cast<std::uint32_t *>(Esp()) = cont;
                    return JumpTo(gRegisterCorona);

                case Kind::ShadowTwin:
                    ScaleHeadLight(gLightsVehicle, 45.0f, 45.0f, 45.0f);
                    PatchStackRgb(32);
                    Esp(Esp() - 4);
                    *reinterpret_cast<std::uint32_t *>(Esp()) = cont;
                    return JumpTo(gStoreShadow);

                case Kind::ShadowSingle:
                    ScaleHeadLight(gLightsVehicle, 45.0f, 45.0f, 45.0f);
                    PatchStackRgb(32);
                    Esp(Esp() - 4);
                    *reinterpret_cast<std::uint32_t *>(Esp()) = cont;
                    return JumpTo(gStoreShadow);
                }

                return DoNowt();
            }
        };

        std::vector<std::unique_ptr<LightsHook>> gHooks;
        bool                                     gInstalled = false;

        void Keep(std::unique_ptr<LightsHook> hook) {
            hook->inject();
            gHooks.push_back(std::move(hook));
        }
    } // namespace

    void SetVehicleHeadlightColor(void *handle, std::uint32_t packedRgb) {
        if (!handle) {
            return;
        }

        if (packedRgb == 0) {
            gVehicleHeadlight.erase(handle);
            return;
        }

        gVehicleHeadlight[handle] = packedRgb;
    }

    void ClearVehicleHeadlightColor(void *handle) {
        if (handle) {
            gVehicleHeadlight.erase(handle);
        }
    }

    void InstallVehicleHeadlightHooks() {
        if (gInstalled || !gPatterns.CVehicle__DoHeadLightBeam) {
            return;
        }

        gRegisterCorona = static_cast<std::uint32_t>(gPatterns.CCoronas__RegisterCorona);
        gStoreShadow    = static_cast<std::uint32_t>(gPatterns.CShadows__StoreCarLightShadow);

        // A0 <imm32> — night-vis flag load at DoVehicleLights.
        gNightVisFlag = reinterpret_cast<std::uint8_t *>(*reinterpret_cast<std::uint32_t *>(gPatterns.CVehicle__DoVehicleLights + 1));

        // C7 05 <imm32> 05 00 00 00 — Im3D vertex count store in the beam path.
        gIm3DRenderCount = reinterpret_cast<std::uint32_t *>(*reinterpret_cast<std::uint32_t *>(gPatterns.CVehicle__DoHeadLightBeam_Verts + 2));

        const auto lights  = static_cast<std::uint32_t>(gPatterns.CVehicle__DoVehicleLights);
        const auto beam    = static_cast<std::uint32_t>(gPatterns.CVehicle__DoHeadLightBeam);
        const auto verts   = static_cast<std::uint32_t>(gPatterns.CVehicle__DoHeadLightBeam_Verts);
        const auto corona1 = static_cast<std::uint32_t>(gPatterns.CVehicle__DoHeadLightEffect_Corona1);
        const auto corona2 = static_cast<std::uint32_t>(gPatterns.CVehicle__DoHeadLightEffect_Corona2);
        const auto shadowT = static_cast<std::uint32_t>(gPatterns.CVehicle__DoHeadLightReflectionTwin_Shadow);
        const auto shadowS = static_cast<std::uint32_t>(gPatterns.CVehicle__DoHeadLightReflectionSingle_Shadow);

        Keep(std::make_unique<LightsHook>(lights, lights + 8, Kind::VehicleLights));
        Keep(std::make_unique<LightsHook>(beam, beam + 6, Kind::Beam));
        Keep(std::make_unique<LightsHook>(verts, verts + 10, Kind::BeamVerts));

        // Only the CALL (5 bytes). cont is the following `add esp, …`.
        Keep(std::make_unique<LightsHook>(corona1, corona1 + 5, Kind::Corona));
        Keep(std::make_unique<LightsHook>(corona2, corona2 + 5, Kind::Corona));
        Keep(std::make_unique<LightsHook>(shadowT, shadowT + 5, Kind::ShadowTwin));
        Keep(std::make_unique<LightsHook>(shadowS, shadowS + 5, Kind::ShadowSingle));

        gInstalled = true;
    }
} // namespace VantaMP::SDK
