#include "game/sync/vehicle_sync.h"

#include "game/sync/player_sync.h"
#include "game/sync/vehicle/state.h"
#include "sdk/game.h"
#include "shared/entity/vehicle_entity.h"
#include "shared/rpc/common/metrics.h"
#include "shared/rpc/vehicle/driver_sync.h"
#include "shared/rpc/vehicle/unoccupied_sync.h"
#include "shared/world/world_space.h"

#include <utils/safe_win32.h>

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>

#include <cmath>
#include <utility>
#include <vector>

// WHAT THIS CLIENT SENDS ABOUT VEHICLES.
//
// Two streams, both Unreliable and both delta-encoded against what was last
// SENT rather than what GTA currently holds - a field suppressed this tick must
// keep comparing against what the receiver actually heard.
//
//   VehicleDriverSync      the car we are driving, at the player rate
//   VehicleUnoccupiedSync  cars nobody is in that the server made us syncer for,
//                          batched and at a slower cadence
//
// Split out of vehicle_sync.cpp. Shared registry state is in vehicle/state.h.
namespace VantaMP::Game::VehicleSyncState {
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
        // Decides which fields this tick actually has to carry, and fills them in.
        //
        // EVERY TEST IS AGAINST THE LAST SENT VALUE, and a keyframe forces all of
        // them. None of these branch on the value itself - a control returning to
        // zero IS a change and must be sent, which is the stale-state bug this
        // protocol must not have.
        void BuildDriverSyncFields(Shared::RPC::VehicleDriverSync &sync, const RemoteVehicle &remote, const SDK::VehicleState &state,
                                   const glm::vec3 &position, const glm::vec3 &velocity, const glm::vec3 &angular, std::uint8_t flags,
                                   bool keyframe) {
            namespace Field = Shared::RPC::Vehicle::DriverField;

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
        }

        // Records what actually went out, so the next tick compares against what
        // the RECEIVER now holds rather than against whatever GTA has.
        //
        // Only fields that were actually sent move the baseline: a field suppressed
        // this tick must keep comparing against what the receiver last heard.
        void CommitDriverSyncBaseline(RemoteVehicle &remote, const Shared::RPC::VehicleDriverSync &sync, const SDK::VehicleState &state,
                                      const glm::vec3 &position, const glm::vec3 &velocity, const glm::vec3 &angular, std::uint8_t flags) {
            namespace Field = Shared::RPC::Vehicle::DriverField;

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
        }

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

            BuildDriverSyncFields(sync, remote, state, position, velocity, angular, flags, keyframe);

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

            CommitDriverSyncBaseline(remote, sync, state, position, velocity, angular, flags);

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
} // namespace VantaMP::Game::VehicleSyncState
