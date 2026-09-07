#pragma once

#include <mafianet/BitStream.h>

#include <cstdint>

namespace VantaMP::Shared::RPC {
    // Script (or spawn) telling the OWNER which world they belong in.
    //
    // Interior can also arrive from the owner's own snapshot — walking
    // through an enex writes GTA's area code and that is what puresync
    // reports. Dimension has no GTA field. The server writes it; the next
    // owner snapshot would send 0 and stomp that write unless the owning
    // client copies the new value onto its replica first. This packet is
    // that copy.
    //
    // flags is Shared::kWorldSpaceInterior and/or kWorldSpaceDimension so
    // a dimension-only set does not yank the owner into interior 0.
    struct WorldSpace {
        static constexpr const char *kIdentifier = "VantaMP::WorldSpace";

        std::uint64_t networkId = 0;
        std::uint8_t  interior  = 0;
        std::uint16_t dimension = 0;
        std::uint8_t  flags     = 0;

        void FromParameters(std::uint64_t id, std::uint8_t area, std::uint16_t world, std::uint8_t which) {
            networkId = id;
            interior  = area;
            dimension = world;
            flags     = which;
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, networkId);
            bs->Serialize(write, interior);
            bs->Serialize(write, dimension);
            bs->Serialize(write, flags);
        }
    };
} // namespace VantaMP::Shared::RPC
