#pragma once

namespace VantaMP::Shared::Spawn {
    // Grove Street.
    //
    // SHARED, BECAUSE BOTH SIDES HAVE TO AGREE. The client puts its local ped
    // here during startup so the world has one; the server puts its copy of the
    // entity here so the interest grid measures from the right place. Two
    // separate constants drift, and the symptom is players streamed against a
    // position they are not standing at.
    inline constexpr float kDefaultX = 2488.562f;
    inline constexpr float kDefaultY = -1666.865f;
    inline constexpr float kDefaultZ = 12.8757f;
} // namespace VantaMP::Shared::Spawn
