#pragma once

namespace VantaMP::Game {
    // Switches off GTA's autonomous population.
    //
    // Single-player San Andreas invents pedestrians and traffic wherever the
    // player is looking. In multiplayer the server decides what exists: two
    // clients inventing their own crowds would each see a different world, and
    // nothing about a locally-invented ped can be synchronised.
    //
    // What is NOT disabled: streaming, paths and the entity pools. The world
    // still loads, and models the client asks for explicitly still arrive - the
    // pools have to keep working because Vanta's own peds and vehicles live in
    // them.
    //
    // Idempotent, and safe to call before the world exists.
    void DisableAutonomousPopulation();

    [[nodiscard]] bool IsAutonomousPopulationDisabled();
} // namespace VantaMP::Game
