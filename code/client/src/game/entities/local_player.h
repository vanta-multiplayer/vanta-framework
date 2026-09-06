#pragma once

namespace VantaMP::Game {
    struct SpawnPoint {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    // The GTA ped this client controls.
    //
    // Built on the first gameplay frame because the camera and world call
    // CEntity::RegisterReference on FindPlayerPed(0) and crash if it is null.
    // MTA already has that ped from StartGame; we stub the SCM, so we have to
    // create it ourselves. It stays allocated and in the world for the process.
    //
    // The SERVER session is separate: ApplyLocalSpawn is what places them and
    // lifts the fade. DestroyLocalPlayer ends the session, not the GTA ped.
    void CreateLocalPlayer();

    // Ends the multiplayer session. The ped is unlinked, unregistered and
    // hidden. It is not removed from the world and not freed - that is the
    // 0x571B73 crash (RegisterReference on a null player).
    void DestroyLocalPlayer();

    [[nodiscard]] bool IsPlayerSpawned();

    // Where the ped goes before a server has an opinion. A placeholder, and
    // deliberately one nobody sees.
    void SetSpawnPoint(const SpawnPoint &point);

    // Puts the local player where the server says, and reveals the world.
    //
    // THE SERVER DECIDES WHERE A PLAYER BELONGS. Later calls move an existing
    // ped (a respawn). The fade only lifts when the world is currently hidden.
    void ApplyLocalSpawn(float x, float y, float z, float heading, float health, float armour);

    // Black screen for the connect menu. The next ApplyLocalSpawn fades back in.
    void ConcealWorld();
} // namespace VantaMP::Game
