#include "vehicle_occupancy.h"

#include "ped/access.h"
#include "sdk/game_sdk.h"
#include "sdk/patterns.h"

#include "game/entities/player_slots.h"

#include <CTaskComplexEnterCar.h>
#include <CTaskComplexEnterCarAsDriver.h>
#include <CTaskComplexEnterCarAsPassenger.h>
#include <CTaskComplexLeaveCar.h>
#include <CTaskSimpleCarSetPedInAsDriver.h>
#include <CTaskSimpleCarSetPedInAsPassenger.h>
#include <CTaskSimpleCarSetPedOut.h>

#include <algorithm>
#include <cmath>

namespace VantaMP::SDK {
    namespace {
        using Internal::PedAt;
        using Internal::TaskIdOf;
        using Internal::TaskVirtual;

        using OperatorNewFn   = void *(__cdecl *)(unsigned int);
        using SetTaskFn       = void(__thiscall *)(void *, void *, int, bool);
        using FlushTasksFn    = void(__thiscall *)(void *, bool);
        using ProcessPedFn    = bool(__thiscall *)(void *, CPed *);
        using DoorAsPassengerFn = int(__cdecl *)(CVehicle *, int);
        using DoorToExitFn      = int(__cdecl *)(CVehicle *, CPed *);
        using SeatFromDoorFn    = int(__cdecl *)(const CVehicle *, int);

        using EnterDriverCtorFn    = void *(__thiscall *)(void *, CVehicle *);
        using EnterPassengerCtorFn = void *(__thiscall *)(void *, CVehicle *, int, bool);
        using SetInDriverCtorFn    = void *(__thiscall *)(void *, CVehicle *, void *);
        using SetInPassengerCtorFn = void *(__thiscall *)(void *, CVehicle *, int, void *);
        using LeaveCarCtorFn       = void *(__thiscall *)(void *, CVehicle *, int, int, bool, bool);
        using SetOutCtorFn         = void *(__thiscall *)(void *, CVehicle *, int, bool);

        constexpr std::size_t kProcessPedSlot = 7;

        constexpr unsigned int kEnterDriverBytes    = 0x50;
        constexpr unsigned int kEnterPassengerBytes = 0x50;
        constexpr unsigned int kLeaveCarBytes       = 0x34;
        constexpr unsigned int kSetInDriverBytes    = 0x1C;
        constexpr unsigned int kSetInPassengerBytes = 0x20;
        constexpr unsigned int kSetOutBytes         = 0x18;

        constexpr int kNoDoor     = 0;
        constexpr int kNoSuchDoor = 0xFF;

        CPed *PedFor(int slot) {
            return slot <= 0 ? Internal::LocalPed() : PedAt(slot);
        }

        CTask *PrimaryTask(CPed *ped) {
            if (!ped || !ped->m_pIntelligence) {
                return nullptr;
            }

            return ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[TASK_PRIMARY_PRIMARY];
        }

        bool IsEnterTaskId(eTaskType id) {
            switch (id) {
            case TASK_COMPLEX_ENTER_CAR_AS_DRIVER:
            case TASK_COMPLEX_ENTER_CAR_AS_DRIVER_TIMED:
            case TASK_COMPLEX_ENTER_CAR_AS_PASSENGER:
            case TASK_COMPLEX_ENTER_CAR_AS_PASSENGER_TIMED:
                return true;

            default:
                return false;
            }
        }

        bool PedIsShuffling(CPed *ped) {
            auto *primary = PrimaryTask(ped);

            return primary && TaskIdOf(primary) == TASK_COMPLEX_SHUFFLE_SEATS;
        }

        bool IsLeaveTaskId(eTaskType id) {
            switch (id) {
            case TASK_COMPLEX_LEAVE_CAR:
            case TASK_COMPLEX_LEAVE_CAR_AND_DIE:
            case TASK_COMPLEX_LEAVE_CAR_AND_FLEE:
            case TASK_COMPLEX_LEAVE_CAR_AND_WANDER:
            case TASK_COMPLEX_LEAVE_ANY_CAR:
            case TASK_COMPLEX_LEAVE_BOAT:
                return true;

            default:
                return false;
            }
        }

        bool IsDriverEnterTaskId(eTaskType id) {
            return id == TASK_COMPLEX_ENTER_CAR_AS_DRIVER || id == TASK_COMPLEX_ENTER_CAR_AS_DRIVER_TIMED;
        }

        CVehicle *OccupiedVehicleOf(CPed *ped) {
            if (!ped || !ped->bInVehicle) {
                return nullptr;
            }

            return ped->m_pVehicle;
        }

        std::uint8_t SeatIn(const CVehicle *vehicle, const CPed *ped) {
            if (!vehicle || !ped) {
                return kInvalidSeat;
            }

            if (vehicle->m_pDriver == ped) {
                return kDriverSeat;
            }

            for (std::uint8_t i = 0; i < kMaxPassengerSeats; ++i) {
                if (vehicle->m_apPassengers[i] == ped) {
                    return static_cast<std::uint8_t>(i + 1u);
                }
            }

            return kInvalidSeat;
        }

        bool SeatExists(const CVehicle *vehicle, std::uint8_t seat) {
            if (seat == kDriverSeat) {
                return true;
            }

            const std::uint8_t passengerIndex = static_cast<std::uint8_t>(seat - 1u);
            const bool pastArray              = passengerIndex >= kMaxPassengerSeats;
            const bool pastDeclared           = vehicle->m_nMaxPassengers != kUnknownPassengerCount && passengerIndex >= vehicle->m_nMaxPassengers;

            return !pastArray && !pastDeclared;
        }

        void PrepareForTransition(CPed *ped) {
            // FlushImmediately clears the walk that would otherwise fight the
            // climb. CantBeKnockedOffBike = never: a remote ped is a puppet
            // and local physics knocking it off a bike has nothing to correct it.
            reinterpret_cast<FlushTasksFn>(gPatterns.CPedIntelligence__FlushImmediately)(ped->m_pIntelligence, false);
            ped->CantBeKnockedOffBike = 1u;
        }

        void *AllocateTask(unsigned int bytes) {
            return reinterpret_cast<OperatorNewFn>(gPatterns.CTask__operator_new)(bytes);
        }

        void SetPrimaryTask(CPed *ped, void *task) {
            reinterpret_cast<SetTaskFn>(gPatterns.CTaskManager__SetTask)(&ped->m_pIntelligence->m_TaskMgr, task, TASK_PRIMARY_PRIMARY, false);
        }

        void ClearPrimaryTask(CPed *ped) {
            SetPrimaryTask(ped, nullptr);
        }

        bool ProcessSimpleTask(void *task, CPed *ped) {
            return reinterpret_cast<ProcessPedFn>(TaskVirtual(task, kProcessPedSlot))(task, ped);
        }

        void GiveDefaultPrimary(CPed *ped) {
            // Empty primary slot = frozen mid-stride. FlushImmediately(true)
            // creates a default idle. CoopAndreas: "create a default primary task".
            reinterpret_cast<FlushTasksFn>(gPatterns.CPedIntelligence__FlushImmediately)(ped->m_pIntelligence, true);
        }

        bool PedIsEntering(CPed *ped) {
            auto *primary = PrimaryTask(ped);

            return primary && IsEnterTaskId(TaskIdOf(primary));
        }

        bool PedIsLeaving(CPed *ped) {
            auto *primary = PrimaryTask(ped);

            return primary && IsLeaveTaskId(TaskIdOf(primary));
        }

        CVehicle *EnterTargetOf(CPed *ped) {
            auto *primary = PrimaryTask(ped);

            if (!primary || !IsEnterTaskId(TaskIdOf(primary))) {
                return nullptr;
            }

            return static_cast<CTaskComplexEnterCar *>(primary)->m_pTargetVehicle;
        }

        std::uint8_t EnterSeatOf(CPed *ped) {
            auto *primary = PrimaryTask(ped);

            if (!primary) {
                return kDriverSeat;
            }

            const auto id = TaskIdOf(primary);

            if (IsDriverEnterTaskId(id)) {
                return kDriverSeat;
            }

            if (!IsEnterTaskId(id)) {
                return kDriverSeat;
            }

            auto *enter = static_cast<CTaskComplexEnterCar *>(primary);

            // ALREADY IN A SEAT WINS. The task still names a door while the
            // ped is walking; once they sit, the vehicle is the game's answer.
            const auto occupied = SeatIn(enter->m_pTargetVehicle, ped);

            if (occupied != kInvalidSeat && occupied != kDriverSeat) {
                return occupied;
            }

            // m_nTargetSeat IS A DOOR. Adding one produced seat 9 from door 8
            // and the server refused the packet. CoopAndreas and old Vanta
            // convert with ComputePassengerIndexFromCarDoor.
            const int index = reinterpret_cast<SeatFromDoorFn>(gPatterns.CCarEnterExit__ComputePassengerIndexFromCarDoor)(enter->m_pTargetVehicle, enter->m_nTargetSeat);

            if (index < 0) {
                return 1;
            }

            const int seat = index + 1;

            return seat > static_cast<int>(kMaxPassengerSeats) ? kMaxPassengerSeats : static_cast<std::uint8_t>(seat);
        }

        std::uint8_t DoorOfEnterTask(CPed *ped) {
            auto *primary = PrimaryTask(ped);

            if (!primary || !IsEnterTaskId(TaskIdOf(primary))) {
                return kInvalidDoor;
            }

            const int door = static_cast<CTaskComplexEnterCar *>(primary)->m_nTargetDoor;

            if (door < 0 || door > 255) {
                return kInvalidDoor;
            }

            return static_cast<std::uint8_t>(door);
        }

        std::uint8_t DoorOfLeaveTask(CPed *ped) {
            auto *primary = PrimaryTask(ped);

            if (!primary || TaskIdOf(primary) != TASK_COMPLEX_LEAVE_CAR) {
                return kInvalidDoor;
            }

            const int door = static_cast<CTaskComplexLeaveCar *>(primary)->m_nTargetDoor;

            if (door < 0 || door > 255) {
                return kInvalidDoor;
            }

            return static_cast<std::uint8_t>(door);
        }

        void ApplyEnterDoor(void *task, std::uint8_t door) {
            if (!task || door == kInvalidDoor) {
                return;
            }

            // MTA SetTargetDoor: same field, same GTA eTargetDoor ids.
            // Door 0 is FRONT_LEFT and is a real door, so 0xFF is "unset".
            static_cast<CTaskComplexEnterCar *>(task)->m_nTargetDoor = door;
        }
    } // namespace

    PedOccupancy QueryPedOccupancy(int slot) {
        PedOccupancy occupancy;

        auto *ped = PedFor(slot);

        if (!ped) {
            return occupancy;
        }

        occupancy.entering = PedIsEntering(ped) || PedIsShuffling(ped);
        occupancy.leaving  = PedIsLeaving(ped);

        if (auto *vehicle = OccupiedVehicleOf(ped)) {
            occupancy.vehicle = vehicle;
            occupancy.seated  = true;
            occupancy.seat    = SeatIn(vehicle, ped);

            if (occupancy.seat == kInvalidSeat) {
                occupancy.seat = kDriverSeat;
            }

            // TARGET SEAT, NOT THE CUSHION THEY ARE ON. A driver who opened
            // the passenger door sits there first, then shuffles. Reporting
            // seat 1 here made every other client treat the climb as done
            // and left them in the passenger seat. MTA's inout seat is the
            // one they asked for.
            if (PedIsEntering(ped)) {
                occupancy.seat = EnterSeatOf(ped);
                occupancy.door = DoorOfEnterTask(ped);
            }
            else if (PedIsShuffling(ped)) {
                occupancy.seat = kDriverSeat;
            }
            else if (occupancy.leaving) {
                occupancy.door = DoorOfLeaveTask(ped);
            }

            return occupancy;
        }

        if (occupancy.entering) {
            occupancy.vehicle = EnterTargetOf(ped);
            occupancy.seat    = EnterSeatOf(ped);
            occupancy.door    = DoorOfEnterTask(ped);
        }
        else if (occupancy.leaving) {
            occupancy.door = DoorOfLeaveTask(ped);
        }

        return occupancy;
    }

    bool IsPedInVehicle(int slot) {
        return OccupiedVehicleOf(PedFor(slot)) != nullptr;
    }

    bool IsPedEnteringVehicle(int slot) {
        auto *ped = PedFor(slot);

        return PedIsEntering(ped) || PedIsShuffling(ped);
    }

    bool IsPedLeavingVehicle(int slot) {
        return PedIsLeaving(PedFor(slot));
    }

    bool IsPedOccupyingVehicle(int slot) {
        const auto occupancy = QueryPedOccupancy(slot);

        return occupancy.seated || occupancy.entering || occupancy.leaving;
    }

    bool EnterVehicle(int slot, void *handle, std::uint8_t seat, bool warp, std::uint8_t door) {
        auto *ped     = PedFor(slot);
        auto *vehicle = static_cast<CVehicle *>(handle);

        if (!ped || !vehicle || !ped->m_pIntelligence) {
            return false;
        }

        if (!SeatExists(vehicle, seat)) {
            return false;
        }

        if (OccupiedVehicleOf(ped) == vehicle && SeatIn(vehicle, ped) == seat) {
            return true;
        }

        // ALREADY WALKING IN. PrepareForTransition flushes the primary
        // task. Calling this again from the player snapshot - which arrives
        // every tick once vehicleNetworkId is set - would restart the
        // climb and leave them moonwalking at the door. MTA's GetIntoVehicle
        // is the same: occupying that vehicle is enough, do not rebuild
        // the task. Stamp the door if the first start did not have one -
        // that is what makes a driver who opened the passenger side shuffle
        // across instead of sitting there.
        if (EnterTargetOf(ped) == vehicle) {
            if (auto *primary = PrimaryTask(ped)) {
                ApplyEnterDoor(primary, door);
            }

            return true;
        }

        // Mid-shuffle they are seated in the passenger cushion with a
        // shuffle or enter task still running. Warping them out to start a
        // new driver climb is what killed the slide to seat 0.
        if (OccupiedVehicleOf(ped) == vehicle && (PedIsEntering(ped) || PedIsShuffling(ped))) {
            return true;
        }

        if (IsPedInVehicle(slot)) {
            ExitVehicle(slot, true);
        }

        PrepareForTransition(ped);

        if (seat == kDriverSeat) {
            if (warp) {
                alignas(void *) unsigned char storage[kSetInDriverBytes] {};

                reinterpret_cast<SetInDriverCtorFn>(gPatterns.CTaskSimpleCarSetPedInAsDriver__Constructor)(storage, vehicle, nullptr);
                reinterpret_cast<CTaskSimpleCarSetPedInAsDriver *>(storage)->m_bWarpingInToCar = true;
                ProcessSimpleTask(storage, ped);
            }
            else {
                auto *task = AllocateTask(kEnterDriverBytes);

                if (!task) {
                    return false;
                }

                reinterpret_cast<EnterDriverCtorFn>(gPatterns.CTaskComplexEnterCarAsDriver__Constructor)(task, vehicle);
                ApplyEnterDoor(task, door);
                SetPrimaryTask(ped, task);
            }

            return true;
        }

        const int passengerNumber = static_cast<int>(seat) - 1;
        int chosenDoor            = door == kInvalidDoor
                           ? reinterpret_cast<DoorAsPassengerFn>(gPatterns.CCarEnterExit__ComputeTargetDoorToEnterAsPassenger)(vehicle, passengerNumber)
                           : static_cast<int>(door);

        // 0 / 0xFF: this model has no door for that seat. Handing either to
        // the entry task starts a walk that never ends.
        if (chosenDoor == kNoDoor || chosenDoor == static_cast<int>(kInvalidDoor)) {
            return false;
        }

        if (warp) {
            alignas(void *) unsigned char storage[kSetInPassengerBytes] {};

            reinterpret_cast<SetInPassengerCtorFn>(gPatterns.CTaskSimpleCarSetPedInAsPassenger__Constructor)(storage, vehicle, chosenDoor, nullptr);
            reinterpret_cast<CTaskSimpleCarSetPedInAsPassenger *>(storage)->m_bWarpingInToCar = true;
            ProcessSimpleTask(storage, ped);
        }
        else {
            auto *task = AllocateTask(kEnterPassengerBytes);

            if (!task) {
                return false;
            }

            reinterpret_cast<EnterPassengerCtorFn>(gPatterns.CTaskComplexEnterCarAsPassenger__Constructor)(task, vehicle, chosenDoor, false);
            ApplyEnterDoor(task, door == kInvalidDoor ? static_cast<std::uint8_t>(chosenDoor) : door);
            SetPrimaryTask(ped, task);
        }

        return true;
    }

    void ExitVehicle(int slot, bool warp, std::uint8_t door) {
        auto *ped = PedFor(slot);

        if (!ped || !ped->m_pIntelligence) {
            return;
        }

        // MTA: REQUEST_OUT_CONFIRMED does not rebuild LEAVE_CAR if one is
        // already running. Restarting it every snapshot popped the door
        // shut and left them standing in the seat.
        if (!warp && PedIsLeaving(ped)) {
            return;
        }

        auto *vehicle = OccupiedVehicleOf(ped);

        if (!vehicle) {
            if (PedIsEntering(ped) || PedIsShuffling(ped)) {
                ClearPrimaryTask(ped);
                GiveDefaultPrimary(ped);
            }

            return;
        }

        ClearPrimaryTask(ped);
        ped->CantBeKnockedOffBike = 2u;

        int chosenDoor = door == kInvalidDoor
            ? reinterpret_cast<DoorToExitFn>(gPatterns.CCarEnterExit__ComputeTargetDoorToExit)(vehicle, ped)
            : static_cast<int>(door);

        if (warp) {
            alignas(void *) unsigned char storage[kSetOutBytes] {};

            reinterpret_cast<SetOutCtorFn>(gPatterns.CTaskSimpleCarSetPedOut__Constructor)(storage, vehicle, chosenDoor, false);
            reinterpret_cast<CTaskSimpleCarSetPedOut *>(storage)->m_bWarpingOutOfCar = true;
            ProcessSimpleTask(storage, ped);
            GiveDefaultPrimary(ped);
            return;
        }

        auto *task = AllocateTask(kLeaveCarBytes);

        if (!task) {
            return;
        }

        // sensible leave, not forced: the game can refuse an upside-down exit.
        reinterpret_cast<LeaveCarCtorFn>(gPatterns.CTaskComplexLeaveCar__Constructor)(task, vehicle, chosenDoor, 0, true, false);
        SetPrimaryTask(ped, task);
    }

    void CancelEntriesInto(void *handle) {
        auto *vehicle = static_cast<CVehicle *>(handle);

        if (!vehicle) {
            return;
        }

        auto cancel = [vehicle](CPed *ped) {
            if (!ped || !ped->m_pIntelligence) {
                return;
            }

            auto *primary = PrimaryTask(ped);

            if (!primary || !IsEnterTaskId(TaskIdOf(primary))) {
                return;
            }

            if (static_cast<CTaskComplexEnterCar *>(primary)->m_pTargetVehicle != vehicle) {
                return;
            }

            ClearPrimaryTask(ped);
            GiveDefaultPrimary(ped);
        };

        cancel(Internal::LocalPed());

        for (int slot = 2; slot < Game::kMaxPlayers; ++slot) {
            cancel(PedAt(slot));
        }
    }

    void UnlinkFromVehicle(int slot) {
        auto *ped = PedFor(slot);

        if (!ped) {
            return;
        }

        auto *vehicle = OccupiedVehicleOf(ped);

        if (!vehicle) {
            if (PedIsEntering(ped)) {
                ClearPrimaryTask(ped);
                GiveDefaultPrimary(ped);
            }

            return;
        }

        ExitVehicle(slot, true);

        if (vehicle->m_pDriver == ped) {
            vehicle->m_pDriver = nullptr;
        }

        for (auto &passenger : vehicle->m_apPassengers) {
            if (passenger == ped) {
                passenger = nullptr;
            }
        }

        ped->m_pVehicle = nullptr;
        ped->bInVehicle = false;
    }

    bool PassengerDoorDistance(int slot, void *vehicleHandle, float *outDistance) {
        auto *ped     = PedFor(slot);
        auto *vehicle = static_cast<CVehicle *>(vehicleHandle);

        if (!ped || !vehicle || !outDistance) {
            return false;
        }

        // NOT STREAMED IN MEANS NO MODEL TO ASK ABOUT.
        //
        // GetNearestCarPassengerDoor walks the door nodes. A pool entry
        // the streamer has not built yet has m_pRwObject == nullptr, and
        // that walk is a null deref inside GTA with nothing of ours on
        // the stack. MTA and old Vanta skip those for the same reason.
        if (!vehicle->m_pRwObject || vehicle->m_fHealth <= 0.0f) {
            return false;
        }

        using NearestDoorFn = bool(__cdecl *)(const CPed *, const CVehicle *, CVector *, int *, bool, bool, bool);

        CVector doorPosition {};
        int     doorId = 0;
        const bool haveDoor = reinterpret_cast<NearestDoorFn>(gPatterns.CCarEnterExit__GetNearestCarPassengerDoor)(
            ped, vehicle, &doorPosition, &doorId, false, false, false);

        const CVector pedPosition = ped->GetPosition();
        const CVector target      = haveDoor ? doorPosition : vehicle->GetPosition();
        const CVector offset      = target - pedPosition;

        *outDistance = std::sqrt((offset.x * offset.x) + (offset.y * offset.y) + (offset.z * offset.z));
        return true;
    }

    std::uint8_t FreePassengerSeat(const void *vehicleHandle) {
        const auto *vehicle = static_cast<const CVehicle *>(vehicleHandle);

        if (!vehicle) {
            return 0;
        }

        // 255 MEANS "UNKNOWN", NOT "TWO HUNDRED AND FIFTY-FIVE SEATS".
        //
        // GTA leaves m_nMaxPassengers at 0xFF until it has worked the
        // count out. Treating that as a length walks past m_apPassengers
        // and hands enter() a seat the server will refuse.
        const std::uint8_t declared = vehicle->m_nMaxPassengers;
        const std::size_t  seats    = declared == kUnknownPassengerCount
            ? static_cast<std::size_t>(kMaxPassengerSeats)
            : std::min<std::size_t>(declared, kMaxPassengerSeats);

        for (std::size_t index = 0; index < seats; ++index) {
            if (vehicle->m_apPassengers[index] == nullptr) {
                return static_cast<std::uint8_t>(index + 1u);
            }
        }

        return 0;
    }

    bool IsAnyoneEnteringVehicleAsDriver(const void *vehicle) {
        if (!vehicle) {
            return false;
        }

        auto enteringAsDriver = [vehicle](int slot) {
            const auto occupancy = QueryPedOccupancy(slot);

            return occupancy.entering && occupancy.vehicle == vehicle && occupancy.seat == kDriverSeat;
        };

        if (enteringAsDriver(0)) {
            return true;
        }

        for (int slot = 2; slot < Game::kMaxPlayers; ++slot) {
            if (enteringAsDriver(slot)) {
                return true;
            }
        }

        return false;
    }
} // namespace VantaMP::SDK
