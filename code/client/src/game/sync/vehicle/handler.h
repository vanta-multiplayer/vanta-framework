#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>

namespace Framework::Utils {
    class Interpolator;
} // namespace Framework::Utils

namespace VantaMP::Shared {
    struct VehicleEntity;
} // namespace VantaMP::Shared

namespace VantaMP::Game::VehicleSync {
    // EVERYTHING A TYPE HANDLER NEEDS TO DRIVE ONE VEHICLE FOR ONE FRAME.
    //
    // Deliberately NOT the wire format. Shared::VehicleEntity is what crosses
    // the network and stays generic; this is the per-frame view a handler
    // works from, plus the little bit of cross-frame state reconciliation
    // needs. Splitting them is what lets a CBike and a CAutomobile apply the
    // same packet under completely different policies without either of them
    // knowing anything about serialization.
    struct Frame {
        void                  *handle = nullptr;
        Shared::VehicleEntity *entity = nullptr;

        // Cross-frame reconciliation state, owned by the generic layer but
        // read and written by handlers.
        Framework::Utils::Interpolator *interpolator = nullptr;

        glm::vec3 aimedPosition {};
        glm::quat aimedRotation = glm::identity<glm::quat>();
        bool      aimed         = false;
        float     speed         = 0.0f;

        // Momentum as of the last packet. Applied on the packet edge only -
        // MTA's rule, and the one that keeps GTA's own integration intact.
        glm::vec3 velocity {};
        glm::vec3 angular {};

        // True on the frame a new packet arrived.
        bool packetArrived = false;

        // GetTickCount() when the current packet landed. Handlers that dead
        // reckon between packets need it to know how stale the target is.
        std::uint32_t packetMs = 0;

        // Does GTA think somebody is riding it? A driverless vehicle is the
        // game's own business and several policies hinge on this.
        bool hasDriver = false;
    };

    // One handler per GTA physical vehicle class.
    //
    // Free functions rather than a class hierarchy: the project's sync layer
    // is written this way throughout, and a vtable would buy nothing here.
    // Adding a boat means adding boat_sync.cpp and one switch arm.
    struct Handler {
        // Called once per frame for a remote vehicle, after the generic layer
        // has done cosmetics, damage, doors and bookkeeping. Owns the whole
        // physical lifecycle from here: pose, momentum, controls.
        void (*Apply)(Frame &frame) = nullptr;

        // Called when a new packet arrives, before Apply. Where a handler
        // decides how the interpolator should be aimed for its vehicle type.
        void (*OnSnapshot)(Frame &frame) = nullptr;
    };

    // Resolves the handler for a vehicle's GTA class, once.
    [[nodiscard]] const Handler &HandlerFor(void *vehicle);
} // namespace VantaMP::Game::VehicleSync
