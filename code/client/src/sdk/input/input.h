#pragma once

#include <cstdint>

namespace VantaMP::SDK {
    // What a player is holding, reduced to what has to cross the wire.
    //
    // THE STICKS AND THE PEDALS ARE ANALOGUE, THE REST ARE NOT. Accelerator and
    // brake carry pressure and can both be held at once - that is a drift, and
    // a bitmask can express neither fact. Everything else is on or off.
    struct ControllerInput {
        std::int16_t leftStickX = 0;
        std::int16_t leftStickY = 0;

        std::uint8_t accelerator = 0;
        std::uint8_t brake       = 0;

        std::uint16_t buttons = 0;
    };

    // The buttons the mask carries.
    //
    // GTA stores each as a signed short, 255 when pressed, which is 44 bytes
    // for what fits in two.
    namespace Button {
        inline constexpr std::uint16_t Fire      = 1u << 0;
        inline constexpr std::uint16_t Aim       = 1u << 1;
        inline constexpr std::uint16_t Jump      = 1u << 2;
        inline constexpr std::uint16_t Sprint    = 1u << 3;
        inline constexpr std::uint16_t EnterExit = 1u << 4;
        inline constexpr std::uint16_t Crouch    = 1u << 5;
        inline constexpr std::uint16_t Walk      = 1u << 6;
    } // namespace Button

    // Reads what the local player is holding right now.
    //
    // Sampled for the context the player is actually in: GTA reuses its
    // buttons, so cross is sprint on foot and accelerate at the wheel. Reading
    // them by their on-foot names while driving puts "sprinting, jumping" on
    // the wire for someone sitting still, and the receiver plays exactly that.
    [[nodiscard]] ControllerInput ReadLocalInput();

    // Writes input into a slot's pad, for the game to act on when it next
    // processes that ped.
    //
    // Call this every frame, not only when the input changes: the pad is
    // consumed each time the ped is processed. It maintains the pad's previous
    // state itself, which is what lets the game see press edges - see the
    // implementation, because getting that wrong is silent and total.
    void WriteRemoteInput(int slot, const ControllerInput &input);

    // Wipes the local pad, so the game acts on nothing this frame.
    //
    // enablePlayerControls says what state to leave the pad in: false while
    // input is still being suppressed, true on the frame control returns.
    //
    // Clears the mouse deltas too. The camera reads those directly rather than
    // through the pad's button state, so clearing the pad alone still lets a
    // player steer their view.
    void ClearLocalInput(bool enablePlayerControls);
} // namespace VantaMP::SDK
