#pragma once

#include "common.h"
#include "driver_sync.h"
#include "unoccupied_sync.h"
#include "shared/vehicle/validate.h"

#include <cstdint>

namespace VantaMP::Shared::RPC::Vehicle {
    // Server-side acceptance rules for vehicle motion.
    //
    // SPLITTING PACKETS MUST NOT LOSE AUTHORITY. Each new packet gained its
    // own sender check, and none of them may be satisfied by "the sender is
    // a connected player" - that was true of the old damage RPC and it let
    // any client damage any vehicle anywhere on the map.
    //
    // These are pure predicates so they can be unit-tested without a server:
    // the caller supplies what it knows, and gets a yes/no plus a reason.

    enum class Reject : std::uint8_t {
        None = 0,
        UnknownVehicle,   // no such entity
        NotOwner,         // sender does not hold authority
        NotDriver,        // sender is in the vehicle but not in seat 0
        StaleSequence,    // older than the newest applied update
        BadPosition,      // NaN/Inf or outside world bounds
        BadVelocity,      // NaN/Inf or implausible magnitude
        BadRotation,      // NaN/Inf or degenerate quaternion
        BadHealth,        // NaN/Inf or out of range
        OutOfRange,       // sender is too far away to be a witness
    };

    [[nodiscard]] inline const char *RejectReason(Reject reason) {
        switch (reason) {
        case Reject::None: return "accepted";
        case Reject::UnknownVehicle: return "unknown vehicle";
        case Reject::NotOwner: return "sender is not the owner";
        case Reject::NotDriver: return "sender is not the driver";
        case Reject::StaleSequence: return "stale sequence";
        case Reject::BadPosition: return "invalid position";
        case Reject::BadVelocity: return "invalid velocity";
        case Reject::BadRotation: return "invalid rotation";
        case Reject::BadHealth: return "invalid health";
        case Reject::OutOfRange: return "sender out of range";
        default: return "rejected";
        }
    }

    // Health arriving over the wire. The quantizer already clamps to
    // 0..kHealthMax, so this only has to catch a NaN that came from a
    // corrupted or hostile sender before it reaches a GTA field.
    [[nodiscard]] inline bool IsValidHealth(float health) {
        return Shared::IsFiniteFloat(health) && health >= 0.0f && health <= kHealthMax + 1.0f;
    }

    // Validates only the fields the mask actually claims to carry.
    //
    // A FIELD THAT WAS NOT SENT IS NOT INVALID. Validating an absent field
    // would reject a perfectly good partial update because some default
    // sitting in the struct happened to fail a bounds check.
    [[nodiscard]] inline Reject ValidateDriverSync(const VehicleDriverSync &sync) {
        if (sync.Has(DriverField::kPosition) && !Shared::IsValidPosition(sync.position)) {
            return Reject::BadPosition;
        }

        if (sync.Has(DriverField::kRotation) && !Shared::IsValidRotation(sync.rotation)) {
            return Reject::BadRotation;
        }

        if (sync.Has(DriverField::kVelocity) && !Shared::IsValidVelocity(sync.velocity)) {
            return Reject::BadVelocity;
        }

        if (sync.Has(DriverField::kTurnSpeed) && !Shared::IsValidVelocity(sync.turnSpeed)) {
            return Reject::BadVelocity;
        }

        if (sync.Has(DriverField::kHealth) && !IsValidHealth(sync.health)) {
            return Reject::BadHealth;
        }

        return Reject::None;
    }

    [[nodiscard]] inline Reject ValidateUnoccupiedEntry(const VehicleUnoccupiedEntry &entry) {
        if (entry.Has(UnoccupiedField::kPosition) && !Shared::IsValidPosition(entry.position)) {
            return Reject::BadPosition;
        }

        if (entry.Has(UnoccupiedField::kRotation) && !Shared::IsValidRotation(entry.rotation)) {
            return Reject::BadRotation;
        }

        if (entry.Has(UnoccupiedField::kVelocity) && !Shared::IsValidVelocity(entry.velocity)) {
            return Reject::BadVelocity;
        }

        if (entry.Has(UnoccupiedField::kTurnSpeed) && !Shared::IsValidVelocity(entry.turnSpeed)) {
            return Reject::BadVelocity;
        }

        if (entry.Has(UnoccupiedField::kHealth) && !IsValidHealth(entry.health)) {
            return Reject::BadHealth;
        }

        return Reject::None;
    }

    // THE PASSENGER RULE, IN ONE PLACE.
    //
    // Owning the entity is not enough to drive it. A player who drove a car,
    // got out and climbed back into a passenger seat still holds ownership
    // (the server keeps it across an exit so an empty car still rolls), so
    // an ownership test alone would let them steer from the back seat - the
    // authority bug that was already fixed once on the server and must not
    // reappear through a new packet.
    [[nodiscard]] inline Reject CheckDriverAuthority(bool vehicleExists, bool senderOwnsVehicle, bool senderIsSeatZero, bool senderIsSeated) {
        if (!vehicleExists) {
            return Reject::UnknownVehicle;
        }

        if (!senderOwnsVehicle) {
            return Reject::NotOwner;
        }

        // Seated but not in seat 0 is a passenger: refuse. Not seated at all
        // is the legitimate "drove it, got out, still simulating the empty
        // car" case, which ownership alone already covers.
        if (senderIsSeated && !senderIsSeatZero) {
            return Reject::NotDriver;
        }

        return Reject::None;
    }

    [[nodiscard]] inline Reject CheckUnoccupiedAuthority(bool vehicleExists, bool senderIsAssignedSyncer) {
        if (!vehicleExists) {
            return Reject::UnknownVehicle;
        }

        return senderIsAssignedSyncer ? Reject::None : Reject::NotOwner;
    }
} // namespace VantaMP::Shared::RPC::Vehicle
