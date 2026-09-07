#include "input.h"

#include "sdk/ped/access.h"

#include <algorithm>

namespace VantaMP::SDK {
    namespace {
        using Internal::GetPadFn;
    } // namespace

    ControllerInput ReadLocalInput() {
        ControllerInput input;

        const auto pad = reinterpret_cast<CPad *>(reinterpret_cast<GetPadFn>(gPatterns.CPad__GetPad)(0));

        if (!pad) {
            return input;
        }

        const auto &state = pad->NewState;

        input.leftStickX = state.LeftStickX;
        input.leftStickY = state.LeftStickY;

        // ANALOGUE AND INDEPENDENT, NOT TWO BITS OF THE MASK. In a car these
        // are accelerate and brake, both can be held at once - that is a drift -
        // and they carry pressure, which a bit cannot. Without this every remote
        // car is flat out or coasting.
        input.accelerator = static_cast<std::uint8_t>(std::clamp<int>(state.ButtonCross, 0, 255));
        input.brake       = static_cast<std::uint8_t>(std::clamp<int>(state.ButtonSquare, 0, 255));

        const auto held = [](std::int16_t value) { return value != 0; };

        std::uint16_t buttons = 0;

        // SAMPLED FOR THE CONTEXT THE PLAYER IS IN. GTA reuses its buttons:
        // cross is sprint on foot and accelerate at the wheel, square is jump
        // and reverse. Reading them by their on-foot names while driving puts
        // "sprinting, jumping" on the wire for someone sitting still.
        //
        // The pressure values that DO mean something in a car are already sent
        // above as their own fields, so nothing is lost by leaving these clear.
        const auto *ped   = Internal::LocalPed();
        const bool seated = ped && ped->bInVehicle;

        if (held(state.ButtonCircle)) {
            buttons |= Button::Fire;
        }

        if (held(state.RightShoulder1)) {
            buttons |= Button::Aim;
        }

        if (!seated) {
            if (held(state.ButtonSquare)) {
                buttons |= Button::Jump;
            }
            if (held(state.ButtonCross)) {
                buttons |= Button::Sprint;
            }
            if (held(state.ButtonTriangle)) {
                buttons |= Button::EnterExit;
            }
            if (held(state.ShockButtonL)) {
                buttons |= Button::Crouch;
            }
            if (held(state.m_bPedWalk)) {
                buttons |= Button::Walk;
            }
        }

        input.buttons = buttons;
        return input;
    }

    void WriteRemoteInput(int slot, const ControllerInput &input) {
        auto *pad = reinterpret_cast<CPad *>(Game::PadAt(slot));

        if (!pad) {
            return;
        }

        // LAST FRAME'S STATE BECOMES OldState, AND THIS ORDER IS THE WHOLE POINT.
        //
        // GTA does not act on a button being down. It acts on a button being
        // down NOW and up LAST frame - CPad::JumpJustDown and its siblings are
        // all NewState && !OldState. Everything discrete a player does comes
        // through one of them: jumping, toggling a crouch, entering a car.
        //
        // So OldState has to hold what this ped was given on the PREVIOUS frame.
        // Assigning it from the state just built - which this did - makes the two
        // identical forever, and the game never sees a single edge: a remote
        // player could not jump, could not crouch, could not open a car door,
        // and there is nothing to see wrong because the buttons all arrive
        // correctly and are all faithfully ignored.
        //
        // Sampling at 60fps against input arriving at 10Hz is not a problem: the
        // first frame of a held button is an edge and the rest are not, which is
        // exactly one jump per press.
        auto &state = pad->NewState;

        pad->OldState = state;

        // CLEARED NEXT. Anything not set below has to read as released, or the
        // remote player inherits whatever this client happens to be holding.
        state = CControllerState {};

        state.LeftStickX = input.leftStickX;
        state.LeftStickY = input.leftStickY;

        state.ButtonCross  = static_cast<std::int16_t>(input.accelerator);
        state.ButtonSquare = static_cast<std::int16_t>(input.brake);

        const auto set = [](bool pressed) -> std::int16_t { return pressed ? 255 : 0; };

        state.ButtonCircle   = set((input.buttons & Button::Fire) != 0);
        state.RightShoulder1 = set((input.buttons & Button::Aim) != 0);
        state.ButtonTriangle = set((input.buttons & Button::EnterExit) != 0);
        state.m_bPedWalk     = set((input.buttons & Button::Walk) != 0);

        const auto *ped    = Internal::PedAt(slot);
        const bool  seated = ped && ped->bInVehicle;

        // CROUCH IS A TOGGLE ON FOOT AND A HORN IN A CAR.
        //
        // On foot the duck is already synced as a task from isDucked, so
        // replaying the button flickers the pose. In a vehicle the same
        // physical key is ShockButtonL - horn / siren - and MTA writes it
        // there. Without this a remote driver can never honk.
        if (seated) {
            state.ShockButtonL = set((input.buttons & Button::Crouch) != 0);
            return;
        }

        // Jump and sprint share their buttons with the pedals, which were
        // already written above from their own analogue fields. Setting them
        // again here would overwrite a car's throttle with an on-foot bit.
        if ((input.buttons & Button::Jump) != 0) {
            state.ButtonSquare = 255;
        }

        if ((input.buttons & Button::Sprint) != 0) {
            state.ButtonCross = 255;
        }
    }

    void ClearLocalInput(bool enablePlayerControls) {
        // CALLED THROUGH THE GAME'S OWN FUNCTIONS rather than by zeroing the
        // structures. The pad keeps button history and press timings beside the
        // current state, and a memset would leave those inconsistent - a key
        // released this way would read as still held.
        using ClearFn = void(__thiscall *)(void *, bool, bool);

        if (auto *pad = reinterpret_cast<GetPadFn>(gPatterns.CPad__GetPad)(0)) {
            reinterpret_cast<ClearFn>(gPatterns.CPad__Clear)(pad, enablePlayerControls, true);
        }

        reinterpret_cast<void (*)()>(gPatterns.CPad__ClearMouseHistory)();

        // THE MOUSE DELTAS, SEPARATELY. CPad::Clear does not touch these and the
        // camera reads them directly, so without this the player can still steer
        // their view while suppressed.
        //
        // New and old sit next to each other; both are cleared so nothing is
        // reconstructed from the difference.
        auto *mouse = reinterpret_cast<CMouseControllerState *>(gPatterns.CPad__NewMouseState);

        mouse[0].x = 0.0f;
        mouse[0].y = 0.0f;
        mouse[1].x = 0.0f;
        mouse[1].y = 0.0f;
    }
} // namespace VantaMP::SDK
