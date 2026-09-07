#pragma once

#include <cstdint>

#include <glm/gtc/quaternion.hpp>

namespace VantaMP::SDK {
    // Peds owned by somebody else, addressed by the player slot they occupy.
    //
    // Creating one needs plugin-sdk's CPlayerPed constructor and a dozen of its
    // fields, so it lives behind this boundary with the rest of the SDK.
    // Everything above the boundary works in slots and plain numbers.

    struct Position {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    // Builds a ped for `slot` and puts it in the world.
    //
    // The sequence mirrors what the game does for the local player, in the same
    // order. Every step in it exists because leaving it out produced something
    // specific and unpleasant - see the implementation.
    //
    // Returns the ped, or nullptr if it could not be created.
    void *CreateRemotePed(int slot, int modelIndex, float x, float y, float z);

    // Removes a ped created by CreateRemotePed.
    void DestroyRemotePed(int slot);

    // Moves a ped that already exists. Position and heading only - velocity and
    // animation come from the game processing the ped's input.
    void SetRemotePedTransform(int slot, float x, float y, float z, float heading);

    // Where a ped currently is, which is not where it was last told to be: the
    // game moves it too, from its own physics and the input it was given. The
    // difference between the two is what the interpolation corrects.
    [[nodiscard]] Position GetRemotePedPosition(int slot);

    // GTA's own head bone. plugin-sdk names it BONE_HEAD; the number is here
    // so callers above the SDK boundary do not have to.
    inline constexpr unsigned int kBoneHead = 8;

    // Where a named bone is this frame. updateSkin is the expensive rebuild;
    // nametags do not need it, because the game has already posed the ped by
    // the time the overlay draws.
    [[nodiscard]] Position GetRemotePedBonePosition(int slot, unsigned int bone, bool updateSkin = false);

    void SetRemotePedPosition(int slot, float x, float y, float z);
    // includeAiming writes the upper-body target as well. Leave it false while
    // a gun is up: that field then belongs to the aim IK, and writing it is
    // what makes the hands stutter against a smooth heading lerp.
    void SetRemotePedHeading(int slot, float heading, bool includeAiming = true);

    // The ped's facing, as a quaternion so it interpolates as a rotation rather
    // than as an angle - lerping two headings the short way round is what makes
    // a player spin the wrong way past due north.
    [[nodiscard]] glm::quat GetRemotePedRotation(int slot);

    // Health and armour, and a revive if the ped is still in a death state
    // while the snapshot says they are alive. Writing the numbers alone
    // leaves them on the ground.
    void SetRemotePedHealth(int slot, float health, float armour);

    // The momentum a remote ped carries between updates.
    //
    // Without it a remote ped has no speed of its own: it only ever moves when a
    // correction drags it, so it lags behind by however far its owner travelled
    // since the last update - which at 100ms is metres.
    //
    // Not on every update while on foot, though - see the caller.
    void SetRemotePedVelocity(int slot, float x, float y, float z);

    // Puts a remote ped into, or out of, a crouch.
    //
    // DUCKING IS A TASK, NOT A FLAG. Writing bIsDucking to start one looks like
    // it works and does nothing: the flag is an OUTPUT the game recomputes from
    // whatever task is running, so the ped stands straight back up on the next
    // frame. The pose has to be produced the way the owner's game produced it,
    // by running the same task - which is what MTA's CClientPed::Duck does.
    //
    // Ending one is not symmetric with starting one; the implementation says
    // why, and it matters.
    //
    // Idempotent: asking for a crouch that is already running does nothing, so
    // this can be called on every frame without restarting the animation.
    void SetRemotePedDucked(int slot, bool ducked);

    // Puts a remote ped in the water.
    //
    // Call this when the owner reports being in water; it does nothing if the
    // ped is already swimming, so it can be called on every frame.
    //
    // THERE IS NO MATCHING WAY OUT, DELIBERATELY. Getting out of water is a
    // decision the receiving client's own game is perfectly able to make, and
    // does: the task ends itself when the ped is no longer in water. Only
    // getting IN needs help, because a remote ped is walked by replayed input
    // and can be somewhere its own physics has not caught up with yet. MTA
    // starts this task on the same edge and likewise never ends it.
    //
    // Returns whether it actually installed a task, which should be true once
    // per entry into water and false on every other call. Anything else means
    // the task is not being found where it was put, and a ped handed a fresh
    // swim task every frame never gets to swim in any of them.
    bool StartRemotePedSwimming(int slot);

    // Off while a remote ped is in a car so they do not shove it around.
    void SetRemotePedCollision(int slot, bool enabled);

    // CEntity::m_nAreaCode. GTA hides entities whose area is not currArea,
    // which is how a remote player in a shop disappears from the street.
    void SetRemotePedInterior(int slot, std::uint8_t interior);

    // What a remote player HOLDS is in sdk/ped/weapon.h, not here.
} // namespace VantaMP::SDK
