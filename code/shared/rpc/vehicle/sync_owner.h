#pragma once

#include <mafianet/BitStream.h>

#include <cstdint>

namespace VantaMP::Shared::RPC {
    // Server -> client: you are now the syncer for this unoccupied vehicle
    // (start), or you are not any more (stop). MTA's
    // UNOCCUPIED_VEHICLE_STARTSYNC / STOPSYNC.
    //
    // RELIABLE ORDERED, AND THE ORDERING IS THE POINT. A stop and a start
    // that arrive out of order leave either two clients believing they own
    // the same physical car - they fight over its position, and it jitters
    // between two simulations - or none, and it freezes. Ordering on the
    // Events channel is what makes the handoff a clean transfer rather than
    // a race.
    //
    // ONE PACKET, NOT TWO. Start and stop carry identical fields and must be
    // ordered against each other; splitting them into separate identifiers
    // would pay the Huffman identifier cost twice and gain nothing. This is
    // the §23 judgement call: separate packets need a real difference in
    // reliability, cadence, authority or lifetime, and these two have none.
    struct VehicleSyncOwner {
        static constexpr const char *kIdentifier = "VantaMP::VehicleSyncOwner";

        // WHICH KIND OF ENTITY THIS NAMES.
        //
        // The receiver cannot work this out for itself: these announcements
        // are sent from OnPlayerReady, BEFORE the replicas exist on the
        // joining client, so resolving the id against the replication manager
        // returns nullptr and every player announcement was being filed in the
        // vehicle registry. The sender always knows, so it says.
        static constexpr std::uint8_t kVehicle = 0;
        static constexpr std::uint8_t kPlayer  = 1;

        std::uint8_t kind = kVehicle;

        std::uint64_t vehicleId = 0;

        // THE COMPACT ID IS MINTED BY THE SERVER AND LEARNED HERE.
        //
        // Clients never invent one. This packet (and spawn/resync) is how a
        // client finds out which 16-bit id addresses this vehicle on the hot
        // path; until it has one, motion for that vehicle rides plain
        // replication. Carrying it here rather than in its own packet is
        // free - this message already had to be sent and already names the
        // vehicle.
        std::uint16_t wireId = 0xFFFF;

        // False means stop syncing. The client must drop its syncer role
        // immediately: a delayed stop is the double-owner bug.
        bool start = false;

        // Where the new syncer should start from, so it does not begin
        // simulating from a stale local pose. Only meaningful when start is
        // true; a resync follows for anything more than this.
        std::uint16_t sequence = 0;

        void FromParameters(std::uint64_t id, bool begin, std::uint16_t seq = 0, std::uint16_t compactId = 0xFFFF,
                            std::uint8_t entityKind = kVehicle) {
            kind      = entityKind;
            vehicleId = id;
            start     = begin;
            sequence  = seq;
            wireId    = compactId;
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, kind);
            bs->Serialize(write, vehicleId);
            bs->Serialize(write, wireId);
            bs->Serialize(write, start);
            bs->Serialize(write, sequence);
        }
    };
} // namespace VantaMP::Shared::RPC
