#pragma once

#include "game/sync/vehicle/handler.h"

namespace VantaMP::Game::VehicleSync {
    // Reconciliation pieces that are genuinely shared, not "shared because
    // they happened to be written once". Each is used by more than one
    // handler; anything used by exactly one lives in that handler's file.

    // MTA SetTargetPosition, adapted. Aims the position interpolator at the
    // packet, leading by a few ms of the VEHICLE'S OWN speed (not the
    // packet's - see CClientVehicle::SetTargetPosition, which reads
    // GetMoveSpeed off the vehicle) and scaling the error by
    // lerp(0.25, 1, unlerp(100, delay, 400)).
    void AimPosition(Frame &frame);

    // MTA SetTargetRotation. Slerped error over the same window.
    void AimRotation(Frame &frame);

    // Has the vehicle drifted far enough that easing it back would look worse
    // than placing it? Threshold scales with speed and tick rate.
    [[nodiscard]] bool ShouldWarp(const Frame &frame);

    // Re-aims both interpolators at the pose we are about to force, so the
    // next frame does not immediately drag the vehicle back off it.
    void ReaimAfterWarp(Frame &frame);

    // Momentum and the states that are not momentum. Health/engine/siren are
    // per-frame facts; move and turn speed are per-PACKET facts, because GTA
    // integrates them and re-writing them every frame cancels that
    // integration. `withMomentum` is the packet edge.
    void ApplyMotion(Frame &frame, bool withMomentum);
} // namespace VantaMP::Game::VehicleSync
