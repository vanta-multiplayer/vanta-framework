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
        std::uint32_t buttons   = 0;

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override;
        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override;
    };
} // namespace VantaMP::Shared
