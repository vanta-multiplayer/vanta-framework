#include "camera.h"

#include "sdk/player/slots.h"
#include "sdk/patterns.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// No plugin-sdk here, deliberately: a camera rotation is a float at a known
// address and a slot is an integer, so none of this needs the game's types.
// Every file that does need them is listed in CMakeLists; this one stays off
// that list.

namespace VantaMP::SDK {
    namespace {
        // One camera rotation per player slot.
        //
        // The game has a SINGLE camera rotation global, because it was built for
        // one player. Every remote ped still has to be processed under its own
        // owner's value, so each is kept here and swapped into that global for
        // the duration of that ped's processing - the same approach the pad
        // takes, and for the same reason.
        std::array<float, static_cast<std::size_t>(SDK::kMaxPlayers)> gRemoteCameraRotation {};
    } // namespace

    float ReadLocalCameraRotation() {
        return *reinterpret_cast<const float *>(gPatterns.CCamera__Rotation);
    }

    void SetRemoteCameraRotation(int slot, float rotation) {
        if (slot < 0 || slot >= SDK::kMaxPlayers) {
            return;
        }

        gRemoteCameraRotation[static_cast<std::size_t>(slot)] = rotation;
    }

    float GetRemoteCameraRotation(int slot) {
        if (slot < 0 || slot >= SDK::kMaxPlayers) {
            return 0.0f;
        }

        return gRemoteCameraRotation[static_cast<std::size_t>(slot)];
    }

    ScreenPoint WorldToScreen(float x, float y, float z) {
        ScreenPoint screen;

        // Three floats in, three out. RwV3d is that layout, and naming it
        // would pull RenderWare into a file that does not have it.
        float world[3] = {x, y, z};
        float out[3]   = {};
        float width    = 0.0f;
        float height   = 0.0f;

        using CalcScreenCoorsFn = bool(__cdecl *)(const float *, float *, float *, float *, bool, bool);

        if (!reinterpret_cast<CalcScreenCoorsFn>(gPatterns.CSprite__CalcScreenCoors)(world, out, &width, &height, true, false)) {
            return screen;
        }

        // z <= 0 is behind the near plane. The game still writes an x/y for
        // those, and drawing there puts a nametag on the wrong side of the
        // screen.
        if (out[2] <= 0.1f) {
            return screen;
        }

        screen.visible = true;
        screen.x       = out[0];
        screen.y       = out[1];
        screen.z       = out[2];

        return screen;
    }

    ListenerCamera ReadListenerCamera() {
        ListenerCamera listener;

        const auto theCamera = gPatterns.TheCamera;
        if (!theCamera) {
            return listener;
        }

        // CCamera : CPlaceable. Offsets are from plugin-sdk's CCamera / CCam
        // layout for 1.0 US, not from including those headers here.
        constexpr std::ptrdiff_t kActiveCam = 0x59;
        constexpr std::ptrdiff_t kCams      = 0x174;
        constexpr std::size_t kCamSize      = 0x238;
        constexpr std::ptrdiff_t kFront     = 0x190;
        constexpr std::ptrdiff_t kSource    = 0x19C;
        constexpr std::ptrdiff_t kUp        = 0x1B4;

        const auto *base     = reinterpret_cast<const std::uint8_t *>(theCamera);
        const unsigned index = base[kActiveCam];

        if (index > 2) {
            return listener;
        }

        const auto *cam    = base + kCams + index * kCamSize;
        const auto *front  = reinterpret_cast<const float *>(cam + kFront);
        const auto *source = reinterpret_cast<const float *>(cam + kSource);
        const auto *up     = reinterpret_cast<const float *>(cam + kUp);

        const float frontLen = std::sqrt(front[0] * front[0] + front[1] * front[1] + front[2] * front[2]);
        if (frontLen < 0.001f) {
            return listener;
        }

        float upLen = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
        if (upLen < 0.001f) {
            listener.upX = 0.0f;
            listener.upY = 0.0f;
            listener.upZ = 1.0f;
        }
        else {
            listener.upX = up[0] / upLen;
            listener.upY = up[1] / upLen;
            listener.upZ = up[2] / upLen;
        }

        listener.valid    = true;
        listener.x        = source[0];
        listener.y        = source[1];
        listener.z        = source[2];
        listener.forwardX = front[0] / frontLen;
        listener.forwardY = front[1] / frontLen;
        listener.forwardZ = front[2] / frontLen;
        return listener;
    }
} // namespace VantaMP::SDK
