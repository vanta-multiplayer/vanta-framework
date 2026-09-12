#include "game/sync/vehicle_sync.h"

#include "game/sync/player_sync.h"
#include "game/sync/vehicle/handler.h"
#include "sdk/game.h"
#include "sdk/input/input.h"
#include "sdk/world/streaming.h"
#include "shared/entity/player_entity.h"
#include "shared/world/world_space.h"
#include "shared/rpc/player/enter_vehicle.h"
#include "shared/rpc/player/exit_vehicle.h"
#include "shared/rpc/player/occupancy_abort.h"
#include "shared/rpc/vehicle/damage.h"
#include "shared/rpc/vehicle/driver_sync.h"
#include "shared/rpc/vehicle/unoccupied_sync.h"
#include "shared/rpc/common/metrics.h"
#include "shared/rpc/common/wire_registry.h"
#include "shared/sync_rates.h"
#include "shared/vehicle/colors.h"
#include "shared/entity/vehicle_entity.h"
#include "shared/vehicle/upgrades.h"

#include <utils/safe_win32.h>

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>
#include <utils/interpolator.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace VantaMP::Game {
    namespace {
        namespace Replication = Framework::Networking::Replication;

        struct RemoteVehicle {
            void                         *handle = nullptr;
            Framework::Utils::Interpolator interpolator;
            glm::vec3                      aimedPosition {};
            glm::quat                      aimedRotation = glm::identity<glm::quat>();
            float                          speed        = 0.0f;
            std::uint32_t                  whenMs       = 0;
            bool                           aimed        = false;

            glm::vec3 velocity {};
            glm::vec3 angular {};
            float     health   = 1000.0f;
            bool      engineOn = true;
            bool      sirenOn  = false;

            std::int8_t  steer       = 0;
            std::uint8_t accelerator = 0;
            std::uint8_t brake       = 0;
            bool         handbrake   = false;
            std::int8_t  gasPedal    = 0;
            std::int8_t  brakePedal  = 0;

            std::uint8_t  lastEngine  = 0;
            std::uint64_t lastDoors   = 0;
            std::uint32_t lastWheels  = 0;
            std::uint32_t lastPanels  = 0;
            std::uint32_t lastLights  = 0;
            std::uint64_t lastRatios       = 0;
            std::uint64_t lastEntityRatios = 0;
            std::uint64_t heldRatios       = 0;
            float         doorStart[6]     {};
            float         doorTarget[6]    {};
            std::uint32_t doorInterpMs     = 0;
            bool          doorInterp       = false;
            bool          haveDamage  = false;
            bool          wrecked     = false;
            std::uint32_t lastDamageReportMs = 0;
            std::uint32_t lastRatioReportMs  = 0;
            std::uint64_t lastSentRatios     = 0;

            // GetTickCount() when the local ped stopped using a door on this
            // vehicle. Reporting continues for a short while afterwards - see
            // ReportOccupantDoorRatios.
            std::uint32_t doorReleasedMs = 0;

            std::uint8_t  lastPaintjob  = 255;
            std::array<std::uint32_t, Shared::kVehicleColorCount> lastColours {};
            bool haveColours = false;
            std::array<std::uint16_t, Shared::kUpgradeSlots> lastUpgrades {};

            bool         haveScriptState     = false;
            bool         lastEngineOn        = true;
            bool         lastSirenOn         = false;
            std::uint8_t lastOverrideLights  = 0;
            bool         lastLocked          = false;
            std::uint8_t lastDirt            = 255;
            bool         lastTaxiLight       = false;
            std::uint32_t lastHeadlight      = 0;
            bool         lastFrozen          = false;
            bool         lastDamageProof     = false;
            bool         lastCollisions      = true;
            bool         lastLandingGear     = true;
            bool         lastLightsOn        = false;
            bool         lastEngineBroken    = false;
            bool         lastWheelsCanBurst  = true;
            bool         lastCanBeDamaged    = true;
            std::uint8_t lastNitroCount      = 0;
            std::string  lastPlateText;
            std::uint8_t lastVariant         = 255;
            std::uint8_t lastVariant2        = 255;
            bool         haveExtras          = false;
            bool         haveVariants        = false;

            // --- Outbound driver sync (owner side only) ---
            //
            // The last state actually PUT ON THE WIRE, not the last state
            // read from GTA. A delta has to compare against what the
            // receiver believes, or a field that drifts by less than its
            // threshold every tick is never sent at all and the two sides
            // walk apart for ever.
            std::uint16_t sentSequence = 0;
            glm::vec3     sentPosition {};
            glm::quat     sentRotation = glm::identity<glm::quat>();
            glm::vec3     sentVelocity {};
            glm::vec3     sentAngular {};
            float         sentHealth   = -1.0f;
            std::int8_t   sentSteer    = 0;
            std::uint8_t  sentAccel    = 0;
            std::uint8_t  sentBrake    = 0;
            std::int8_t   sentGas      = 0;
            std::int8_t   sentBrakePedal = 0;
            std::uint8_t  sentFlags    = 0xFF;
            std::uint32_t lastDriverSyncMs  = 0;
            std::uint32_t lastKeyframeMs    = 0;
            bool          haveSentDriver    = false;

            // --- Unoccupied syncer role ---
            //
            // True only while the server has named THIS client the syncer for
            // this vehicle. Never inferred locally: two clients each deciding
            // they are nearest is exactly the double-owner bug the handoff
            // exists to prevent.
            bool isSyncer = false;

            // Last state this client put on the wire AS SYNCER. Separate from
            // the driver-sync baseline above: the two roles are mutually
            // exclusive for a given vehicle, but a car can pass between them
            // and each must start from its own clean comparison.
            glm::vec3 syncedPosition {};
            glm::quat syncedRotation = glm::identity<glm::quat>();
            glm::vec3 syncedVelocity {};
            glm::vec3 syncedAngular {};
            float     syncedHealth    = -1.0f;
            bool      haveSynced      = false;
            std::uint32_t lastSyncerKeyframeMs = 0;

            // --- Inbound driver sync (receiver side) ---
            //
            // The newest sequence actually APPLIED, so a reordered datagram
            // can be dropped. Per vehicle, not per channel: a sequenced RakNet
            // channel would let one car's packets discard another's.
            std::uint16_t appliedSequence     = 0;
            bool          haveAppliedSequence = false;
        };

        std::unordered_map<std::uint64_t, RemoteVehicle> gVehicles;

        // Canonical NetworkID <-> compact wire id. The client only ever
        // learns mappings; it never mints one (see wire_registry.h).
        Shared::RPC::Common::WireRegistry gWireIds;

        // MTA's stream-in warp: a ped that appeared already in a seat,
        // remembered until the car exists and they are put in it.
        std::unordered_map<int, bool> gLateJoinWarp;

        void RememberLateJoin(int slot) {
            gLateJoinWarp[slot] = true;
        }

        void ForgetLateJoin(int slot) {
            gLateJoinWarp.erase(slot);
        }

        [[nodiscard]] bool ConsumeLateJoin(int slot) {
            const auto found = gLateJoinWarp.find(slot);

            return found != gLateJoinWarp.end() && found->second;
        }

        enum class LocalRide { OnFoot, Entering, Seated, Leaving };

        LocalRide gLocalRide = LocalRide::OnFoot;

        // First frame of a walk-in the enter task still has door 0, which is
        // also FRONT_LEFT. Waiting one tick lets CreateFirstSubTask write the
        // door the owner actually walked to, so remotes shuffle when that
        // door is the passenger side.
        bool gAwaitEnterDoor = false;

        // Stick held when the climb started must not cancel it — that is
        // just walking up to the door. A new press, a jump, or a release
        // then press is the player changing their mind.
        bool gBlockEnterUntilRelease = false;
        bool gBlockExitUntilRelease  = false;

        constexpr float kAbortStick = 48.0f;

        [[nodiscard]] float StickMagnitude(const SDK::ControllerInput &input) {
            const auto x = static_cast<float>(input.leftStickX);
            const auto y = static_cast<float>(input.leftStickY);

            return std::sqrt(x * x + y * y);
        }

        [[nodiscard]] bool OccupancyInputIdle() {
            const auto input = SDK::ReadLocalInput();

            return (input.buttons & SDK::Button::EnterExit) == 0 && (input.buttons & SDK::Button::Jump) == 0 && StickMagnitude(input) <= kAbortStick;
        }

        // ONLY REACHED AFTER THE SERVER REFUSED AN ENTRY OR EXIT.
        //
        // The local cancel path no longer aborts anything - see the Entering
        // case below - so the only thing that sets these flags now is
        // ApplyOccupancyAbort, i.e. the server saying "you may not have that
        // seat". Re-issuing the abort while the player keeps the key held is
        // correct there: GTA will happily start a fresh enter task the moment
        // the old one dies, and the server has already said no.
        //
        // SDK::AbortVehicleEnter latches on the task pointer, so a task that
        // is already unwinding is not asked twice; a genuinely new task -
        // which is exactly what this is guarding against - still gets one
        // abort of its own.
        void PulseOccupancyBlocks(const SDK::PedOccupancy &occupancy) {
            if (gBlockEnterUntilRelease) {
                if (occupancy.entering) {
                    SDK::AbortVehicleEnter(0);
                }

                if (OccupancyInputIdle()) {
                    gBlockEnterUntilRelease = false;
                }
            }

            if (gBlockExitUntilRelease) {
                if (occupancy.leaving) {
                    SDK::AbortVehicleExit(0, occupancy.vehicle, occupancy.seat);
                }

                if (OccupancyInputIdle()) {
                    gBlockExitUntilRelease = false;
                }
            }
        }

        void *HandleOf(std::uint64_t vehicleId) {
            const auto found = gVehicles.find(vehicleId);

            return found != gVehicles.end() ? found->second.handle : nullptr;
        }

        void SendLocalEnter(std::uint64_t vehicleId, std::uint8_t seat, bool warp, std::uint8_t door) {
            auto *net = Framework::CoreModules::GetNetworkPeer();

            if (!net) {
                return;
            }

            Shared::RPC::PlayerEnterVehicle enter;
            enter.FromParameters(0, vehicleId, seat, warp, door);
            net->BroadcastRPC(enter);

            Framework::Logging::GetLogger("Vehicle")->debug("local enter vehicle {} seat {} door {} warp {}", vehicleId, seat, door, warp);
        }

        void SendLocalExit(bool warp, std::uint8_t door, bool abortEnter = false, void *vehicle = nullptr, bool knockedOff = false) {
            auto *net = Framework::CoreModules::GetNetworkPeer();

            if (!net) {
                return;
            }

            // MTA writes GetDoorOpenRatio(enteringDoor) into the notify-abort
            // (CClientPed.cpp:10443) so the receiver has an authoritative
            // angle for a door nobody else can observe. Same idea here: this
            // is the angle at the moment the entry ended. The settle window in
            // ReportOccupantDoorRatios then reports the swing that follows.
            std::uint8_t ratio = 0;

            if (abortEnter && vehicle) {
                const auto index = SDK::DoorRatioIndexForTarget(door);

                if (index != SDK::kInvalidDoor) {
                    const auto state = SDK::ReadVehicle(vehicle);

                    ratio = static_cast<std::uint8_t>((state.doorRatiosPacked >> (index * 8)) & 0xFFu);
                }
            }

            Shared::RPC::PlayerExitVehicle exit;
            exit.FromParameters(0, warp, door, abortEnter, ratio, knockedOff);
            net->BroadcastRPC(exit);

            Framework::Logging::GetLogger("Vehicle")->debug("local exit door {} warp {} abortEnter {}", door, warp, abortEnter);
        }

        void WatchLocalOccupancy() {
            const auto occupancy = SDK::QueryPedOccupancy(0);
            const auto vehicleId = VehicleNetworkIdOf(occupancy.vehicle);

            PulseOccupancyBlocks(occupancy);

            switch (gLocalRide) {
            case LocalRide::OnFoot:
                if (gBlockEnterUntilRelease) {
                    gAwaitEnterDoor = false;
                    break;
                }

                if (occupancy.seated && vehicleId != 0) {
                    gAwaitEnterDoor = false;
                    SendLocalEnter(vehicleId, occupancy.seat, true, occupancy.door);
                    gLocalRide = LocalRide::Seated;
                }
                else if (occupancy.entering && vehicleId != 0) {
                    if (!gAwaitEnterDoor) {
                        gAwaitEnterDoor = true;
                        break;
                    }

                    gAwaitEnterDoor = false;
                    SendLocalEnter(vehicleId, occupancy.seat, false, occupancy.door);
                    gLocalRide = LocalRide::Entering;
                }
                else {
                    gAwaitEnterDoor = false;
                }
                break;

            case LocalRide::Entering:
                gAwaitEnterDoor = false;

                if (occupancy.seated) {
                    // JACK COMPLETES HERE. The walk packet left the
                    // occupant in the seat; this seated packet is what
                    // lets the server throw them out and give us the car.
                    if (vehicleId != 0) {
                        SendLocalEnter(vehicleId, occupancy.seat, true, occupancy.door);
                    }

                    gLocalRide = LocalRide::Seated;
                }
                else if (!occupancy.entering) {
                    // THE ONLY WAY AN ENTRY ENDS. WE OBSERVE IT, WE DO NOT
                    // CAUSE IT.
                    //
                    // There used to be a second branch here that watched for
                    // jump / stick input, called AbortVehicleEnter and forced
                    // the state to OnFoot. That was a duplicate of something
                    // GTA already does: CPlayerPed's enter task ends itself
                    // when the player jumps or steers away - that is what
                    // bHasDisplayedPlayerQuitEnterCarHelpText in the save
                    // structure is the help text FOR. So on the cancel frame
                    // two mechanisms tore at the same task: GTA unwinding it
                    // normally, and our MakeAbortable restarting the unwind.
                    // That is the glitching, and no amount of latching the
                    // abort fixes it, because the abort should not be there.
                    //
                    // MTA does exactly what is left here. It never intercepts
                    // the cancel key at all - there is no abort call anywhere
                    // in its local enter path. It polls IsEnteringVehicle()
                    // (CClientPed.cpp:8272) and, the frame the task is gone,
                    // asks GetRealOccupiedVehicle() whether the ped ended up
                    // in a seat or on the pavement (10336-10420). Seat means
                    // notify-in; no seat means notify-abort. The cancel is
                    // GTA's business; the network's business is reporting
                    // which of the two happened.
                    SendLocalExit(false, occupancy.door, true, occupancy.vehicle);
                    gLocalRide = LocalRide::OnFoot;
                }
                break;

            case LocalRide::Seated:
                if (gBlockExitUntilRelease) {
                    break;
                }

                if (occupancy.leaving) {
                    SendLocalExit(false, occupancy.door);
                    gLocalRide = LocalRide::Leaving;
                }
                else if (!occupancy.seated && !occupancy.entering) {
                    // THROWN OUT, NOT WALKED OUT.
                    //
                    // Seated one frame, on the pavement the next, with no
                    // leave task in between. Nobody asked for this - GTA did
                    // it. A crash on a bike raises CEventKnockOffBike
                    // (World.cpp:2008 constructs one for explosions;
                    // CEventHandler::ComputeKnockOffBikeResponse is the
                    // handler MTA hooks at 0x4BA06F), and the ped is ejected
                    // without ever running CTaskComplexLeaveCar.
                    //
                    // This used to send warp=false, the same as a deliberate
                    // exit, which was wrong twice over. The server fires
                    // vehicleStartExit for a non-warp exit, so a script could
                    // "veto" something that had already physically happened.
                    // And the receiver built a CTaskComplexLeaveCar - a calm,
                    // open-the-door-and-step-out animation - for a ped who was
                    // just launched over the handlebars. That is why the
                    // knock-off never looked right on the other machine.
                    //
                    // warp=true says "they are already out, put them there".
                    // It is the same distinction MTA draws between
                    // VEHICLE_NOTIFY_OUT and a requested exit.
                    SendLocalExit(true, occupancy.door, false, nullptr, true);
                    gLocalRide = LocalRide::OnFoot;
                }
                break;

            case LocalRide::Leaving:
                if (!occupancy.seated && !occupancy.leaving) {
                    // The leave finished normally - the exit RPC went out when
                    // the task started, so there is nothing more to send.
                    gLocalRide = LocalRide::OnFoot;
                }
                else if (occupancy.seated && !occupancy.leaving) {
                    if (vehicleId != 0) {
                        SendLocalEnter(vehicleId, occupancy.seat, true, occupancy.door);
                    }

                    gLocalRide = LocalRide::Seated;
                }
                break;
            }
        }

        // MTA's rule, and the one that matters: `if (uiSeat == 0)`.
        //
        // CNetAPI::WriteVehiclePuresync writes rotation, move speed, turn
        // speed, health and the vehicle-specific block ONLY for seat 0, and
        // ReadVehiclePuresync applies them only for seat 0. Being inside a
        // car is not authority over it.
        //
        // GTA's own m_pDriver is the source of truth here rather than the
        // replica's owner flag: a passenger who used to drive this car can
        // still be holding a stale ownerGUID (the server keeps it across an
        // exit so an empty car still rolls), and that is exactly the case
        // that let a passenger steer the driver's bike.
        [[nodiscard]] bool LocalPlayerOwnsVehicleInput(void *handle) {
            if (SDK::IsLocalPlayerDriving(handle)) {
                return true;
            }

            const auto occupancy = SDK::QueryPedOccupancy(0);

            // Seated somewhere in this car, but not at the wheel. Loud,
            // because reaching here means the replica handed us authority we
            // must not act on - a passenger about to write the driver's pose.
            if (occupancy.vehicle == handle && occupancy.seated && occupancy.seat != SDK::kDriverSeat) {
                static std::uint32_t lastWarnMs = 0;
                const std::uint32_t  now        = GetTickCount();

                if (lastWarnMs == 0 || (now - lastWarnMs) > 1000u) {
                    lastWarnMs = now;
                    Framework::Logging::GetLogger("Vehicle")
                        ->warn("Refusing driver authority: local player is a passenger in seat {} of vehicle {}", occupancy.seat,
                               VehicleNetworkIdOf(handle));
                }
            }

            // NOBODY IS DRIVING IT, AND THE SERVER STILL SAYS IT IS OURS.
            //
            // This is the knock-off desync. The server keeps vehicle
            // ownership across an exit on purpose - an empty car still rolls,
            // and somebody has to simulate it - but this gate only ever
            // returned true for the ped in the driver's seat. The moment
            // CEventKnockOffBike ejects the rider, m_pDriver goes null, so
            // their client stopped pushing the bike while the server went on
            // naming them its owner. Nobody simulated it, and the bike sat
            // wherever each client had last put it.
            //
            // The exception is narrow on purpose: the vehicle must have NO
            // driver at all. A passenger still refuses (the case the warning
            // above is about), and a car someone else is driving is theirs.
            if (occupancy.vehicle != handle && !SDK::VehicleHasDriver(handle)) {
                return true;
            }

            return false;
        }
        // --- Outbound driver sync -------------------------------------------
        //
        // Thresholds below which a change is not worth a field on the wire.
        // Each is well under what a receiver can show after interpolation, so
        // suppressing them is invisible; sending them is pure bandwidth.
        constexpr float kPositionEpsilon = 0.01f;   // 1 cm
        constexpr float kRotationEpsilon = 0.001f;  // ~0.08 deg on the quat dot
        constexpr float kVelocityEpsilon = 0.05f;   // 5 cm/s
        constexpr float kAngularEpsilon  = 0.01f;
        constexpr float kHealthEpsilon   = 0.5f;    // the health quantizer's own step

        // A FULL KEYFRAME EVERY SO OFTEN, BECAUSE THE STREAM IS UNRELIABLE.
        //
        // Deltas are computed against what we last SENT, but an unreliable
        // packet may never have arrived. Without a periodic keyframe a
        // receiver that missed the one packet carrying a field would keep the
        // wrong value FOR EVER - the delta is never re-sent, because as far as
        // the sender is concerned it already went.
        //
        // THIS BOUNDS THE SEND INTERVAL, NOT THE DIVERGENCE. A keyframe is as
        // droppable as anything else on this channel, so real recovery time is
        // a multiple of it. Measured against the real serializer under
        // simulated loss: worst-case field divergence was 0.9 s clean, 2.2 s
        // at 20% loss and 3.6 s at 50% loss. Position does not suffer the same
        // way - it recovers on the next packet that carries it at all - which
        // is why visible error stays small (mean 0.4 m at 20% loss) even while
        // a rarely-changing field is still stale.
        constexpr std::uint32_t kKeyframeMs = 1000;

        [[nodiscard]] std::uint8_t PackVehicleFlags(const SDK::VehicleState &state, void *handle) {
            namespace Flag = Shared::RPC::Vehicle::VehicleFlag;

            std::uint8_t flags = 0;

            if (SDK::VehicleOnGround(handle)) {
                flags |= Flag::kOnGround;
            }

            if (state.engineOn) {
                flags |= Flag::kEngineOn;
            }

            if (state.sirenOn) {
                flags |= Flag::kSirenOn;
            }

            if (state.handbrake) {
                flags |= Flag::kHandbrake;
            }

            if (SDK::VehicleLightsOn(handle)) {
                flags |= Flag::kLightsOn;
            }

            return flags;
        }

        // Builds and sends VehicleDriverSync for a vehicle we hold driver
        // authority over. Returns the payload size, or 0 when nothing needed
        // sending.
        std::size_t SendDriverSync(std::uint64_t networkId, RemoteVehicle &remote, const SDK::VehicleState &state) {
            namespace Field = Shared::RPC::Vehicle::DriverField;

            const auto wireId = gWireIds.Find(networkId);

            // No mapping yet means the server has not told us this vehicle's
            // wire id. Motion waits; the replica still carries it, so nothing
            // is lost in the meantime.
            if (!wireId.Valid()) {
                return 0;
            }

            auto *net = Framework::CoreModules::GetNetworkPeer();

            if (!net) {
                return 0;
            }

            const std::uint32_t now      = GetTickCount();
            const bool          keyframe = !remote.haveSentDriver || (now - remote.lastKeyframeMs) >= kKeyframeMs;

            const glm::vec3 position(state.x, state.y, state.z);
            const glm::vec3 velocity(state.vx, state.vy, state.vz);
            const glm::vec3 angular(state.avx, state.avy, state.avz);
            const std::uint8_t flags = PackVehicleFlags(state, remote.handle);

            Shared::RPC::VehicleDriverSync sync;

            sync.vehicleId = wireId;
            sync.sequence  = static_cast<std::uint16_t>(remote.sentSequence + 1);

            // EVERY TEST IS AGAINST THE LAST SENT VALUE, and a keyframe
            // forces all of them. Note none of these branch on the value
            // itself - a control returning to zero IS a change and must be
            // sent, which is the stale-state bug this protocol must not have.
            if (keyframe || glm::distance(position, remote.sentPosition) > kPositionEpsilon) {
                sync.fields |= Field::kPosition;
                sync.position = position;
            }

            if (keyframe || std::fabs(glm::dot(glm::normalize(state.rotation), glm::normalize(remote.sentRotation))) < 1.0f - kRotationEpsilon) {
                sync.fields |= Field::kRotation;
                sync.rotation = state.rotation;
            }

            if (keyframe || glm::distance(velocity, remote.sentVelocity) > kVelocityEpsilon) {
                sync.fields |= Field::kVelocity;
                sync.velocity = velocity;
            }

            if (keyframe || glm::distance(angular, remote.sentAngular) > kAngularEpsilon) {
                sync.fields |= Field::kTurnSpeed;
                sync.turnSpeed = angular;
            }

            if (keyframe || std::fabs(state.health - remote.sentHealth) > kHealthEpsilon) {
                sync.fields |= Field::kHealth;
                sync.health = state.health;
            }

            if (keyframe || state.steer != remote.sentSteer || state.accelerator != remote.sentAccel || state.brake != remote.sentBrake
                || state.gasPedal != remote.sentGas || state.brakePedal != remote.sentBrakePedal) {
                sync.fields |= Field::kControls;
                sync.steer       = state.steer;
                sync.accelerator = state.accelerator;
                sync.brake       = state.brake;
                sync.gasPedal    = state.gasPedal;
                sync.brakePedal  = state.brakePedal;
            }

            if (keyframe || flags != remote.sentFlags) {
                sync.fields |= Field::kFlags;
                sync.flags = flags;
            }

            // Nothing moved and nothing was touched: send nothing at all.
            // This is what makes a driver sitting still in a stopped car free.
            if (sync.fields == 0) {
                return 0;
            }

            MafiaNet::BitStream bs;

            sync.Serialize(&bs, true);

            // UNRELIABLE, NOT SEQUENCED. A sequenced channel is per channel,
            // not per entity, so one car's packet would discard another's;
            // ordering is per vehicle via `sequence` on the receiver.
            net->BroadcastRPC(sync, MafiaNet::Priority::High, MafiaNet::Reliability::Unreliable);

            remote.sentSequence = sync.sequence;
            remote.lastDriverSyncMs = now;

            if (keyframe) {
                remote.lastKeyframeMs = now;
            }

            // Only fields that were actually sent update the baseline. A
            // field suppressed this tick must keep comparing against what the
            // receiver last heard, not against what GTA has now.
            if (sync.fields & Field::kPosition) {
                remote.sentPosition = position;
            }

            if (sync.fields & Field::kRotation) {
                remote.sentRotation = state.rotation;
            }

            if (sync.fields & Field::kVelocity) {
                remote.sentVelocity = velocity;
            }

            if (sync.fields & Field::kTurnSpeed) {
                remote.sentAngular = angular;
            }

            if (sync.fields & Field::kHealth) {
                remote.sentHealth = state.health;
            }

            if (sync.fields & Field::kControls) {
                remote.sentSteer      = state.steer;
                remote.sentAccel      = state.accelerator;
                remote.sentBrake      = state.brake;
                remote.sentGas        = state.gasPedal;
                remote.sentBrakePedal = state.brakePedal;
            }

            if (sync.fields & Field::kFlags) {
                remote.sentFlags = flags;
            }

            remote.haveSentDriver = true;

            const auto bytes = static_cast<std::size_t>(bs.GetNumberOfBytesUsed());

            Shared::RPC::Metrics::Counters::Get().Record(Shared::RPC::Metrics::PacketType::DriverSync, bytes);

            return bytes;
        }


        void PushOwnedVehicle(Shared::VehicleEntity *entity, RemoteVehicle &remote) {
            if (!remote.handle) {
                return;
            }

            // A PASSENGER NEVER WRITES THE CAR. Pose, momentum, steer and
            // pedals below are all driver-only state; letting a passenger
            // push them is the authority bug, not a cosmetic one.
            if (!LocalPlayerOwnsVehicleInput(remote.handle)) {
                return;
            }

            const auto state = SDK::ReadVehicle(remote.handle);

            // MOTION GOES OUT AS ITS OWN PACKET.
            //
            // VehicleDriverSync carries pose, momentum, controls, health and
            // the physical flags on an Unreliable channel at the driver's
            // cadence, with a change mask so an unchanged field costs nothing.
            // The replica writes below still happen: they are what a LATE
            // JOINER and an out-of-range client construct from, and what the
            // server holds as authoritative state. The difference is that the
            // replica's own delta channel now almost never has anything to
            // say about these fields, because the values it is comparing
            // against were already pushed by the packet.
            // The flag rides with the transform channel and tells every
            // receiver that the pose is coming from the packet instead. It is
            // raised only while we actually have a wire id to send against -
            // before the server hands one over, plain replication is still
            // the only thing carrying this car.
            const auto sentBytes    = SendDriverSync(static_cast<std::uint64_t>(entity->GetNetworkID()), remote, state);
            entity->driverSyncActive = gWireIds.Find(static_cast<std::uint64_t>(entity->GetNetworkID())).Valid();

            (void)sentBytes;

            entity->position        = glm::vec3(state.x, state.y, state.z);
            entity->velocity        = glm::vec3(state.vx, state.vy, state.vz);
            entity->rotation        = state.rotation;
            entity->angularVelocity = glm::vec3(state.avx, state.avy, state.avz);
            entity->health          = state.health;
            entity->engineOn        = state.engineOn;
            entity->sirenOn         = state.sirenOn;

            if (entity->blown || SDK::IsVehicleWrecked(remote.handle) || state.health <= Shared::VehicleEntity::kWreckHealth) {
                entity->LatchBlown();
                SDK::KeepScriptVehicle(remote.handle);
            }
            entity->steer           = state.steer;
            entity->accelerator     = state.accelerator;
            entity->brake           = state.brake;
            entity->handbrake       = state.handbrake;
            entity->gasPedal        = state.gasPedal;
            entity->brakePedal      = state.brakePedal;

            if (state.hasDamage) {
                entity->damageEngine     = state.damageEngine;
                entity->doorsPacked      = state.doorsPacked;
                entity->wheelsPacked     = state.wheelsPacked;
                entity->panels           = state.panels;
                entity->lights           = state.lights;
                entity->doorRatiosPacked = state.doorRatiosPacked;
            }

            entity->interior = SDK::VehicleInterior(remote.handle);

            // Landing gear is a GTA fact on planes (the key toggles it).
            // Cars stay true and this read is a no-op.
            entity->landingGearDown = SDK::VehicleLandingGearDown(remote.handle);
            entity->lightsOn        = SDK::VehicleLightsOn(remote.handle);
            entity->onGround        = SDK::VehicleOnGround(remote.handle);
        }

        // --- Outbound unoccupied sync ---------------------------------------
        //
        // A SLOWER CADENCE, BECAUSE NOBODY IS STEERING IT. MTA uses 400 ms for
        // exactly this and the reasoning holds: an empty car is either at rest
        // (nothing to send at all) or coasting on physics both sides can
        // integrate, so the extra precision of a 100 ms stream buys nothing a
        // viewer can see.
        constexpr std::uint32_t kUnoccupiedIntervalMs = 400;

        // Looser than the driven thresholds for the same reason. A parked car
        // that GTA is jittering by a millimetre must produce NO entry, or the
        // "stationary vehicles cost nothing" property is lost to noise.
        constexpr float kUnoccupiedPositionEpsilon = 0.05f;
        constexpr float kUnoccupiedRotationEpsilon = 0.002f;
        constexpr float kUnoccupiedVelocityEpsilon = 0.10f;

        // Keyframe for the same reason as the driven stream, at the slower
        // rate: this channel is unreliable too.
        constexpr std::uint32_t kUnoccupiedKeyframeMs = 2000;

        std::uint32_t gLastUnoccupiedSendMs = 0;

        // Builds one entry for a vehicle we sync, or leaves fields==0 when
        // nothing changed. Does NOT update the baseline - that happens only
        // once the entry is actually committed to a batch that gets sent.
        [[nodiscard]] Shared::RPC::VehicleUnoccupiedEntry BuildUnoccupiedEntry(Shared::RPC::Common::WireId wireId, RemoteVehicle &remote,
                                                                              const SDK::VehicleState &state, bool keyframe) {
            namespace Field = Shared::RPC::Vehicle::UnoccupiedField;

            Shared::RPC::VehicleUnoccupiedEntry entry;

            entry.vehicleId = wireId;

            const glm::vec3 position(state.x, state.y, state.z);
            const glm::vec3 velocity(state.vx, state.vy, state.vz);
            const glm::vec3 angular(state.avx, state.avy, state.avz);

            if (keyframe || !remote.haveSynced || glm::distance(position, remote.syncedPosition) > kUnoccupiedPositionEpsilon) {
                entry.fields |= Field::kPosition;
                entry.position = position;
            }

            if (keyframe || !remote.haveSynced
                || std::fabs(glm::dot(glm::normalize(state.rotation), glm::normalize(remote.syncedRotation))) < 1.0f - kUnoccupiedRotationEpsilon) {
                entry.fields |= Field::kRotation;
                entry.rotation = state.rotation;
            }

            if (keyframe || !remote.haveSynced || glm::distance(velocity, remote.syncedVelocity) > kUnoccupiedVelocityEpsilon) {
                entry.fields |= Field::kVelocity;
                entry.velocity = velocity;
            }

            if (keyframe || !remote.haveSynced || glm::distance(angular, remote.syncedAngular) > kUnoccupiedVelocityEpsilon) {
                entry.fields |= Field::kTurnSpeed;
                entry.turnSpeed = angular;
            }

            if (keyframe || !remote.haveSynced || std::fabs(state.health - remote.syncedHealth) > kHealthEpsilon) {
                entry.fields |= Field::kHealth;
                entry.health = state.health;
            }

            return entry;
        }

        void CommitUnoccupiedEntry(RemoteVehicle &remote, const Shared::RPC::VehicleUnoccupiedEntry &entry, const SDK::VehicleState &state, bool keyframe,
                                   std::uint32_t now) {
            namespace Field = Shared::RPC::Vehicle::UnoccupiedField;

            if (entry.fields & Field::kPosition) {
                remote.syncedPosition = glm::vec3(state.x, state.y, state.z);
            }

            if (entry.fields & Field::kRotation) {
                remote.syncedRotation = state.rotation;
            }

            if (entry.fields & Field::kVelocity) {
                remote.syncedVelocity = glm::vec3(state.vx, state.vy, state.vz);
            }

            if (entry.fields & Field::kTurnSpeed) {
                remote.syncedAngular = glm::vec3(state.avx, state.avy, state.avz);
            }

            if (entry.fields & Field::kHealth) {
                remote.syncedHealth = state.health;
            }

            if (keyframe) {
                remote.lastSyncerKeyframeMs = now;
            }

            remote.haveSynced = true;
        }

        // Sweeps every vehicle we are the syncer for and sends their deltas as
        // one or more batches.
        //
        // BATCHING IS THE WHOLE POINT. Each RPC pays ~9 bytes of RPC4 header
        // (two message ids plus the Huffman-encoded identifier); twenty
        // separate packets pay it twenty times. One batch pays it once, which
        // is most of the saving on this path.
        void SendUnoccupiedBatches() {
            const std::uint32_t now = GetTickCount();

            if (gLastUnoccupiedSendMs != 0 && (now - gLastUnoccupiedSendMs) < kUnoccupiedIntervalMs) {
                return;
            }

            gLastUnoccupiedSendMs = now;

            auto *net = Framework::CoreModules::GetNetworkPeer();

            if (!net) {
                return;
            }

            Shared::RPC::VehicleUnoccupiedSync batch;
            std::size_t                        pendingBits = 0;

            // Per-vehicle bookkeeping deferred until the batch is actually
            // sent, so a vehicle whose entry was cut by the MTU boundary is
            // not marked as sent.
            std::vector<std::pair<RemoteVehicle *, std::pair<Shared::RPC::VehicleUnoccupiedEntry, SDK::VehicleState>>> committed;
            std::vector<bool>                                                                                          committedKeyframe;

            const auto flush = [&]() {
                if (batch.entries.empty()) {
                    return;
                }

                MafiaNet::BitStream bs;

                batch.Serialize(&bs, true);
                net->BroadcastRPC(batch, MafiaNet::Priority::Medium, MafiaNet::Reliability::Unreliable);

                Shared::RPC::Metrics::Counters::Get().Record(Shared::RPC::Metrics::PacketType::UnoccupiedSync, bs.GetNumberOfBytesUsed());

                for (std::size_t i = 0; i < committed.size(); ++i) {
                    CommitUnoccupiedEntry(*committed[i].first, committed[i].second.first, committed[i].second.second, committedKeyframe[i], now);
                }

                batch.entries.clear();
                committed.clear();
                committedKeyframe.clear();
                pendingBits = 0;
            };

            for (auto &[networkId, remote] : gVehicles) {
                if (!remote.isSyncer || !remote.handle) {
                    continue;
                }

                const auto wireId = gWireIds.Find(networkId);

                if (!wireId.Valid()) {
                    continue;
                }

                const auto state    = SDK::ReadVehicle(remote.handle);
                const bool keyframe = !remote.haveSynced || (now - remote.lastSyncerKeyframeMs) >= kUnoccupiedKeyframeMs;
                const auto entry    = BuildUnoccupiedEntry(wireId, remote, state, keyframe);

                // A PARKED CAR PRODUCES NO ENTRY AT ALL. This is the line that
                // makes a car park free.
                if (entry.Empty()) {
                    continue;
                }

                // STOP BEFORE THE MTU, NOT AFTER. A datagram past 1400 bytes
                // fragments at the IP layer, where losing one fragment loses
                // the whole batch - so the boundary is enforced by starting a
                // new packet, never by hoping the last entry fits.
                if (!Shared::RPC::VehicleUnoccupiedSync::Fits(pendingBits)) {
                    flush();
                }

                batch.sequence = static_cast<std::uint16_t>(now / kUnoccupiedIntervalMs);
                batch.entries.push_back(entry);
                committed.emplace_back(&remote, std::make_pair(entry, state));
                committedKeyframe.push_back(keyframe);
                pendingBits += Shared::RPC::VehicleUnoccupiedEntry::MaxBits();
            }

            flush();
        }

        void ApplyCosmetics(Shared::VehicleEntity *entity, RemoteVehicle &remote) {
            if (!remote.handle) {
                return;
            }

            const bool colours = !remote.haveColours || remote.lastColours != entity->colours;
            const bool paint   = remote.lastPaintjob != entity->paintjob;
            const bool parts   = remote.lastUpgrades != entity->upgrades;

            if (!colours && !paint && !parts) {
                return;
            }

            if (colours) {
                SDK::SetVehicleRgbColors(remote.handle, entity->colours.data(), Shared::kVehicleColorCount);
                remote.lastColours = entity->colours;
                remote.haveColours = true;
            }

            if (paint || parts) {
                SDK::ApplyVehicleCosmetics(remote.handle, entity->paintjob, entity->upgrades.data(), Shared::kUpgradeSlots);
                remote.lastPaintjob  = entity->paintjob;
                remote.lastUpgrades  = entity->upgrades;
            }
        }

        void ApplyScriptState(Shared::VehicleEntity *entity, RemoteVehicle &remote) {
            if (!remote.handle) {
                return;
            }

            if (remote.haveScriptState && remote.lastEngineOn == entity->engineOn && remote.lastSirenOn == entity->sirenOn &&
                remote.lastOverrideLights == entity->overrideLights && remote.lastLocked == entity->locked) {
                return;
            }

            SDK::ApplyVehicleScriptState(remote.handle, entity->engineOn, entity->sirenOn, entity->overrideLights, entity->locked);
            remote.lastEngineOn       = entity->engineOn;
            remote.lastSirenOn        = entity->sirenOn;
            remote.lastOverrideLights = entity->overrideLights;
            remote.lastLocked         = entity->locked;
            remote.haveScriptState    = true;
        }

        void ApplyExtras(Shared::VehicleEntity *entity, RemoteVehicle &remote) {
            if (!remote.handle) {
                return;
            }

            if (remote.haveExtras && remote.lastDirt == entity->dirtLevel && remote.lastTaxiLight == entity->taxiLight &&
                remote.lastHeadlight == entity->headlightColor && remote.lastFrozen == entity->frozen && remote.lastDamageProof == entity->damageProof &&
                remote.lastCollisions == entity->collisions && remote.lastLandingGear == entity->landingGearDown && remote.lastLightsOn == entity->lightsOn &&
                remote.lastEngineBroken == entity->engineBroken && remote.lastWheelsCanBurst == entity->wheelsCanBurst &&
                remote.lastCanBeDamaged == entity->canBeDamaged && remote.lastNitroCount == entity->nitroCount && remote.lastPlateText == entity->plateText) {
                return;
            }

            SDK::ApplyVehicleExtras(remote.handle, entity->dirtLevel, entity->taxiLight, entity->headlightColor, entity->frozen, entity->damageProof,
                                    entity->collisions, entity->landingGearDown, entity->lightsOn, entity->engineBroken, entity->wheelsCanBurst,
                                    entity->canBeDamaged, entity->nitroCount);
            SDK::SetVehiclePlateText(remote.handle, entity->plateText);
            remote.lastDirt           = entity->dirtLevel;
            remote.lastTaxiLight      = entity->taxiLight;
            remote.lastHeadlight      = entity->headlightColor;
            remote.lastFrozen         = entity->frozen;
            remote.lastDamageProof    = entity->damageProof;
            remote.lastCollisions     = entity->collisions;
            remote.lastLandingGear    = entity->landingGearDown;
            remote.lastLightsOn       = entity->lightsOn;
            remote.lastEngineBroken   = entity->engineBroken;
            remote.lastWheelsCanBurst = entity->wheelsCanBurst;
            remote.lastCanBeDamaged   = entity->canBeDamaged;
            remote.lastNitroCount     = entity->nitroCount;
            remote.lastPlateText      = entity->plateText;
            remote.haveExtras         = true;
        }

        bool DamageWorseThan(const SDK::VehicleState &local, const Shared::VehicleEntity &entity) {
            if (local.health + 0.5f < entity.health) {
                return true;
            }

            if (local.damageEngine > entity.damageEngine) {
                return true;
            }

            if (Shared::VehicleEntity::WorsePacked(local.panels, entity.panels, Shared::VehicleEntity::kPanelCount, 4) != entity.panels) {
                return true;
            }

            if (Shared::VehicleEntity::WorsePacked(local.lights, entity.lights, Shared::VehicleEntity::kLightCount, 2) != entity.lights) {
                return true;
            }

            if (Shared::VehicleEntity::WorseBytes(local.doorsPacked, entity.doorsPacked, Shared::VehicleEntity::kDoorCount) != entity.doorsPacked) {
                return true;
            }

            return Shared::VehicleEntity::WorseBytes(local.wheelsPacked, entity.wheelsPacked, Shared::VehicleEntity::kWheelCount) != entity.wheelsPacked;
        }

        // Damage STATUS only. The door swing is driven by BeginDoorInterp on
        // the kDoorRatios path, never from here.
        void ApplyDamageToHandle(void *handle, const Shared::VehicleEntity &entity, RemoteVehicle &remote, bool spawnFlying) {
            SDK::ApplyVehicleDamage(handle, entity.damageEngine, entity.doorsPacked, entity.wheelsPacked, entity.panels, entity.lights, spawnFlying);

            remote.haveDamage = true;
            remote.lastEngine = entity.damageEngine;
            remote.lastDoors  = entity.doorsPacked;
            remote.lastWheels = entity.wheelsPacked;
            remote.lastPanels = entity.panels;
            remote.lastLights = entity.lights;
        }

        float RatioAt(std::uint64_t packed, int index) {
            return static_cast<float>((packed >> (index * 8)) & 0xFFu) / 255.0f;
        }

        std::uint64_t PackRatios(const float *ratios) {
            std::uint64_t packed = 0;

            for (int i = 0; i < Shared::VehicleEntity::kDoorCount; ++i) {
                const auto byte = static_cast<std::uint8_t>(std::clamp(ratios[i], 0.0f, 1.0f) * 255.0f);

                packed |= static_cast<std::uint64_t>(byte) << (i * 8);
            }

            return packed;
        }

        void BeginDoorInterp(RemoteVehicle &remote, void *handle, std::uint64_t targetPacked) {
            const auto current = SDK::ReadVehicle(handle);

            for (int i = 0; i < Shared::VehicleEntity::kDoorCount; ++i) {
                remote.doorStart[i]  = RatioAt(current.doorRatiosPacked, i);
                remote.doorTarget[i] = RatioAt(targetPacked, i);
            }

            remote.heldRatios    = targetPacked;
            remote.doorInterpMs  = GetTickCount();
            remote.doorInterp    = true;
        }

        void MergeOwnerDamage(Shared::VehicleEntity *entity, RemoteVehicle &remote) {
            const auto local = SDK::ReadVehicle(remote.handle);

            if (!local.hasDamage) {
                return;
            }

            // Status only. Ratios stay on this client - applying the replica
            // here is what made the driver's close bounce back open.
            // No model rebuild: the puncher is often still in contact.
            const bool replicaWorse = entity->damageEngine > local.damageEngine ||
                Shared::VehicleEntity::WorseBytes(entity->doorsPacked, local.doorsPacked, Shared::VehicleEntity::kDoorCount) != local.doorsPacked ||
                Shared::VehicleEntity::WorseBytes(entity->wheelsPacked, local.wheelsPacked, Shared::VehicleEntity::kWheelCount) != local.wheelsPacked ||
                Shared::VehicleEntity::WorsePacked(entity->panels, local.panels, Shared::VehicleEntity::kPanelCount, 4) != local.panels ||
                Shared::VehicleEntity::WorsePacked(entity->lights, local.lights, Shared::VehicleEntity::kLightCount, 2) != local.lights;

            if (replicaWorse) {
                ApplyDamageToHandle(remote.handle, *entity, remote, false);
            }

            if (entity->health + 0.5f < local.health) {
                remote.health = entity->health;

                // States only - see SDK::ApplyVehicleStates. A
                // read-modify-write here would re-impose stale momentum.
                SDK::ApplyVehicleStates(remote.handle, entity->health, local.engineOn, local.sirenOn);
            }
        }

        void ReportBystanderDamage(std::uint64_t vehicleId, Shared::VehicleEntity *entity, RemoteVehicle &remote) {
            const auto local = SDK::ReadVehicle(remote.handle);

            if (!local.hasDamage || !DamageWorseThan(local, *entity)) {
                return;
            }

            const std::uint32_t now = GetTickCount();

            if (remote.lastDamageReportMs != 0 && (now - remote.lastDamageReportMs) < Shared::SyncRate::kVehicleMs) {
                return;
            }

            auto *net = Framework::CoreModules::GetNetworkPeer();

            if (!net) {
                return;
            }

            namespace Field = Shared::RPC::Vehicle::DamageField;

            // STATUS GROUPS ONLY. The door ANGLES are not ours to report -
            // the driver owns the swing - so kDoorRatios stays out of the
            // mask, and the receiver cannot mistake its absence for "shut
            // every door".
            Shared::RPC::VehicleDamageReport report;

            report.vehicleId    = vehicleId;
            report.damageEngine = local.damageEngine;
            report.doorsPacked  = local.doorsPacked;
            report.wheelsPacked = local.wheelsPacked;
            report.panels       = local.panels;
            report.lights       = local.lights;
            report.health       = local.health;
            report.Mark(Field::kEngine);
            report.Mark(Field::kDoors);
            report.Mark(Field::kWheels);
            report.Mark(Field::kPanels);
            report.Mark(Field::kLights);
            report.Mark(Field::kHealth);

            net->BroadcastRPC(report);
            remote.lastDamageReportMs = now;
        }

        // WAS LOCAL-ONLY, AND THAT WAS THE DOOR SPAM.
        //
        // This used to ask QueryPedOccupancy(0) - slot 0, the local player -
        // so only OUR door was protected from network ratio writes. A remote
        // player climbing in or out had their door written over on every
        // packet, and CVehicle::OpenDoor re-plays the swing sound each time
        // it is called with a changed ratio.
        //
        // SDK::OccupiedDoorMask walks the local ped, the vehicle's occupants
        // and anyone entering it, so the same protection MTA gives every
        // occupied door (CClientVehicle::SetDoorOpenRatio) now applies here.
        std::uint8_t OccupyingDoorMask(void *vehicle) {
            return SDK::OccupiedDoorMask(vehicle);
        }

        void ReportOccupantDoorRatios(std::uint64_t vehicleId, Shared::VehicleEntity *entity, RemoteVehicle &remote) {
            if (!remote.handle || entity->IsOwner()) {
                return;
            }

            // THE DRIVER OWNS THE SWING. A seated passenger reporting
            // every 100ms overwrote the close they just started, and
            // OpenDoor on the way back is the kick-and-clap loop.
            if (SDK::VehicleHasDriver(remote.handle) || SDK::IsLocalPlayerDriving(remote.handle)) {
                return;
            }

            // KEEP REPORTING AFTER THE TASK ENDS. THIS IS THE ABORT DOOR BUG.
            //
            // The window used to be exactly "while entering or leaving", which
            // closes the frame the enter task dies. But GTA swings the door
            // shut AFTER that - the close is part of the ped returning to
            // foot, not part of the enter task - so the last ratio anyone else
            // heard was a stale half-open angle from mid-climb.
            //
            // The other clients then had no way to correct it. Their own
            // OccupiedDoorMask stops masking the door the moment our ped stops
            // using it, but the only authority for its angle is us, and we had
            // stopped talking. So the aborting player saw the door shut and
            // everyone else kept it hanging open.
            //
            // MTA's answer is to put the ratio in the abort packet
            // (CClientPed.cpp:10443). That is one sample at abort time, which
            // is a fix for the packet but not for the swing that follows it.
            // Holding the window open for a beat afterwards reports the whole
            // close, and costs nothing when the door was already shut because
            // the unchanged-ratio check below drops those.
            constexpr std::uint32_t kDoorSettleMs = 1200;

            const auto          occupancy = SDK::QueryPedOccupancy(0);
            const std::uint32_t now       = GetTickCount();
            const bool          usingDoor = occupancy.vehicle == remote.handle && (occupancy.entering || occupancy.leaving);

            if (usingDoor) {
                remote.doorReleasedMs = now;
            }
            else if (remote.doorReleasedMs == 0 || (now - remote.doorReleasedMs) > kDoorSettleMs) {
                return;
            }

            const auto local = SDK::ReadVehicle(remote.handle);

            if (local.doorRatiosPacked == remote.lastSentRatios) {
                return;
            }

            if (remote.lastRatioReportMs != 0 && (now - remote.lastRatioReportMs) < Shared::SyncRate::kVehicleMs) {
                return;
            }

            auto *net = Framework::CoreModules::GetNetworkPeer();

            if (!net) {
                return;
            }

            namespace Field = Shared::RPC::Vehicle::DamageField;

            // THE DOOR ANGLES ARE THE POINT OF THIS REPORT, so kDoorRatios
            // is in the mask.
            Shared::RPC::VehicleDamageReport report;

            report.vehicleId        = vehicleId;
            report.damageEngine     = local.damageEngine;
            report.doorsPacked      = local.doorsPacked;
            report.wheelsPacked     = local.wheelsPacked;
            report.panels           = local.panels;
            report.lights           = local.lights;
            report.doorRatiosPacked = local.doorRatiosPacked;
            report.health           = local.health;
            report.Mark(Field::kEngine);
            report.Mark(Field::kDoors);
            report.Mark(Field::kWheels);
            report.Mark(Field::kPanels);
            report.Mark(Field::kLights);
            report.Mark(Field::kDoorRatios);
            report.Mark(Field::kHealth);

            net->BroadcastRPC(report);
            remote.lastRatioReportMs = now;
            remote.lastSentRatios    = local.doorRatiosPacked;
        }

        void PulseRemoteDoorRatios(void *handle, RemoteVehicle &remote) {
            // MTA SetDoorOpenRatio(..., TICK_RATE): lerp over the send
            // interval, then stop. Holding OpenDoor every frame fights
            // GTA on a swinging or damaged door and is the stutter.
            const auto skip = OccupyingDoorMask(handle);

            if (!remote.doorInterp) {
                const auto current = SDK::ReadVehicle(handle);

                for (int i = 0; i < Shared::VehicleEntity::kDoorCount; ++i) {
                    if (std::fabs(RatioAt(current.doorRatiosPacked, i) - RatioAt(remote.heldRatios, i)) > (2.0f / 255.0f)) {
                        BeginDoorInterp(remote, handle, remote.heldRatios);
                        break;
                    }
                }

                if (!remote.doorInterp) {
                    return;
                }
            }

            const std::uint32_t now     = GetTickCount();
            const float         elapsed = static_cast<float>(now - remote.doorInterpMs);
            const float         span    = static_cast<float>(Shared::SyncRate::kVehicleMs);

            if (elapsed >= span) {
                SDK::ApplyVehicleDoorRatios(handle, remote.heldRatios, skip);
                remote.doorInterp = false;
                return;
            }

            float mixed[Shared::VehicleEntity::kDoorCount] {};

            for (int i = 0; i < Shared::VehicleEntity::kDoorCount; ++i) {
                mixed[i] = remote.doorStart[i] + (remote.doorTarget[i] - remote.doorStart[i]) * (elapsed / span);
            }

            SDK::ApplyVehicleDoorRatios(handle, PackRatios(mixed), skip);
        }

        void EnsureSpawned(Shared::VehicleEntity *entity, RemoteVehicle &remote) {
            if (remote.handle) {
                return;
            }

            const int model = static_cast<int>(entity->modelId);

            if (!SDK::IsVehicleModel(model)) {
                return;
            }

            if (!SDK::IsModelLoaded(model)) {
                SDK::RequestModel(model);
                return;
            }

            const float heading = glm::roll(entity->rotation);

            remote.handle = SDK::CreateScriptVehicle(model, entity->position.x, entity->position.y, entity->position.z, heading, entity->primaryColour,
                                                     entity->secondaryColour, entity->variant, entity->variant2);

            if (!remote.handle) {
                Framework::Logging::GetLogger("Vehicle")->warn("Could not create vehicle {} (model {})", static_cast<std::uint64_t>(entity->GetNetworkID()), model);
                return;
            }

            remote.lastVariant  = entity->variant;
            remote.lastVariant2 = entity->variant2;
            remote.haveVariants = true;

            SDK::SetVehiclePose(remote.handle, entity->position.x, entity->position.y, entity->position.z, entity->rotation);

            auto state = SDK::ReadVehicle(remote.handle);

            state.health   = entity->health;
            state.engineOn = entity->engineOn;
            state.sirenOn  = entity->sirenOn;
            state.vx       = entity->velocity.x;
            state.vy       = entity->velocity.y;
            state.vz       = entity->velocity.z;
            state.avx      = entity->angularVelocity.x;
            state.avy      = entity->angularVelocity.y;
            state.avz      = entity->angularVelocity.z;

            SDK::ApplyVehicleMotion(remote.handle, state);
            SDK::ApplyVehicleDamage(remote.handle, entity->damageEngine, entity->doorsPacked, entity->wheelsPacked, entity->panels, entity->lights);
            SDK::ApplyVehicleDoorRatios(remote.handle, entity->doorRatiosPacked, SDK::OccupiedDoorMask(remote.handle));
            SDK::BindRemoteVehicleProcessControl(remote.handle);

            if (entity->IsBlown()) {
                // Already a wreck on the wire. Hide the boom — it happened
                // before this client existed.
                SDK::WreckVehicle(remote.handle, true);
                remote.wrecked = true;
            }

            SDK::KeepScriptVehicle(remote.handle);
            SDK::SyncVehicleSimulationStatus(remote.handle);
            SDK::SetVehicleInterior(remote.handle, entity->interior);
            ApplyCosmetics(entity, remote);
            ApplyScriptState(entity, remote);
            ApplyExtras(entity, remote);

            remote.velocity = entity->velocity;
            remote.angular  = entity->angularVelocity;
            remote.health   = entity->health;
            remote.engineOn = entity->engineOn;
            remote.sirenOn  = entity->sirenOn;
            remote.haveDamage = true;
            remote.lastEngine = entity->damageEngine;
            remote.lastDoors  = entity->doorsPacked;
            remote.lastWheels = entity->wheelsPacked;
            remote.lastPanels = entity->panels;
            remote.lastLights = entity->lights;
            remote.lastRatios       = entity->doorRatiosPacked;
            remote.lastEntityRatios = entity->doorRatiosPacked;
            remote.heldRatios       = entity->doorRatiosPacked;
        }

        // COMMON BOOKKEEPING, THEN DISPATCH.
        //
        // Everything here applies to every vehicle regardless of its GTA
        // class: cosmetics, damage, doors, wreck state, ownership. The moment
        // physical state is involved - pose, momentum, controls - the frame
        // goes to the handler for that vehicle's type. See
        // game/sync/vehicle/handler.h.
        void ApplyRemoteVehicle(Shared::VehicleEntity *entity, RemoteVehicle &remote) {
            if (!remote.handle || SDK::IsLocalPlayerDriving(remote.handle)) {
                return;
            }

            // BEFORE GTA PROCESSES IT THIS FRAME. Status decides whether the
            // game runs a vehicle's player-driving path at all - and for a
            // bike that path is what applies steer and lean and clears the
            // handbrake CVehicle's constructor leaves on.
            SDK::SyncVehicleSimulationStatus(remote.handle);

            SDK::SetVehicleInterior(remote.handle, entity->interior);
            ApplyCosmetics(entity, remote);
            ApplyScriptState(entity, remote);
            ApplyExtras(entity, remote);

            const std::uint32_t now     = GetTickCount();
            const bool          changed = !remote.aimed || remote.aimedPosition != entity->position || remote.aimedRotation != entity->rotation;

            remote.health      = entity->health;
            remote.engineOn    = entity->engineOn;
            remote.sirenOn     = entity->sirenOn;
            remote.steer       = entity->steer;
            remote.accelerator = entity->accelerator;
            remote.brake       = entity->brake;
            remote.handbrake   = entity->handbrake;
            remote.gasPedal    = entity->gasPedal;
            remote.brakePedal  = entity->brakePedal;

            const bool partsChanged = !remote.haveDamage || remote.lastEngine != entity->damageEngine || remote.lastDoors != entity->doorsPacked ||
                remote.lastWheels != entity->wheelsPacked || remote.lastPanels != entity->panels || remote.lastLights != entity->lights;

            if (partsChanged && !remote.wrecked) {
                // One flying component per newly-missing part, not a
                // SetupDamageAfterLoad of the whole wreck.
                ApplyDamageToHandle(remote.handle, *entity, remote, entity->health > SDK::kWreckHealth);
            }

            if (!remote.wrecked && (entity->IsBlown() || SDK::IsVehicleWrecked(remote.handle))) {
                SDK::WreckVehicle(remote.handle, false);
                remote.wrecked = true;
            }

            if (remote.wrecked) {
                // GTA deletes a wreck after 60 seconds and will stream the
                // hull out if BlowUpCar dropped the mission lock. Leave it
                // where the boom put it - interpolating a wreck is how it
                // falls through the map.
                SDK::KeepScriptVehicle(remote.handle);
                return;
            }

            if (entity->doorRatiosPacked != remote.lastEntityRatios) {
                remote.lastEntityRatios = entity->doorRatiosPacked;
                remote.lastRatios       = entity->doorRatiosPacked;
                BeginDoorInterp(remote, remote.handle, entity->doorRatiosPacked);
            }

            // The per-frame view a handler works from. Cross-frame state
            // lives in RemoteVehicle and is copied back out below.
            VehicleSync::Frame frame;

            frame.handle       = remote.handle;
            frame.entity       = entity;
            frame.interpolator = &remote.interpolator;
            frame.aimedPosition = remote.aimedPosition;
            frame.aimedRotation = remote.aimedRotation;
            frame.aimed         = remote.aimed;
            frame.speed         = remote.speed;
            frame.velocity      = remote.velocity;
            frame.angular       = remote.angular;
            frame.packetArrived = changed;
            frame.packetMs      = remote.whenMs;
            frame.hasDriver     = SDK::VehicleHasDriver(remote.handle);

            const auto &handler = VehicleSync::HandlerFor(remote.handle);

            if (changed) {
                remote.velocity = entity->velocity;
                remote.angular  = entity->angularVelocity;

                frame.velocity = remote.velocity;
                frame.angular  = remote.angular;

                if (handler.OnSnapshot) {
                    handler.OnSnapshot(frame);
                }

                remote.aimedPosition = entity->position;
                remote.aimedRotation = entity->rotation;
                remote.speed         = glm::length(entity->velocity);
                remote.whenMs        = now;
                remote.aimed         = true;

                frame.aimedPosition = remote.aimedPosition;
                frame.aimedRotation = remote.aimedRotation;
                frame.aimed         = remote.aimed;
                frame.speed         = remote.speed;
                frame.packetMs      = remote.whenMs;
            }

            if (handler.Apply) {
                handler.Apply(frame);
            }

            PulseRemoteDoorRatios(remote.handle, remote);
        }

        void DestroyHandle(RemoteVehicle &remote) {
            if (!remote.handle) {
                return;
            }

            SDK::CancelEntriesInto(remote.handle);
            SDK::ClearVehicleRgbColors(remote.handle);
            SDK::ClearVehicleHeadlightColor(remote.handle);
            SDK::DestroyScriptVehicle(remote.handle);
            remote.handle = nullptr;
        }

        void RecreateIfVariantChanged(Shared::VehicleEntity *entity, RemoteVehicle &remote) {
            if (!remote.handle || !remote.haveVariants) {
                return;
            }

            if (remote.lastVariant == entity->variant && remote.lastVariant2 == entity->variant2) {
                return;
            }

            // Extras are chosen at CreateCarForScript. Changing them means
            // a new clump — same as MTA SetVariant → ReCreate.
            DestroyHandle(remote);
            remote.haveExtras       = false;
            remote.haveScriptState  = false;
            remote.haveColours      = false;
            remote.haveVariants     = false;
            remote.haveDamage       = false;
            remote.wrecked          = false;
        }
    } // namespace

    void UpdateVehicleSync() {
        auto *replication = Framework::CoreModules::GetReplication();

        if (!replication) {
            return;
        }

        std::unordered_set<std::uint64_t> seen;

        replication->ForEachEntity([&seen](Replication::NetworkEntity *entity) {
            auto *vehicle = dynamic_cast<Shared::VehicleEntity *>(entity);

            if (!vehicle) {
                return;
            }

            const auto id = static_cast<std::uint64_t>(vehicle->GetNetworkID());

            if (!Shared::SameDimension(QueryLocalWorldSpace().dimension, vehicle->dimension)) {
                return;
            }

            seen.insert(id);

            auto &remote = gVehicles[id];

            RecreateIfVariantChanged(vehicle, remote);
            EnsureSpawned(vehicle, remote);

            if (!remote.handle) {
                return;
            }

            if (vehicle->IsOwner()) {
                ApplyCosmetics(vehicle, remote);
                ApplyScriptState(vehicle, remote);
                ApplyExtras(vehicle, remote);
                SDK::SetVehicleAlpha(remote.handle, vehicle->alpha);
                MergeOwnerDamage(vehicle, remote);
                PushOwnedVehicle(vehicle, remote);
                return;
            }

            ReportBystanderDamage(id, vehicle, remote);
            ReportOccupantDoorRatios(id, vehicle, remote);
            ApplyRemoteVehicle(vehicle, remote);
            SDK::SetVehicleAlpha(remote.handle, vehicle->alpha);
        });

        for (auto it = gVehicles.begin(); it != gVehicles.end();) {
            if (seen.contains(it->first)) {
                ++it;
                continue;
            }

            DestroyHandle(it->second);
            it = gVehicles.erase(it);
        }

        WatchLocalOccupancy();

        // After the per-vehicle pass, so every syncer's GTA state has been
        // read this frame. Rate-limits itself to kUnoccupiedIntervalMs.
        SendUnoccupiedBatches();
    }

    void ClearRemoteVehicles() {
        for (auto &[id, remote] : gVehicles) {
            DestroyHandle(remote);
        }

        gVehicles.clear();
        gLateJoinWarp.clear();
        gLocalRide = LocalRide::OnFoot;
    }

    std::uint64_t VehicleNetworkIdOf(const void *vehicle) {
        if (!vehicle) {
            return 0;
        }

        for (const auto &[id, remote] : gVehicles) {
            if (remote.handle == vehicle) {
                return id;
            }
        }

        return 0;
    }

    void SeatRemoteInVehicle(int slot, std::uint64_t vehicleId, std::uint8_t seat, bool lateJoin) {
        if (lateJoin) {
            RememberLateJoin(slot);
        }

        auto *handle = HandleOf(vehicleId);

        if (!handle) {
            return;
        }

        const auto occupancy = SDK::QueryPedOccupancy(slot);

        if (occupancy.seated && occupancy.vehicle == handle && occupancy.seat == seat) {
            ForgetLateJoin(slot);
            return;
        }

        if (occupancy.entering && occupancy.vehicle == handle) {
            return;
        }

        if (occupancy.leaving) {
            return;
        }

        // STILL GETTING UP. A warp into the seat here flushes the tasks
        // (EnterVehicle -> PrepareForTransition) and the stand-up animation
        // vanishes mid-play. The owner will still be reporting the seat next
        // packet, so this costs a tick, not the seat.
        if (SDK::PedIsFallingOffBike(slot)) {
            return;
        }

        // MTA puresync never seats anyone. A live climb is PlayerEnterVehicle
        // with the door. Starting a walk from this snapshot - before that
        // RPC, and without a door - is the frozen walk-to-car: they path
        // from wherever they are, then the RPC is ignored because they
        // already "occupy" the vehicle.
        const bool warp = lateJoin || ConsumeLateJoin(slot);

        if (!warp) {
            return;
        }

        if (SDK::EnterVehicle(slot, handle, seat, true)) {
            ForgetLateJoin(slot);

            // The seat is what makes this a driven vehicle, so the status
            // that follows from it is settled here rather than a frame later.
            SDK::SyncVehicleSimulationStatus(handle);
        }
    }

    void ApplyRemoteEnterVehicle(std::uint64_t networkId, std::uint64_t vehicleId, std::uint8_t seat, bool warp, std::uint8_t door) {
        if (networkId == 0 || networkId == LocalNetworkId()) {
            if (networkId == LocalNetworkId() && warp) {
                auto *handle = HandleOf(vehicleId);

                if (handle) {
                    SDK::EnterVehicle(0, handle, seat, true, door);
                    gLocalRide = LocalRide::Seated;
                }
            }

            return;
        }

        const int slot = SlotOfNetworkId(networkId);

        if (slot < 0) {
            return;
        }

        if (warp) {
            RememberLateJoin(slot);
        }
        else {
            ForgetLateJoin(slot);
        }

        auto *handle = HandleOf(vehicleId);

        if (!handle) {
            return;
        }

        SDK::EnterVehicle(slot, handle, seat, warp, door);

        // Collision stays on while they walk to the door. Turning it off
        // here is what froze remotes mid-stride: the enter task cannot
        // path to a door on a ped that does not collide.
        if (SDK::IsPedInVehicle(slot)) {
            SDK::SetRemotePedCollision(slot, false);
        }
    }

    void ApplyRemoteExitVehicle(std::uint64_t networkId, bool warp, std::uint8_t door, bool abortEnter, std::uint8_t doorRatio, bool knockedOff) {
        if (networkId == 0 || networkId == LocalNetworkId()) {
            // OUR OWN EXIT COMES BACK TO US - IGNORE IT IF WE ARE ALREADY OUT.
            //
            // The server rebroadcasts to everyone including the sender, and a
            // knock-off now reports warp=true (the ped was ejected, not
            // walked out). Without this check that echo would re-run
            // ExitVehicle on a ped who is already lying on the road, which
            // re-issues CTaskSimpleCarSetPedOut and stamps them back to the
            // door position.
            //
            // The branch still matters for a genuine SERVER-forced warp-out,
            // where we are seated and are being told to get out.
            if (networkId == LocalNetworkId() && warp && SDK::IsPedInVehicle(0)) {
                SDK::ExitVehicle(0, true, door);
                gLocalRide = LocalRide::OnFoot;
            }

            return;
        }

        const int slot = SlotOfNetworkId(networkId);

        if (slot < 0) {
            return;
        }

        ForgetLateJoin(slot);

        // THE DOOR THEY LEFT BEHIND.
        //
        // An aborted entry is the one case where the door's angle cannot be
        // derived locally: the remote ped stops using it, so our occupancy
        // mask stops protecting it, but the owner is the only one who knows
        // how far it swung. They send it with the abort; we force it, exactly
        // as MTA does on notify-abort (CPacketHandler.cpp:1972-1974).
        if (abortEnter) {
            const auto index = SDK::DoorRatioIndexForTarget(door);

            if (index != SDK::kInvalidDoor) {
                // Read BEFORE ExitVehicle below, while the ped's enter task
                // still names the vehicle it was climbing into.
                const auto occupancy = SDK::QueryPedOccupancy(slot);

                if (occupancy.vehicle) {
                    SDK::ForceVehicleDoorRatio(occupancy.vehicle, index, doorRatio);
                }
            }
        }

        SDK::ExitVehicle(slot, warp, door, knockedOff);

        if (!SDK::IsPedInVehicle(slot)) {
            SDK::SetRemotePedCollision(slot, true);
        }
    }

    void ApplyOccupancyAbort(std::uint64_t networkId, std::uint64_t vehicleId, std::uint8_t kind, std::uint8_t seat, std::uint8_t door) {
        (void)door;

        const bool local = networkId != 0 && networkId == LocalNetworkId();
        const int  slot  = local ? 0 : SlotOfNetworkId(networkId);

        if (!local && slot < 0) {
            return;
        }

        auto *handle = HandleOf(vehicleId);

        if (kind == Shared::RPC::PlayerOccupancyAbort::kExit) {
            SDK::AbortVehicleExit(slot, handle, seat);

            if (local) {
                gBlockExitUntilRelease = true;
                gLocalRide             = LocalRide::Seated;
            }

            return;
        }

        SDK::AbortVehicleEnter(slot);
        ForgetLateJoin(slot);

        if (local) {
            gBlockEnterUntilRelease = true;
            gLocalRide              = LocalRide::OnFoot;
        }
        else if (!SDK::IsPedInVehicle(slot)) {
            SDK::SetRemotePedCollision(slot, true);
        }
    }

    void ClearRemoteOccupancy(int slot) {
        ForgetLateJoin(slot);
    }

    void AdoptVehicleWireId(std::uint64_t networkId, std::uint16_t wireId) {
        gWireIds.Adopt(networkId, Shared::RPC::Common::WireId {wireId});
    }

    void SetVehicleSyncerRole(std::uint64_t networkId, bool start) {
        auto &remote = gVehicles[networkId];

        if (remote.isSyncer == start) {
            return;
        }

        remote.isSyncer = start;

        // A ROLE CHANGE RESETS THE BASELINE. Deltas are compared against what
        // this client last sent, and it has sent nothing as syncer for a
        // vehicle it has just been handed - so without this the first batch
        // would suppress every field that happens to match a stale baseline
        // from the last time it held the role, and the new receivers would
        // never be told the current state.
        remote.haveSynced = false;
    }

    void ApplyRemoteUnoccupiedSync(const Shared::RPC::VehicleUnoccupiedSync &batch) {
        namespace Field = Shared::RPC::Vehicle::UnoccupiedField;

        auto *replication = Framework::CoreModules::GetReplication();

        if (!replication) {
            return;
        }

        for (const auto &entry : batch.entries) {
            const auto networkId = gWireIds.Resolve(entry.vehicleId);

            if (networkId == 0) {
                continue;
            }

            auto *entity = dynamic_cast<Shared::VehicleEntity *>(replication->GetEntityByNetworkID(static_cast<MafiaNet::NetworkID>(networkId)));

            if (!entity || entity->IsOwner()) {
                continue;
            }

            auto it = gVehicles.find(networkId);

            // Our own batch comes back to us for vehicles we sync; applying it
            // would fight the local simulation.
            if (it == gVehicles.end() || it->second.isSyncer) {
                continue;
            }

            if (entry.Has(Field::kPosition)) {
                entity->position = entry.position;
            }

            if (entry.Has(Field::kRotation)) {
                entity->rotation = entry.rotation;
            }

            if (entry.Has(Field::kVelocity)) {
                entity->velocity = entry.velocity;
            }

            if (entry.Has(Field::kTurnSpeed)) {
                entity->angularVelocity = entry.turnSpeed;
            }

            if (entry.Has(Field::kHealth)) {
                entity->health = entry.health;
            }
        }
    }

    void ApplyRemoteDriverSync(const Shared::RPC::VehicleDriverSync &sync) {
        namespace Field = Shared::RPC::Vehicle::DriverField;

        const auto networkId = gWireIds.Resolve(sync.vehicleId);

        // A wire id that does not resolve is a packet for a vehicle that has
        // despawned, or one minted before a slot was reused. Dropping it is
        // the point of the generation half of the id - the alternative is
        // steering whatever now occupies that slot.
        if (networkId == 0) {
            return;
        }

        auto *replication = Framework::CoreModules::GetReplication();

        if (!replication) {
            return;
        }

        auto *entity = dynamic_cast<Shared::VehicleEntity *>(replication->GetEntityByNetworkID(static_cast<MafiaNet::NetworkID>(networkId)));

        if (!entity) {
            return;
        }

        // NEVER LET A PACKET OVERWRITE WHAT WE OURSELVES OWN. The server
        // broadcasts, so our own sync comes back to us; applying it would
        // fight the local simulation.
        if (entity->IsOwner()) {
            return;
        }

        auto it = gVehicles.find(networkId);

        if (it == gVehicles.end()) {
            return;
        }

        auto &remote = it->second;

        // LATEST WINS. The stream is Unreliable, so datagrams reorder: an
        // older snapshot arriving after a newer one must be dropped, not
        // applied, or the car visibly jumps backwards. Signed-difference
        // comparison so the uint16 wrap is harmless.
        if (remote.haveAppliedSequence && !Shared::RPC::IsNewerSequence(sync.sequence, remote.appliedSequence)) {
            return;
        }

        remote.appliedSequence     = sync.sequence;
        remote.haveAppliedSequence = true;

        // ONLY WHAT THE MASK NAMES. A field the sender suppressed keeps the
        // value we already have - it is NOT zero, and it is NOT stale: the
        // sender suppressed it precisely because it had not changed since the
        // value we are holding.
        if (sync.Has(Field::kPosition)) {
            entity->position = sync.position;
        }

        if (sync.Has(Field::kRotation)) {
            entity->rotation = sync.rotation;
        }

        if (sync.Has(Field::kVelocity)) {
            entity->velocity = sync.velocity;
        }

        if (sync.Has(Field::kTurnSpeed)) {
            entity->angularVelocity = sync.turnSpeed;
        }

        if (sync.Has(Field::kHealth)) {
            entity->health = sync.health;
        }

        if (sync.Has(Field::kControls)) {
            entity->steer       = sync.steer;
            entity->accelerator = sync.accelerator;
            entity->brake       = sync.brake;
            entity->gasPedal    = sync.gasPedal;
            entity->brakePedal  = sync.brakePedal;
        }

        if (sync.Has(Field::kFlags)) {
            namespace Flag = Shared::RPC::Vehicle::VehicleFlag;

            entity->onGround  = (sync.flags & Flag::kOnGround) != 0;
            entity->engineOn  = (sync.flags & Flag::kEngineOn) != 0;
            entity->sirenOn   = (sync.flags & Flag::kSirenOn) != 0;
            entity->handbrake = (sync.flags & Flag::kHandbrake) != 0;
            entity->lightsOn  = (sync.flags & Flag::kLightsOn) != 0;
        }

        // ApplyRemoteVehicle runs from the per-frame tick and picks the new
        // pose up from the entity, exactly as it did when the replica carried
        // it. Nothing below the dispatcher knows this came from a packet -
        // which is the whole point of decoding into the entity.
    }

    void ApplyRemoteVehicleDamage(const Shared::RPC::VehicleDamageReport &report) {
        namespace Field = Shared::RPC::Vehicle::DamageField;

        const auto found = gVehicles.find(report.vehicleId);

        if (found == gVehicles.end() || !found->second.handle) {
            return;
        }

        auto &remote = found->second;

        // SEEDED FROM WHAT WE ALREADY HAVE, NOT FROM ZERO.
        //
        // This used to build a blank VehicleEntity and copy every group out
        // of the packet into it, which meant a report about one panel arrived
        // at ApplyDamageToHandle claiming every door, wheel and light was
        // pristine. It survived only because the old sender always filled all
        // seven groups - so the mask is not just a bandwidth saving here, it
        // is what makes a partial report expressible at all.
        Shared::VehicleEntity entity;

        entity.damageEngine     = remote.lastEngine;
        entity.doorsPacked      = remote.lastDoors;
        entity.wheelsPacked     = remote.lastWheels;
        entity.panels           = remote.lastPanels;
        entity.lights           = remote.lastLights;
        entity.doorRatiosPacked = remote.lastRatios;
        entity.health           = remote.health;

        if (report.Has(Field::kEngine)) {
            entity.damageEngine = report.damageEngine;
        }

        if (report.Has(Field::kDoors)) {
            entity.doorsPacked = report.doorsPacked;
        }

        if (report.Has(Field::kWheels)) {
            entity.wheelsPacked = report.wheelsPacked;
        }

        if (report.Has(Field::kPanels)) {
            entity.panels = report.panels;
        }

        if (report.Has(Field::kLights)) {
            entity.lights = report.lights;
        }

        if (report.Has(Field::kHealth)) {
            entity.health = report.health;
        }

        ApplyDamageToHandle(remote.handle, entity, remote, entity.health > SDK::kWreckHealth);

        const auto local = SDK::ReadVehicle(remote.handle);

        if (report.Has(Field::kHealth) && report.health + 0.5f < local.health) {
            remote.health = report.health;

            SDK::ApplyVehicleStates(remote.handle, report.health, local.engineOn, local.sirenOn);
        }

        if (!remote.wrecked && entity.health <= SDK::kWreckHealth) {
            SDK::WreckVehicle(remote.handle, false);
            remote.wrecked = true;
        }

        if (remote.wrecked) {
            SDK::KeepScriptVehicle(remote.handle);
        }

        // Absent ratios mean DO NOT TOUCH THE DOORS - not "close them".
        if (report.Has(Field::kDoorRatios)) {
            remote.lastRatios       = report.doorRatiosPacked;
            remote.lastEntityRatios = report.doorRatiosPacked;
            BeginDoorInterp(remote, remote.handle, report.doorRatiosPacked);
        }
    }

    void TryLocalPassengerKey() {
        if (SDK::QueryLocalPedWeapon().reloading) {
            return;
        }

        const auto occupancy = SDK::QueryPedOccupancy(0);

        if (occupancy.entering || occupancy.leaving) {
            return;
        }

        // MTA enter_passenger: G gets a passenger out. A driver has to
        // use F. Old Vanta exited from every seat; that is not what G
        // means here.
        if (occupancy.seated) {
            if (occupancy.seat != SDK::kDriverSeat) {
                SDK::ExitVehicle(0, false);
            }

            return;
        }

        void *nearest          = nullptr;
        float nearestDistance = SDK::kPassengerReach;

        for (auto &entry : gVehicles) {
            auto &remote = entry.second;
            float distance = 0.0f;

            if (!remote.handle || !SDK::PassengerDoorDistance(0, remote.handle, &distance)) {
                continue;
            }

            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearest         = remote.handle;
            }
        }

        if (!nearest) {
            return;
        }

        const auto seat = SDK::FreePassengerSeat(nearest);

        if (seat == 0) {
            return;
        }

        // Walk, not warp. WatchLocalOccupancy sends the same enter RPC
        // the F path uses, so G and F cannot disagree about the climb.
        SDK::EnterVehicle(0, nearest, seat, false);
    }

    void ApplyOwnedVehicleWorldSpace(std::uint64_t networkId, std::uint8_t interior, std::uint16_t dimension, std::uint8_t flags) {
        auto *replication = Framework::CoreModules::GetReplication();

        if (!replication || networkId == 0) {
            return;
        }

        Shared::VehicleEntity *vehicle = nullptr;

        replication->ForEachEntity([&](Replication::NetworkEntity *entity) {
            auto *candidate = dynamic_cast<Shared::VehicleEntity *>(entity);

            if (candidate && candidate->IsOwner() && static_cast<std::uint64_t>(candidate->GetNetworkID()) == networkId) {
                vehicle = candidate;
            }
        });

        if (!vehicle) {
            return;
        }

        if ((flags & Shared::kWorldSpaceInterior) != 0) {
            vehicle->interior = interior;

            const auto found = gVehicles.find(networkId);

            if (found != gVehicles.end() && found->second.handle) {
                SDK::SetVehicleInterior(found->second.handle, interior);
            }
        }

        if ((flags & Shared::kWorldSpaceDimension) != 0) {
            vehicle->dimension = dimension;
        }
    }

    void *VehicleHandleOf(std::uint64_t networkId) {
        const auto found = gVehicles.find(networkId);

        return found == gVehicles.end() ? nullptr : found->second.handle;
    }
} // namespace VantaMP::Game
