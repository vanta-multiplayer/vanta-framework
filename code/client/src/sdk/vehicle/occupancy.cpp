#include "occupancy.h"

#include "sdk/game_sdk.h"
#include "sdk/patterns.h"
#include "sdk/ped/access.h"
#include "sdk/ped/remote_ped.h"

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
        using KnockOffCtorFn       = void *(__thiscall *)(void *, CVehicle *, const CVector *, const CVector *, float, float, unsigned char, unsigned char, int, CPed *, bool, bool);
        using EventGroupAddFn      = void *(__thiscall *)(void *, void *, bool);

        constexpr std::size_t kProcessPedSlot = 7;

        constexpr unsigned int kEnterDriverBytes    = 0x50;
        constexpr unsigned int kEnterPassengerBytes = 0x50;
        constexpr unsigned int kLeaveCarBytes       = 0x34;
        constexpr unsigned int kSetInDriverBytes    = 0x1C;
        constexpr unsigned int kSetInPassengerBytes = 0x20;
        constexpr unsigned int kSetOutBytes         = 0x18;

        // VALIDATE_SIZE(CEventKnockOffBike, 0x3C). CEventGroup::Add copies the
        // event into its own storage (it Clone()s), so a stack instance is how
        // GTA itself raises one - see CWorld.cpp:2008.
        constexpr unsigned int kKnockOffEventBytes  = 0x3C;

        // eKnockOffType. 53 = KNOCK_OFF_TYPE_FALL, the plain "came off the
        // bike" case; the others are explosion and skid variants.
        constexpr unsigned char kKnockOffTypeFall   = 53;

        constexpr int kNoDoor     = 0;
        constexpr int kNoSuchDoor = 0xFF;

        void (*gExplosionDeath)() = nullptr;

        CPed *PedFor(int slot) {
            return slot <= 0 ? Internal::LocalPed() : PedAt(slot);
        }

        // WHICH ENTER TASK WE HAVE ALREADY ASKED TO UNWIND, PER PED.
        //
        // See AbortVehicleEnter. Indexed by slot; slot <= 0 is the local ped.
        const void *&AbortLatchFor(int slot) {
            static const void *latches[Game::kMaxPlayers] {};
            static const void *discard = nullptr;

            const int index = slot <= 0 ? 0 : slot;

            if (index < 0 || index >= Game::kMaxPlayers) {
                discard = nullptr;
                return discard;
            }

            return latches[index];
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
            // climb.
            reinterpret_cast<FlushTasksFn>(gPatterns.CPedIntelligence__FlushImmediately)(ped->m_pIntelligence, false);

            // KNOCK-OFF IMMUNITY IS FOR PUPPETS ONLY. MTA'S EXACT RULE:
            //
            //   // Set remote players to not fall off bikes locally, let them decide
            //   if (m_bIsLocalPlayer)
            //       SetCanBeKnockedOffBike(m_bCanBeKnockedOffBike);
            //   else
            //       SetCanBeKnockedOffBike(false);
            //
            // (CClientPed.cpp:5799-5802; false maps to BIKE_KNOCK_OFF_NEVER at
            // 3023, the same 1 = Never in CPed.h:171.)
            //
            // "let them decide" is the whole design. A REMOTE ped must not
            // decide for itself when it falls: each viewer would decide
            // differently from their own copy of the collision, with no
            // authority to reconcile them. The OWNER falls, and the exit RPC
            // tells everyone else.
            //
            // Setting it here unconditionally made the LOCAL ped immune too -
            // so nobody could ever be knocked off, on any machine, and the
            // whole path was dead. 0 = Default is what GTA ships with and what
            // CGameLogic restores on resurrection (GameLogic.cpp:309).
            if (ped != Internal::LocalPed()) {
                ped->CantBeKnockedOffBike = 1u;
            }
            else {
                ped->CantBeKnockedOffBike = 0u;
            }
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

        // GTA's door TARGET ids are not the CDoor array indices the damage
        // and ratio code uses. Enter/leave tasks speak 0/8/9/10/11; the
        // m_doors array is 2-5 for the four passenger doors. 10 is the same
        // front-left as 0 - MTA maps both the same way.
        std::uint8_t DoorRatioIndexFromTarget(std::uint8_t target) {
            switch (target) {
            case 0:
            case 10:
                return 2;
            case 8:
                return 3;
            case 11:
                return 4;
            case 9:
                return 5;
            default:
                return kInvalidDoor;
            }
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

        void DetachPedFromVehicle(CPed *ped, CVehicle *vehicle) {
            if (!ped) {
                return;
            }

            // NO LEAVE TASK. SetPedOut / LeaveCar during BlowUpCar re-enters
            // vehicle processing on a wreck and is itself a crash. Clear the
            // pointers and drop the enter/leave task.
            if (ped->m_pIntelligence) {
                reinterpret_cast<FlushTasksFn>(gPatterns.CPedIntelligence__FlushImmediately)(ped->m_pIntelligence, false);
            }

            if (vehicle) {
                if (vehicle->m_pDriver == ped) {
                    vehicle->m_pDriver = nullptr;
                }

                for (auto &passenger : vehicle->m_apPassengers) {
                    if (passenger == ped) {
                        passenger = nullptr;
                    }
                }
            }

            ped->m_pVehicle = nullptr;
            ped->bInVehicle = false;
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

    void ExitVehicle(int slot, bool warp, std::uint8_t door, bool knockedOff) {
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
            // NOT SEATED, SO THERE IS NOTHING TO GET OUT OF.
            //
            // This is the abort case: the ped is still walking to the door or
            // part way through the climb. It used to ClearPrimaryTask +
            // GiveDefaultPrimary, which is the same pair that froze the local
            // player - slot 3 emptied, the replacement installed into slot 4,
            // and the event handler flushed along the way
            // (PedIntelligence.cpp:479-481, 511-524).
            //
            // It also caused the second symptom. Nulling the enter task skips
            // its unwind, and the unwind is what plays the door SHUTTING. The
            // remote copy was left with the door hanging open wherever the
            // animation happened to be, because the animation never finished -
            // it was deleted.
            //
            // Asking the task to unwind instead fixes both: the ped returns to
            // foot through GTA's own path, and closes the door on the way, the
            // same as it does on the owner's machine.
            AbortVehicleEnter(slot);
            return;
        }

        // THE KNOCK-OFF PATH MUST REACH THE EVENT UNTOUCHED.
        //
        // ClearPrimaryTask nulls slot 3 with no replacement - the freeze this
        // file has already been bitten by twice - and CEventKnockOffBike
        // installs its own task anyway. Setting CantBeKnockedOffBike here is
        // worse than pointless: AffectsPed reads that flag, and although
        // m_forceKnockOff overrides it, there is no reason to set it on a ped
        // we are about to knock off.
        if (!knockedOff) {
            ClearPrimaryTask(ped);
            ped->CantBeKnockedOffBike = 2u;
        }

        int chosenDoor = door == kInvalidDoor
            ? reinterpret_cast<DoorToExitFn>(gPatterns.CCarEnterExit__ComputeTargetDoorToExit)(vehicle, ped)
            : static_cast<int>(door);

        if (knockedOff && gPatterns.CEventKnockOffBike__Constructor && gPatterns.CEventGroup__Add) {
            // RAISE GTA'S OWN EVENT AND LET IT DO ALL OF IT.
            //
            // Two earlier attempts got this wrong by trying to reproduce the
            // knock-off by hand. The first built a CTaskSimpleCarFallOut with
            // 0 for its third argument and crashed at 0x6513C5 - that argument
            // is a CTaskUtilityLineUpPedWithCar*, and
            // CTaskSimpleCarFallOut::SetPedPosition dereferences it with no
            // null check. CarFallOut is also the wrong task entirely: it is
            // the CAR fall-out, driven by CTaskComplexEnterCar, and a
            // knock-off never uses it.
            //
            // What GTA actually does is CEventKnockOffBike, and the response
            // is three separate steps that all have to happen together
            // (EventKnockOffBike.cpp):
            //
            //   SetPedOutCar        0x4AFF60 - CTaskSimpleCarSetPedOut with
            //                                  m_bKnockedOffBike, then
            //                                  RemoveCarSitAnim
            //   CalcForcesAndAnims  0x4B0020 - ApplyMoveForce off the bike's
            //                                  Up/Right vectors, and returns
            //                                  the anim (ANIM_ID_BIKE_FALLR,
            //                                  KO_SPIN_L/R, KD_LEFT/RIGHT,
            //                                  KO_SKID_BACK, FALL_FALL)
            //   SetPedSafePosition  0x4B4AC0 - clears the bike's LeanAngle and
            //                                  m_bLeanMatrixCalculated
            //
            // Doing those by hand means picking knockOffType and direction
            // ourselves and calling an unreversed responder
            // (CEventHandler::ComputeKnockOffBikeResponse, 0x4B9FF0, marked
            // reversed=false) to play the anim. Adding the event to the ped's
            // own event group runs the whole thing the way the game does,
            // including the parts nobody has reversed.
            //
            // m_forceKnockOff = true is the bit that matters for us:
            // CEventKnockOffBike::AffectsPed returns false for a ped with
            // CantBeKnockedOffBike set UNLESS this is on, and every remote ped
            // has that flag (see PrepareForTransition). The owner already
            // decided they fell; this says "do it anyway".
            alignas(void *) unsigned char event[kKnockOffEventBytes] {};

            const CVector moveSpeed = vehicle->m_vecMoveSpeed;
            const CVector impact    = vehicle->m_vecMoveSpeed;

            reinterpret_cast<KnockOffCtorFn>(gPatterns.CEventKnockOffBike__Constructor)(
                event, vehicle, &moveSpeed, &impact,
                0.0f,                        // damageIntensity
                0.0f,                        // field_28
                kKnockOffTypeFall,           // KNOCK_OFF_TYPE_FALL
                0,                           // direction
                0,                           // time
                ped,
                vehicle->m_pDriver == ped,   // isVictimDriver
                true);                       // forceKnockOff - see above

            reinterpret_cast<EventGroupAddFn>(gPatterns.CEventGroup__Add)(&ped->m_pIntelligence->m_eventGroup, event, false);
            return;
        }

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

    // CANCELLING AN ENTRY IS AN ABORT, NOT A SLOT WIPE.
    //
    // This used to be ClearPrimaryTask + GiveDefaultPrimary, and it froze the
    // player. Both halves were wrong, for the same reason.
    //
    // The slots are not interchangeable (CTaskManager.h):
    //
    //   TASK_PRIMARY_PRIMARY = 3      TASK_PRIMARY_DEFAULT = 4
    //
    // ClearPrimaryTask nulls slot 3. GiveDefaultPrimary calls
    // FlushImmediately(true), which - PedIntelligence.cpp:511-524 - installs
    // its replacement into slot 4, NOT the slot that was just emptied. So the
    // enter task is torn out from under a ped that is mid-animation and
    // nothing takes over the slot that was driving it.
    //
    // Worse, FlushImmediately also wipes the event group and event handler
    // (PedIntelligence.cpp:479-481). That is what leaves the player unable to
    // move: their input is delivered as events, and the handler that would
    // turn those into tasks has just been flushed. They stand there.
    //
    // MTA never nulls a live slot. It calls MakeAbortable and lets the task
    // unwind itself (CClientPed.cpp:7798-7841 is the pattern, repeated at
    // every abort site). CTaskComplexEnterCar's own abort path puts the ped
    // back on foot properly, restoring the walk it interrupted - which is
    // exactly the behaviour we want and were destroying by hand.
    namespace {
        // Ask a ped's enter/leave task to unwind. The one correct way to end
        // one of these: it plays the step-back AND the door close, and hands
        // the ped back to foot through GTA's own path. Nulling the slot skips
        // all of that - see AbortVehicleEnter below for the full story.
        void UnwindPrimaryTask(CPed *ped) {
            if (!ped || !ped->m_pIntelligence) {
                return;
            }

            auto *task = PrimaryTask(ped);

            if (!task) {
                return;
            }

            using MakeAbortableFn = bool(__thiscall *)(void *, CPed *, int, void *);

            reinterpret_cast<MakeAbortableFn>(TaskVirtual(task, Internal::kTaskMakeAbortableSlot))(task, ped, ABORT_PRIORITY_IMMEDIATE, nullptr);
        }
    } // namespace

    void AbortVehicleEnter(int slot) {
        auto *ped = PedFor(slot);

        if (!ped || !ped->m_pIntelligence) {
            return;
        }

        if (!PedIsEntering(ped) && !PedIsShuffling(ped)) {
            // The unwind finished. Drop the latch so a genuinely new entry -
            // which may well land on the same freed address - can be aborted.
            AbortLatchFor(slot) = nullptr;
            return;
        }

        auto *task = PrimaryTask(ped);

        if (!task) {
            AbortLatchFor(slot) = nullptr;
            return;
        }

        // ABORT EACH TASK ONCE. THIS IS WHY THE ABORT LOOKED GLITCHY.
        //
        // MakeAbortable does not remove the task - it asks it to unwind, and
        // CTaskComplexEnterCar takes several frames to play the step-back and
        // hand the ped to its replacement. For all of those frames the ped is
        // still "entering", so every caller that polls that condition called
        // straight back in.
        //
        // PulseOccupancyBlocks does exactly that: while gBlockEnterUntilRelease
        // is set it re-aborts every frame for as long as occupancy.entering
        // reads true. Each call restarts the unwind from the top, so the ped
        // replayed the first frames of the exit animation over and over and
        // never got far enough through it to finish - the stutter.
        //
        // MTA never re-issues an abort either. It fires once and then WATCHES
        // for the task to disappear (CClientPed.cpp:10336-10341, "if we aren't
        // working on entering the car... "), reacting to the result instead of
        // pushing again.
        //
        // Keyed on the task pointer, not a bool: if the ped starts a genuinely
        // new entry later, that is a different allocation and must be
        // abortable. The latch is cleared as soon as the ped stops entering
        // (see AbortLatchFor's callers), so an address reused by a later task
        // cannot inherit it.
        auto &latch = AbortLatchFor(slot);

        if (latch == task) {
            return;
        }

        latch = task;

        using MakeAbortableFn = bool(__thiscall *)(void *, CPed *, int, void *);

        // IMMEDIATE, not URGENT. An urgent abort lets the task finish its
        // current animation before unwinding, and the whole point here is
        // that the player pressed a key to get out NOW.
        reinterpret_cast<MakeAbortableFn>(TaskVirtual(task, Internal::kTaskMakeAbortableSlot))(task, ped, ABORT_PRIORITY_IMMEDIATE, nullptr);
    }



    void AbortVehicleExit(int slot, void *handle, std::uint8_t seat) {
        auto *ped     = PedFor(slot);
        auto *vehicle = static_cast<CVehicle *>(handle);

        if (!ped || !ped->m_pIntelligence) {
            return;
        }

        if (OccupiedVehicleOf(ped) == vehicle && vehicle) {
            // SAME TWO BUGS AS THE ENTER ABORT, SAME TWO FIXES.
            //
            // ClearPrimaryTask nulled slot 3 outright, leaving the ped with
            // no primary task while still in the seat. And PulseOccupancyBlocks
            // calls this every frame for as long as the ped reads as leaving,
            // so the leave task was being torn out repeatedly - the animation
            // restarting each time.
            //
            // Ask it to unwind once and let it finish, exactly as MTA does.
            if (PedIsLeaving(ped)) {
                auto *task = PrimaryTask(ped);

                if (task) {
                    auto &latch = AbortLatchFor(slot);

                    if (latch != task) {
                        latch = task;

                        using MakeAbortableFn = bool(__thiscall *)(void *, CPed *, int, void *);

                        reinterpret_cast<MakeAbortableFn>(TaskVirtual(task, Internal::kTaskMakeAbortableSlot))(task, ped, ABORT_PRIORITY_IMMEDIATE, nullptr);
                    }
                }
            }
            else {
                AbortLatchFor(slot) = nullptr;
            }

            return;
        }

        if (vehicle) {
            EnterVehicle(slot, vehicle, seat, true);
        }
    }

    // IS GTA CURRENTLY THROWING THIS PED OFF A BIKE, OR PICKING THEM UP?
    //
    // The knock-off is not instantaneous: CEventKnockOffBike ejects the ped,
    // applies a launch force, and hands them to a fall/get-up task that plays
    // for a second or more. For that whole time the ped's position is GTA's -
    // they are tumbling under physics from the force it applied - and the
    // network must not correct it, or the tumble is fought frame by frame.
    //
    // Detected purely from the task slots the fall lives in - see the note at
    // the return below for why ped->bKnockedOffBike is not used.
    bool PedIsFallingOffBike(int slot) {
        auto *ped = PedFor(slot);

        if (!ped || !ped->m_pIntelligence) {
            return false;
        }

        // EVERY PRIMARY SLOT, NOT JUST TASK_PRIMARY_PRIMARY.
        //
        // This is why the stand-up did not always play. An event's response
        // task does NOT go into the primary slot: CEventHandler::
        // SetEventResponseTask (EventHandler.cpp:448-455) installs it via
        // AddTaskEventResponseTemp / AddTaskEventResponseNonTemp, i.e.
        // TASK_PRIMARY_EVENT_RESPONSE_TEMP (1) or _NONTEMP (2). Checking only
        // slot 3 missed the fall entirely, so the network kept correcting
        // position and heading right through it - which is exactly what
        // stopped the get-up from playing out.
        //
        // Scanning all four costs nothing and cannot miss whichever slot the
        // handler chose.
        for (auto *task : ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks) {
            if (!task) {
                continue;
            }

            switch (TaskIdOf(task)) {
            case TASK_SIMPLE_FALL:
            case TASK_COMPLEX_FALL_AND_GET_UP:
            case TASK_COMPLEX_FALL_AND_STAY_DOWN:
            case TASK_SIMPLE_GET_UP:
            case TASK_COMPLEX_GET_UP_AND_STAND_STILL:
                return true;
            default:
                break;
            }
        }

        // DELIBERATELY NOT ped->bKnockedOffBike.
        //
        // That flag is set by CalcForcesAndAnims and nothing in the reversed
        // source ever clears it, so trusting it would exclude the ped from
        // position correction FOREVER after their first fall - a far worse
        // desync than the one this guard exists to prevent. The task slots
        // above are self-limiting: they disappear when the ped is back on
        // their feet, which is exactly the window we want.
        return false;
    }

    std::uint8_t DoorRatioIndexForTarget(std::uint8_t target) {
        return DoorRatioIndexFromTarget(target);
    }

    // EVERY DOOR CURRENTLY BEING USED BY A PED, LOCAL OR REMOTE.
    //
    // MTA blocks door-ratio writes for exactly this case. SetDoorOpenRatio
    // (CClientVehicle.cpp:709-747) refuses a non-forced write when the door
    // belongs to an occupying ped, and AllowDoorRatioSetting is cleared the
    // moment a ped is paired with a door (CClientVehicle.cpp:4380) and
    // restored when they finish (4322-4324).
    //
    // The reason is the sound. CVehicle::OpenDoor plays the swing/slam even
    // with playSound false, so writing a ratio to a door that the enter or
    // leave animation is already driving re-triggers it - and since the wire
    // ratio and the animation disagree for the whole climb, that is once per
    // packet for as long as it lasts. That is the door spam.
    //
    // We already skipped the LOCAL player's door, via QueryPedOccupancy(0).
    // A remote player entering or leaving was never masked at all, so their
    // door was written over on every packet. This covers both.
    std::uint8_t OccupiedDoorMask(void *handle) {
        auto *vehicle = static_cast<CVehicle *>(handle);

        if (!vehicle) {
            return 0;
        }

        std::uint8_t mask = 0;

        auto add = [vehicle, &mask](CPed *ped) {
            if (!ped || !ped->m_pIntelligence) {
                return;
            }

            const bool entering = PedIsEntering(ped) || PedIsShuffling(ped);
            const bool leaving  = PedIsLeaving(ped);

            if (!entering && !leaving) {
                return;
            }

            // Only this vehicle's doors. A ped climbing into the car parked
            // next to it must not freeze ours.
            if (EnterTargetOf(ped) != vehicle && OccupiedVehicleOf(ped) != vehicle) {
                return;
            }

            const std::uint8_t door = entering ? DoorOfEnterTask(ped) : DoorOfLeaveTask(ped);
            const std::uint8_t index = DoorRatioIndexFromTarget(door);

            if (index == kInvalidDoor) {
                return;
            }

            mask |= static_cast<std::uint8_t>(1u << index);
        };

        add(Internal::LocalPed());

        // The occupants first - cheap, and covers the common case of someone
        // getting out of a car we can see.
        if (vehicle->m_pDriver) {
            add(vehicle->m_pDriver);
        }

        for (auto *passenger : vehicle->m_apPassengers) {
            add(passenger);
        }

        // Then anyone walking TOWARD it, who is not an occupant yet.
        for (int slot = 2; slot < Game::kMaxPlayers; ++slot) {
            auto *ped = PedAt(slot);

            if (ped && EnterTargetOf(ped) == vehicle) {
                add(ped);
            }
        }

        return mask;
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

            // Unwind, do not delete. Deleting the task skips the door
            // close that is part of it - the remote copy was left with a
            // door hanging open wherever the animation stopped.
            UnwindPrimaryTask(ped);
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
                UnwindPrimaryTask(ped);
            }

            return;
        }

        // Same as EvacuateVehicle. SetPedOut / LeaveCar is a gameplay
        // exit. Teardown, a wreck, or a dead ped must not re-enter
        // the car through a warp-out task.
        DetachPedFromVehicle(ped, vehicle);
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

    void SetVehicleExplosionDeathHandler(void (*handler)()) {
        gExplosionDeath = handler;
    }

    void EvacuateVehicle(void *handle, bool killOccupants) {
        auto *vehicle = static_cast<CVehicle *>(handle);

        if (!vehicle) {
            return;
        }

        CancelEntriesInto(vehicle);

        CPed *occupants[1 + kMaxPassengerSeats] {};
        std::size_t count = 0;

        if (vehicle->m_pDriver) {
            occupants[count++] = vehicle->m_pDriver;
        }

        for (auto *passenger : vehicle->m_apPassengers) {
            if (passenger) {
                occupants[count++] = passenger;
            }
        }

        for (std::size_t i = 0; i < count; ++i) {
            auto *ped = occupants[i];
            int   slot = -1;

            if (ped == Internal::LocalPed()) {
                slot = 0;
            }
            else {
                for (int candidate = 2; candidate < Game::kMaxPlayers; ++candidate) {
                    if (PedAt(candidate) == ped) {
                        slot = candidate;
                        break;
                    }
                }
            }

            if (killOccupants && slot >= 0) {
                // IsAlive() is the DIE/DEAD state, not health. Leave them
                // IDLE at 0 and the explosion that follows still treats
                // them as a live CPlayerPed.
                ped->m_fHealth    = 0.0f;
                ped->m_fArmour    = 0.0f;
                ped->m_ePedState  = PEDSTATE_DEAD;

                if (slot == 0) {
                    if (gExplosionDeath) {
                        gExplosionDeath();
                    }
                }
                else {
                    SetRemotePedHealth(slot, 0.0f, 0.0f);
                }
            }

            // Pointers only. SetPedOut re-enters the vehicle while it
            // is exploding. UnlinkFromVehicle is the same detach.
            DetachPedFromVehicle(ped, vehicle);
        }
    }
} // namespace VantaMP::SDK
