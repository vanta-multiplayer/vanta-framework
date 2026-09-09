#pragma once

#include <cstdint>

namespace VantaMP::Shared {
    // WHAT A VEHICLE *IS*, SEPARATE FROM WHAT IT CAN DO.
    //
    // MTA splits this the same way: CClientVehicleManager::GetVehicleType maps
    // a model to a type, and that type then answers capability questions
    // (HasDoors, HasTurret, HasSirens, HasDamageModel...). The type never
    // selects a sync path - a bike is synchronised exactly like a car, minus
    // the capabilities it does not have. See docs/mta-vehicle-sync.md.
    //
    // Order matches GTA's own eVehicleType so a subclass read off a live
    // CVehicle can be compared against these directly.
    enum class VehicleType : std::uint8_t {
        Automobile = 0,
        MonsterTruck,
        Quad,
        Heli,
        Plane,
        Boat,
        Train,
        FakeHeli,
        FakePlane,
        Bike,
        Bmx,
        Trailer,
    };

    [[nodiscard]] inline constexpr VehicleType VehicleTypeOf(std::uint32_t modelId) {
        switch (modelId) {
        // --- BMX / pedal cycles ------------------------------------------
        case 481:
        case 509:
        case 510:
            return VehicleType::Bmx;

        // --- Motorcycles --------------------------------------------------
        case 448:
        case 461:
        case 462:
        case 463:
        case 468:
        case 521:
        case 522:
        case 523:
        case 581:
        case 586:
            return VehicleType::Bike;

        // --- Quad ---------------------------------------------------------
        case 471:
            return VehicleType::Quad;

        // --- Monster trucks -----------------------------------------------
        // GTA gives these their own subclass; they keep a damage model but
        // have their own suspension setup.
        case 444:
        case 556:
        case 557:
            return VehicleType::MonsterTruck;

        // --- Boats ---------------------------------------------------------
        case 430:
        case 446:
        case 452:
        case 453:
        case 454:
        case 472:
        case 473:
        case 484:
        case 493:
        case 595:
            return VehicleType::Boat;

        // --- Helicopters ----------------------------------------------------
        case 417:
        case 425:
        case 447:
        case 469:
        case 487:
        case 488:
        case 497:
        case 548:
        case 563:
            return VehicleType::Heli;

        // --- Planes ---------------------------------------------------------
        case 460:
        case 476:
        case 511:
        case 512:
        case 513:
        case 519:
        case 520:
        case 553:
        case 577:
        case 592:
        case 593:
            return VehicleType::Plane;

        // --- Trains -----------------------------------------------------------
        case 449:
        case 537:
        case 538:
        case 569:
        case 570:
        case 590:
            return VehicleType::Train;

        // --- Trailers ---------------------------------------------------------
        case 435:
        case 450:
        case 584:
        case 591:
        case 606:
        case 607:
        case 608:
        case 610:
        case 611:
            return VehicleType::Trailer;

        default:
            return VehicleType::Automobile;
        }
    }

    // The MTA vehicleType string, for scripting. Kept byte-identical to what
    // scripts already see.
    [[nodiscard]] inline constexpr const char *VehicleTypeName(std::uint32_t modelId) {
        switch (VehicleTypeOf(modelId)) {
        case VehicleType::Bmx:          return "BMX";
        case VehicleType::Bike:         return "Bike";
        case VehicleType::Quad:         return "Quad";
        case VehicleType::Boat:         return "Boat";
        case VehicleType::Heli:         return "Helicopter";
        case VehicleType::Plane:        return "Plane";
        case VehicleType::Train:        return "Train";
        case VehicleType::Trailer:      return "Trailer";
        case VehicleType::MonsterTruck: return "Monster Truck";
        default:                        return "Automobile";
        }
    }

    // --- Capabilities -------------------------------------------------------
    //
    // Each of these answers one question, and each mirrors an MTA
    // CClientVehicleManager predicate. Sync code asks these instead of
    // switching on the model id in a dozen places.
    //
    // Not all of them have a caller yet. VehicleIsBike, VehicleHasSirens,
    // VehicleHasTurret and VehicleHasTaxiLight are here because the sync
    // paths that need them (siren state, turret rotation, taxi lamp) are
    // still model-agnostic - they are the shape those gates will take, not
    // dead weight to delete. VehicleIsBike in particular is the predicate any
    // future bike-specific work should hang off rather than re-deriving.

    // A CDamageManager exists only on these subclasses. Bikes, BMX, boats and
    // trains have none, so doors / panels / lights are meaningless for them
    // and writing packed damage at one is a null deref waiting to happen.
    [[nodiscard]] inline constexpr bool VehicleHasDamageModel(VehicleType type) {
        switch (type) {
        case VehicleType::Automobile:
        case VehicleType::MonsterTruck:
        case VehicleType::Quad:
        case VehicleType::Heli:
        case VehicleType::Plane:
        case VehicleType::Trailer:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] inline constexpr bool VehicleHasDamageModel(std::uint32_t modelId) {
        return VehicleHasDamageModel(VehicleTypeOf(modelId));
    }

    // Doors follow the damage model, minus a list of open-topped or
    // doorless bodies that still have one. MTA keeps the same exception list.
    [[nodiscard]] inline constexpr bool VehicleHasDoors(std::uint32_t modelId) {
        if (!VehicleHasDamageModel(modelId)) {
            return false;
        }

        switch (modelId) {
        case 424: // BF Injection
        case 441: // RC Bandit
        case 457: // Caddy
        case 464: // RC Raider
        case 465: // RC Tiger (Bandito family)
        case 485: // Baggage
        case 486: // Dozer
        case 494: // Hotring Racer
        case 501: // RC Goblin
        case 530: // Forklift
        case 531: // Tractor
        case 532: // Combine
        case 564: // RC Tiger
        case 568: // Bandito
        case 571: // Kart
        case 572: // Mower
        case 574: // Sweeper
        case 594: // RC Cam
            return false;
        default:
            return true;
        }
    }

    // Rotors, and the aircraft-only shoulder controls that go with them.
    [[nodiscard]] inline constexpr bool VehicleIsAircraft(VehicleType type) {
        return type == VehicleType::Heli || type == VehicleType::Plane || type == VehicleType::FakeHeli
            || type == VehicleType::FakePlane;
    }

    [[nodiscard]] inline constexpr bool VehicleIsAircraft(std::uint32_t modelId) {
        return VehicleIsAircraft(VehicleTypeOf(modelId));
    }

    // Rotor-craft specifically. Planes are aircraft but have no CHeli rotor
    // fields, so anything reaching for m_fRotorZ has to ask this and not
    // VehicleIsAircraft.
    [[nodiscard]] inline constexpr bool VehicleIsHeli(VehicleType type) {
        return type == VehicleType::Heli || type == VehicleType::FakeHeli;
    }

    [[nodiscard]] inline constexpr bool VehicleIsHeli(std::uint32_t modelId) {
        return VehicleIsHeli(VehicleTypeOf(modelId));
    }

    // Two wheels and a rider whose weight is part of the physics. The reason
    // this predicate exists at all: GTA raises a bike's centre of mass when it
    // has no rider (tBikeHandlingData::fNoPlayerCOMz), and CBike::ProcessControl
    // is materially driver-gated.
    [[nodiscard]] inline constexpr bool VehicleIsBike(VehicleType type) {
        return type == VehicleType::Bike || type == VehicleType::Bmx;
    }

    [[nodiscard]] inline constexpr bool VehicleIsBike(std::uint32_t modelId) {
        return VehicleIsBike(VehicleTypeOf(modelId));
    }

    // Landing gear is a real, retractable thing on exactly these.
    [[nodiscard]] inline constexpr bool VehicleHasLandingGear(std::uint32_t modelId) {
        switch (modelId) {
        case 476: // Rustler
        case 520: // Hydra
        case 553: // Nevada
        case 577: // AT-400
        case 592: // Andromada
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] inline constexpr bool VehicleHasSirens(std::uint32_t modelId) {
        switch (modelId) {
        case 416: // Ambulance
        case 407: // Firetruck
        case 427: // Enforcer
        case 490: // FBI Rancher
        case 523: // HPV1000
        case 528: // FBI Truck
        case 596: // Police LS
        case 597: // Police SF
        case 598: // Police LV
        case 599: // Police Ranger
        case 601: // S.W.A.T.
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] inline constexpr bool VehicleHasTurret(std::uint32_t modelId) {
        switch (modelId) {
        case 432: // Rhino
        case 407: // Firetruck
        case 601: // S.W.A.T.
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] inline constexpr bool VehicleHasTaxiLight(std::uint32_t modelId) {
        switch (modelId) {
        case 420: // Taxi
        case 438: // Cabbie
            return true;
        default:
            return false;
        }
    }

    // Seats, including the driver's. 255 in MTA's table means "driver only"
    // (trailers and the like); we express that as 0 passengers.
    [[nodiscard]] inline constexpr std::uint8_t VehicleMaxPassengers(std::uint32_t modelId) {
        switch (modelId) {
        case 431: // Bus
        case 437: // Coach
            return 8;

        case 408: // Trashmaster
        case 416: // Ambulance
        case 423: // Mr Whoopee
        case 427: // Enforcer
        case 428: // Securicar
        case 433: // Barracks
        case 508: // Journey
        case 544: // Firetruck LA
        case 573: // Dune
        case 578: // DFT-30
            return 1;

        default:
            break;
        }

        switch (VehicleTypeOf(modelId)) {
        case VehicleType::Bmx:
            return 0;

        case VehicleType::Bike:
        case VehicleType::Quad:
            return 1;

        case VehicleType::Trailer:
        case VehicleType::Train:
            return 0;

        case VehicleType::Boat:
        case VehicleType::Heli:
        case VehicleType::Plane:
            return 1;

        default:
            return 3;
        }
    }
} // namespace VantaMP::Shared
