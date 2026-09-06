#pragma once

#include <mafianet/types.h>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace VantaMP::Shared {
    class PlayerEntity;
}

namespace VantaMP::Game {
    // Who is on the server, and where.
    //
    // Kept apart from the server instance on purpose: Server owns the lifecycle
    // the framework calls into - init, tick, shutdown - and everything about
    // what a player IS belongs here. A server class that also knows how to
    // spawn, move and despawn people grows into the file every feature has to
    // be added to.
    class PlayerManager final {
      public:
        // A connection arrived. Creates the entity that represents them and
        // registers it as the connection's viewer.
        //
        // THE AVATAR IS ALSO THE VIEWER: its position and stream range decide
        // what this connection is told about, so a player with no entity sees an
        // empty world, and one whose entity is not the registered viewer sees
        // everything, everywhere, at once.
        //
        // Creating them does NOT put them in the world - see Spawn.
        bool Add(MafiaNet::PeerGuid guid, const std::string &nickname);

        void Remove(MafiaNet::PeerGuid guid);

        // Places a player in the world, or moves one already there - which is
        // what a respawn is, so death needs no separate resurrect.
        //
        // THE SERVER DECIDES WHETHER A PLAYER EXISTS IN THE WORLD. Until this,
        // a connected player has an entity but no place: they may be in a menu,
        // spectating, or waiting on a resource. Only the server knows which.
        //
        // It also writes the server's own copy of the position first. The
        // interest grid measures from that copy, so a player the server has not
        // positioned is invisible to everyone and sees nobody - which looks
        // like a streaming bug rather than a missing spawn.
        bool Spawn(MafiaNet::PeerGuid guid, float x, float y, float z, float heading = 0.0f, std::uint32_t modelId = 0, float health = 100.0f, float armour = 0.0f);

        [[nodiscard]] Shared::PlayerEntity *Find(MafiaNet::PeerGuid guid) const;

        template <typename Fn>
        void ForEach(Fn &&fn) const {
            for (const auto &[guid, player] : _players) {
                if (player) {
                    fn(player);
                }
            }
        }

        [[nodiscard]] std::size_t Count() const {
            return _players.size();
        }

      private:
        // The replication manager owns the entities; this only remembers which
        // belongs to which connection, so a disconnect knows what to destroy.
        std::unordered_map<MafiaNet::PeerGuid, Shared::PlayerEntity *> _players;
    };
} // namespace VantaMP::Game
