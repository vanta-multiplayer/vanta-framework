#include "game/sync/vehicle/handler.h"

#include "game/sync/vehicle/automobile_sync.h"
#include "game/sync/vehicle/bike_sync.h"
#include "sdk/vehicle/vehicle.h"

namespace VantaMP::Game::VehicleSync {
    namespace {
        // ONE ARM PER GTA PHYSICAL VEHICLE CLASS.
        //
        // Adding a boat means writing boat_sync.cpp and adding an arm here.
        // No existing handler changes, which is the whole point of the split:
        // CBoat, CPlane and CHeli have materially different physics and will
        // eventually each want their own reconciliation policy.
        constinit Handler gBike {&Bike::Apply, &Bike::OnSnapshot};
        constinit Handler gAutomobile {&Automobile::Apply, &Automobile::OnSnapshot};
    } // namespace

    const Handler &HandlerFor(void *vehicle) {
        // Everything that is not a two-wheeler currently rides the automobile
        // path, which is also what boats/planes/helis used before the split.
        // That is a deliberate default rather than an oversight: it preserves
        // today's behaviour exactly until a type earns its own handler.
        return SDK::IsBikeHandle(vehicle) ? gBike : gAutomobile;
    }
} // namespace VantaMP::Game::VehicleSync
