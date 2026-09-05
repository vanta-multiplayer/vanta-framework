#include "gameplay.h"

#include "sdk/patterns.h"

#include <utils/safe_win32.h>

#include <logging/logger.h>
#include <utils/hooking/hooking.h>

namespace VantaMP::Game {
    namespace {
        // The strafe fix needs an address to point the game at, so the value has
        // to outlive this function.
        float gFixedDeltaMoveCommand = 10.1f;
    } // namespace

    void ApplyGameplayPatches() {
        const auto &p = SDK::gPatterns;

        // --- Stats -------------------------------------------------------------
        //
        // Health, stamina, muscle, respect, weapon skills. The server decides
        // what a player's stats are; a client that keeps adjusting them locally
        // drifts away from what everyone else can see, silently.

        // The announcements first - "Respect +", "Stamina increased".
        hook::return_function(p.CStats__MessageDisplay1);
        hook::return_function(p.CStats__MessageDisplay2);

        // BOTH DIRECTIONS, AND EASY TO STOP AT ONE. Blocking only the drain
        // leaves stats still CLIMBING as the player runs, swims and shoots, so
        // the values the server set at spawn drift upward instead of downward -
        // no more stable, and just as invisible.
        hook::return_function(p.CStats__Increment);
        hook::nop(p.CStats__Decrement1, 7);
        hook::nop(p.CStats__Decrement2, 7);

        // The Tab panel: Respect, Stamina, Muscle, Fat, Sex Appeal drawn as bars
        // over the game.
        //
        // PATCHED AT THE BRANCH, NOT BY RETTING THE DRAW. The site is an
        // if/else, and the THEN branch also sets a flag saying the stats screen
        // is up. Stubbing the draw stops the bars appearing but leaves that flag
        // set, so the game goes on believing the panel is open - which made the
        // radar vanish for as long as Tab was held. Forcing the jump takes the
        // else branch instead: flag cleared, normal path, exactly as when Tab is
        // not pressed.
        hook::put<std::uint8_t>(p.CHud__VitalStatsBranch, 0xEB);

        // The hunger nag. Same family: a message about a mechanic the server is
        // not running.
        hook::nop(p.CPed__HungerMessage, 5);

        // --- Movement ----------------------------------------------------------

        // Sprinting on any surface.
        //
        //   66 33 C0     xor ax, ax
        //   C2 04 00     ret 4
        //
        // Byte by byte rather than packed into an integer: the 0x66 is an
        // operand-size prefix, and packing this as a little-endian dword
        // reverses the order in a way that is easy to get wrong and produces a
        // different instruction rather than a compile error.
        //
        // RET 4, not RET: the function answers "can you sprint on this surface?"
        // and is stdcall with one argument, so it has to pop it.
        constexpr std::uint8_t kAlwaysSprint[] = {0x66, 0x33, 0xC0, 0xC2, 0x04, 0x00};

        for (std::size_t i = 0; i < sizeof(kAlwaysSprint); ++i) {
            hook::put<std::uint8_t>(p.SurfaceInfos__CantSprintOn + i, kAlwaysSprint[i]);
        }

        // How fast a ped can turn. 15.0f is the game's own value and is visibly
        // sluggish with a mouse; 9.5f is what the previous client settled on.
        hook::put<float>(p.CPedStats__HeadingRate + 4, 9.5f);

        // Throwable accuracy: jz becomes an unconditional jump.
        hook::put<std::uint8_t>(p.Weapon__ThrowableAccuracy, 0x90);
        hook::put<std::uint8_t>(p.Weapon__ThrowableAccuracy + 1, 0xE9);

        // Let a player interrupt a car-jacking rather than being committed to
        // the animation.
        hook::put<std::uint8_t>(p.CTask__CarJackSelect, 0xEB);

        // --- Frame rate ---------------------------------------------------------
        //
        // GTA was written when hardware could not outrun its own frame cap.
        // Uncapped, its frame-time-dependent code starts behaving differently on
        // faster machines - which in multiplayer means two players doing the same
        // thing getting different results.

        // The 14ms sleep in CTimer::Update.
        hook::put<std::uint16_t>(p.CTimer__FrameDelayJump, 0x43EB);
        hook::put<std::uint8_t>(p.CTimer__FrameDelayStackFix, 0x10);
        hook::nop(p.CTimer__FrameDelayPopFix, 1);

        // Weapon strafing scaled with the frame time, so it got faster the
        // better your machine. Pointed at a fixed value instead.
        hook::put<const float *>(p.Weapon__StrafeDelta + 2, &gFixedDeltaMoveCommand);
        hook::nop(p.Weapon__StrafeMultiply, 6);

        // --- Effects and single-player systems -----------------------------------

        // No high-speed blur; it is a camera effect the server has no say over
        // and it obscures other players.
        hook::nop(p.CPostEffects__SpeedBlur, 5);

        // No camera fade on death: the server decides what a death looks like,
        // and a client fading itself out mid-firefight is its own decision.
        hook::nop(p.CCamera__DeathFade, 16);

        // Garages, the idle camera, settings saving, mission logic, trains,
        // SAM sites and the replay system - all single-player, all capable of
        // moving or changing things the server believes it owns.
        hook::nop(p.CGarages__Update, 5);
        hook::return_function(p.CCamera__IdleProcess);
        hook::return_function(p.CMenuManager__SaveSettings);
        hook::return_function(p.CGameLogic__Update);
        hook::return_function(p.CTrain__ForbiddenCross);
        hook::return_function(p.CSamSite__Behaviour);
        hook::return_function(p.CReplay__FinishPlayback);
        hook::return_function(p.CReplay__TriggerPlayback);
        hook::return_function(p.CReplay__Update);
        hook::return_function(p.CReplay__PlayFromHD);

        Framework::Logging::GetLogger("Gameplay")->info("Single-player gameplay disabled; the server owns stats, deaths and world state");
    }
} // namespace VantaMP::Game
