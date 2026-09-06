#pragma once

namespace VantaMP::Game {
    struct SpawnPoint {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    // The ped this client controls.
    //
    // Created during startup because the world needs one to exist, and put at a
    // placeholder behind a black screen because until a server says otherwise
    // there is no position that means anything.
    void CreateLocalPlayer();

    [[nodiscard]] bool IsPlayerSpawned();

    // Where the ped goes before a server has an opinion. A placeholder, and
    // deliberately one nobody sees.
    void SetSpawnPoint(const SpawnPoint &point);

    // Puts the local player where the server says, and reveals the world.
    //
    // THE SERVER DECIDES WHERE A PLAYER BELONGS. This is the first position
    // that means anything, which is why it is also what lifts the fade.
    //
    // Safe to call again on a player already in the world: it moves them, which
    // is what a respawn is. The fade only lifts when the world is currently
    // hidden (first join, or after a disconnect) - an in-session respawn must
    // not flash the screen.
    void ApplyLocalSpawn(float x, float y, float z, float heading, float health, float armour);

    // Black screen for the connect menu. The next ApplyLocalSpawn fades back in.
    void ConcealWorld();
} // namespace VantaMP::Game
