#pragma once

namespace VantaMP::Game {
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
} // namespace VantaMP::Game
