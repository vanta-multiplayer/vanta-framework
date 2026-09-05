#pragma once

#include <cstdint>

namespace VantaMP::SDK {
    // Plain on purpose: plugin-sdk defines RW and drags RenderWare in, so it stays behind this.
    struct LocalPedState {
        bool valid = false;

        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        float heading = 0.0f;

        int modelIndex = 0;
        int pedType    = 0;

        float health    = 0.0f;
        float maxHealth = 0.0f;

        bool inVehicle        = false;
        int vehicleModelIndex = 0;
    };

    LocalPedState QueryLocalPed();

    // --- Remote peds ---------------------------------------------------------
    //
    // Creating a ped needs plugin-sdk's CPlayerPed constructor and a dozen of
    // its fields, so it lives behind this boundary with the rest of the SDK.
    // Everything above the boundary works in slots and plain numbers.

    // Builds a ped for `slot` and puts it in the world.
    //
    // The sequence mirrors what the game does for the local player, in the same
    // order. Every step in it exists because leaving it out produced something
    // specific and unpleasant - see the implementation.
    //
    // Returns the ped, or nullptr if it could not be created.
    void *CreateRemotePed(int slot, int modelIndex, float x, float y, float z);

    // Removes a ped created by CreateRemotePed.
    void DestroyRemotePed(int slot);

    // Moves a ped that already exists. Position and heading only - velocity and
    // animation come from the game processing the ped's input.
    void SetRemotePedTransform(int slot, float x, float y, float z, float heading);

    void SetRemotePedHealth(int slot, float health, float armour);

    // --- Controller state ----------------------------------------------------
    //
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
    void WriteRemoteInput(int slot, const ControllerInput &input);
} // namespace VantaMP::SDK
