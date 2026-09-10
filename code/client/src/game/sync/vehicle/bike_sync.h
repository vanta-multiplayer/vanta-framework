#pragma once

#include "game/sync/vehicle/handler.h"

// The complete network synchronisation lifecycle of a CBike / CBmx.
// See bike_sync.cpp for why a motorcycle needs its own policy.
namespace VantaMP::Game::VehicleSync::Bike {
    void OnSnapshot(Frame &frame);
    void Apply(Frame &frame);
} // namespace VantaMP::Game::VehicleSync::Bike
