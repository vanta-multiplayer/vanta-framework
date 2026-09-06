#include "camera.h"

#include "game/entities/player_slots.h"
#include "patterns.h"

#include <array>
#include <cstddef>

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
        std::array<float, static_cast<std::size_t>(Game::kMaxPlayers)> gRemoteCameraRotation {};
    } // namespace

    float ReadLocalCameraRotation() {
        return *reinterpret_cast<const float *>(gPatterns.CCamera__Rotation);
    }

    void SetRemoteCameraRotation(int slot, float rotation) {
        if (slot < 0 || slot >= Game::kMaxPlayers) {
            return;
        }

        gRemoteCameraRotation[static_cast<std::size_t>(slot)] = rotation;
    }

    float GetRemoteCameraRotation(int slot) {
        if (slot < 0 || slot >= Game::kMaxPlayers) {
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
} // namespace VantaMP::SDK
