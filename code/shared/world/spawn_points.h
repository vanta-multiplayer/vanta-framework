#pragma once

namespace VantaMP::Shared::Spawn {
    // The waterfront by Los Santos beach, a few steps from deep water.
    //
    // SHARED, BECAUSE BOTH SIDES HAVE TO AGREE. The client puts its local ped
    // here during startup so the world has one; the server puts its copy of the
    // entity here so the interest grid measures from the right place. Two
    // separate constants drift, and the symptom is players streamed against a
    // position they are not standing at.
    //
    // A TESTING SPAWN, not a design decision. Swimming is the least exercised
    // thing the client does and the hardest to reach, so both players start
    // within walking distance of it. Grove Street is below and should come back
    // once remote players in water have been watched for long enough.
    inline constexpr float kDefaultX = 2899.41f;
    inline constexpr float kDefaultY = -1672.64f;
    inline constexpr float kDefaultZ = 12.89f;

    // Grove Street, the previous spawn.
    // inline constexpr float kDefaultX = 2488.562f;
    // inline constexpr float kDefaultY = -1666.865f;
    // inline constexpr float kDefaultZ = 12.8757f;
} // namespace VantaMP::Shared::Spawn
