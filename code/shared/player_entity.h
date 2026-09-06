#pragma once

#include <networking/replication/network_entity.h>

#include <cstdint>
#include <string>

namespace VantaMP::Shared {
    // A player, as everyone else sees them.
    //
    // Shared between client and server on purpose: both sides serialize the
    // same fields in the same order, and the only way to keep that true is for
    // there to be one definition. A second copy on the client drifts the moment
    // either side gains a field.
    //
    // Authority follows ownerGUID, which the framework handles: the owning
    // client sends its own state upstream, the server relays it to everyone
    // else, and state arriving from anyone but the owner is refused.
    //
    // POSITION, VELOCITY AND ROTATION ARE NOT DECLARED HERE. They live on
    // NetworkEntity and are carried by its transform tier, which goes out on the
    // unreliable channel every tick and is memcmp-deduped - so a player standing
    // still stops sending. Re-declaring them here would put the same numbers on
    // the wire twice and give the two copies a chance to disagree.
    class PlayerEntity final: public Framework::Networking::Replication::NetworkEntity {
      public:
        // The name the registry knows this type by. Its CRC32 becomes the type
        // id on the wire, so it has to match on both sides exactly.
        static constexpr const char *kTypeName = "VantaMPPlayer";

        // --- Spawn state, sent once when the entity is constructed -------------

        std::string nickname;

        // The GTA model this player wears. 0 means "the default", which is what
        // a player who has not chosen looks like.
        std::uint32_t modelId = 0;

        // --- Per-tick state ----------------------------------------------------

        float health = 100.0f;
        float armour = 0.0f;

        // The controller state the owning client is acting on, relayed so that
        // everyone else's copy of this player moves the same way.
        //
        // SENT AS INPUT, NOT AS ANIMATION. The alternative is to replicate which
        // animation is playing, which means every client agreeing about state it
        // did not compute. Input is smaller, and it is what the game's own
        // movement code consumes - so a remote player walks because GTA walked
        // them, not because we described walking.
        std::int16_t leftStickX = 0;
        std::int16_t leftStickY = 0;

        // ANALOGUE, AND SEPARATE FROM THE MASK. In a car these are accelerate
        // and brake: both can be held at once - that is a drift - and both carry
        // pressure. A bit can express neither, so every remote car would be flat
        // out or coasting.
        std::uint8_t accelerator = 0;
        std::uint8_t brake       = 0;

        std::uint16_t buttons = 0;

        // WHICH WAY THIS PLAYER IS LOOKING, WHICH IS NOT WHICH WAY THEY FACE.
        //
        // GTA resolves stick input against the camera: pushing forward means
        // "away from the camera", not "along the ped's heading". Process a
        // remote ped with the local camera rotation and their movement is
        // rotated by the difference - they walk sideways, and change direction
        // whenever the local player turns their own view.
        //
        // It also aims the upper body, so without it a remote player shoots
        // wherever the local player happens to be looking.
        //
        // Radians. Sent every tick with the input it has to be interpreted
        // against: arriving a tick late it would describe a different frame.
        float cameraRotation = 0.0f;

        // WHERE THE GUN IS POINTING, captured at CPedIK::PointGunInDirection.
        //
        // cameraRotation above is what the stick is resolved against. These two
        // are what the hands follow. They are not the same: the camera is
        // snapped onto the wire at 10 Hz, and if the IK reads it directly the
        // remote player's arms step around in tenths. MTA sends both arm
        // directions and lerps them for that reason.
        //
        // aimVertical existed once before with neither end wired up, and was
        // removed for it. Both ends exist now.
        float aimHorizontal = 0.0f;
        float aimVertical   = 0.0f;

        // POSE, SENT AS STATE RATHER THAN DERIVED FROM THE INPUT.
        //
        // The crouch button is already in the mask, so replicating "is ducked"
        // as well looks redundant - but the two do not agree. GTA enters and
        // leaves the ducked state through animations that take time and can be
        // refused mid-transition, so a ped told to crouch is not crouched yet,
        // and one that was crouching may still be standing up. MTA sends the
        // flag for exactly this reason.
        //
        // In the air and in water matter for the same reason: the animation the
        // game picks depends on them, and the receiving client cannot infer
        // either from a stick position.
        bool isDucked  = false;
        bool isInAir   = false;
        bool isInWater = false;

        // WHAT IS IN THIS PLAYER'S HANDS.
        //
        // The type alone is what makes a weapon visible, since a held weapon is
        // a model the receiving client attaches from it. Without this every
        // remote player is empty-handed however they are standing, and aiming
        // and firing animate against nothing.
        //
        // The ammo counts are not needed to draw it and are sent anyway,
        // because they are what a HUD, a reload animation and any future hit
        // validation all read, and because they cost four bytes against a
        // weapon switch that already costs a model load. MTA sends the same
        // three, plus the slot - which is omitted here because the receiver
        // gets it back from GiveWeapon rather than having to be told.
        std::uint32_t weaponType = 0;
        std::uint32_t ammoClip   = 0;
        std::uint32_t ammoTotal  = 0;

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override;
        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override;
    };
} // namespace VantaMP::Shared
