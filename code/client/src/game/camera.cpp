#include "camera.h"

#include "sdk/patterns.h"

#include <utils/safe_win32.h>

#include <logging/logger.h>

namespace VantaMP::Game {
    namespace {
        // The direction flag CCamera::Fade takes.
        //
        // THE SENSE IS THE OPPOSITE OF WHAT THE CALL SITES SUGGEST, and it was
        // got wrong once by assuming 1 meant "fade out". Read out of
        // CCamera::ProcessFade, which is what actually drives the alpha:
        //
        //   cmp  ax, 1           ; the flag
        //   jne  <below>         ; NOT 1 -> this branch
        //   ...
        //   mov  edx, 0x437F0000 ; 255.0f, fully opaque
        //
        // The opaque end is loaded where the flag is NOT 1, so 0 fades TO the
        // fade colour and 1 fades back to the scene.
        constexpr short kFadeOut = 0;
        constexpr short kFadeIn  = 1;

        using FadeFn          = void(__thiscall *)(void *, float, short);
        using SetFadeColourFn = void(__thiscall *)(void *, std::uint8_t, std::uint8_t, std::uint8_t);

        void *Camera() {
            return reinterpret_cast<void *>(SDK::gPatterns.TheCamera);
        }

        void Fade(float durationSeconds, short direction) {
            reinterpret_cast<FadeFn>(SDK::gPatterns.CCamera__Fade)(Camera(), durationSeconds, direction);
        }
    } // namespace

    void FadeIn(float durationSeconds) {
        // No colour set here: it is whatever the last fade out chose, and
        // resetting it to black would be indistinguishable from a fade that
        // never ran on the frames where alpha is still high.
        Fade(durationSeconds, kFadeIn);
    }

    void FadeOut(float durationSeconds) {
        reinterpret_cast<SetFadeColourFn>(SDK::gPatterns.CCamera__SetFadeColour)(Camera(), 0, 0, 0);
        Fade(durationSeconds, kFadeOut);
    }
} // namespace VantaMP::Game
