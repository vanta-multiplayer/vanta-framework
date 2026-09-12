#pragma once

#include <cstdint>

namespace VantaMP::Shared::RPC {
    struct PlayerOnFootSync;
    struct PlayerAimSync;
    struct WorldSpace;
    struct PlayerScript;
}

namespace VantaMP::Game {
    struct LocalWorldSpace {
        std::uint8_t  interior  = 0;
        std::uint16_t dimension = 0;
    };
    // Keeps the game and the network in step, in both directions.
    //
    // Outbound: the local player's position, heading, health and input are
    // copied into its replicated entity each tick. The framework does the rest -
    // an owned entity serializes upstream on its own.
    //
    // Inbound: every entity that is not ours gets a GTA ped, and that ped is
    // moved to wherever the entity says it is. The ped's animation and physics
    // are not sent: the game computes them from the input that is, which is why
    // a remote player walks rather than sliding.
    //
    // Called once per frame from the game's own tick, so everything here runs
    // on the game thread and can touch the world directly.
    void UpdatePlayerSync();

    // Registers the per-ped correction below. Called once, at startup.
    void InstallRemotePlayerCorrection();

    // Moves one remote ped a step towards where its owner says it is.
    //
    // RUN PER PED, IMMEDIATELY AFTER THE GAME HAS PROCESSED THAT PED - not once
    // per frame ahead of them all. State arrives ten times a second and the game
    // draws sixty, so a correction every frame is what turns six-frame-apart
    // updates into movement. But it adds a share of the error to where the ped
    // currently is, so it has to see the position the walk produced: run before
    // the game moves peds, it overwrites that walk with a value derived from a
    // stale read, and the ped slides instead of walking.
    void CorrectRemotePlayer(int slot);

    // Drops every remote ped, without touching the entities.
    //
    // For a disconnect: the peds belong to this client and have to go, while the
    // entities are the network's to clean up.
    void ClearRemotePlayers();

    // Which slot a remote player's ped occupies, or -1 if they have none yet.
    [[nodiscard]] int SlotOfNetworkId(std::uint64_t networkId);

    // The entity id sitting in that slot, or 0 if the slot is empty.
    [[nodiscard]] std::uint64_t NetworkIdOfSlot(int slot);

    // This client's own replicated entity, or 0 before the first sync tick.
    [[nodiscard]] std::uint64_t LocalNetworkId();

    // Interior from the local ped, dimension from the owned replica.
    [[nodiscard]] LocalWorldSpace QueryLocalWorldSpace();

    // Spawn / script: apply GTA area and copy the server's dimension onto
    // the owned replica so the next snapshot does not send 0.
    void ApplyOwnedWorldSpace(std::uint8_t interior, std::uint16_t dimension, std::uint8_t flags);

    void ApplyWorldSpace(const Shared::RPC::WorldSpace &space);

    void ApplyPlayerScript(const Shared::RPC::PlayerScript &script);
    void ApplyPlayerWeapon(std::uint8_t action, std::uint32_t weaponType, std::uint32_t ammo);

    // Movement and aim from another player's own client. Decoded into the
    // same PlayerEntity fields the apply path already reads, so nothing below
    // the decode knows the state arrived by packet rather than by replica.
    void ApplyRemoteOnFootSync(const Shared::RPC::PlayerOnFootSync &sync);
    void ApplyRemoteAimSync(const Shared::RPC::PlayerAimSync &sync);

    // Server told us a player's compact wire id. Until it arrives, motion for
    // that player cannot be addressed.
    void AdoptPlayerWireId(std::uint64_t networkId, std::uint16_t wireId);

    // Our own wire id, so the local client can address its own updates.
    void SetLocalPlayerWireId(std::uint16_t wireId);
} // namespace VantaMP::Game
