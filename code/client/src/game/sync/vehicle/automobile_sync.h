#pragma once

#include "game/sync/vehicle/handler.h"

// The complete network synchronisation lifecycle of a CAutomobile.
namespace VantaMP::Game::VehicleSync::Automobile {
    void OnSnapshot(Frame &frame);
    void Apply(Frame &frame);
} // namespace VantaMP::Game::VehicleSync::Automobile
