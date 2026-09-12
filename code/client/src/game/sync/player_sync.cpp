#include "game/sync/player_sync.h"

#include "game/sync/death.h"
#include "game/sync/vehicle_sync.h"
#include "game/sync/world_sync.h"
#include "game/entities/local_player.h"
#include "game/entities/player_slots.h"
#include "game/entities/remote_players.h"
#include "sdk/game.h"
#include "shared/entity/player_entity.h"
#include "shared/entity/state_forced.h"
#include "shared/rpc/common/metrics.h"
#include "shared/rpc/common/wire_registry.h"
#include "shared/rpc/player/onfoot_sync.h"
#include "shared/rpc/player/script.h"
#include "shared/rpc/player/weapon.h"
#include "shared/rpc/world_space.h"
#include "shared/sync_rates.h"
#include "shared/entity/vehicle_entity.h"
#include "shared/world/world_space.h"

#include <utils/safe_win32.h>

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>
#include <utils/interpolator.h>

#include <glm/gtc/quaternion.hpp>

#include <unordered_map>
#include <unordered_set>

#include <algorithm>
#include <cmath>

namespace VantaMP::Game {
    namespace {
        namespace Replication = Framework::Networking::Replication;

        // Which slot each remote entity was given a ped in. Keyed by network id
        // rather than by pointer: an entity can be destroyed and its memory
        // reused, and a stale pointer would silently match the wrong player.
        std::unordered_map<std::uint64_t, int> gSlotByNetworkId;
        std::unordered_map<int, std::uint8_t>  gRemoteAlpha;
        std::uint64_t gLocalNetworkId = 0;
        LocalWorldSpace gLocalWorld {};

        Shared::PlayerEntity *OwnedPlayer(Replication::ReplicationManager *replication) {
            Shared::PlayerEntity *owned = nullptr;

            if (!replication) {
                return nullptr;
            }

            replication->ForEachEntity([&owned](Replication::NetworkEntity *entity) {
                auto *player = dynamic_cast<Shared::PlayerEntity *>(entity);

                if (player && player->IsOwner()) {
                    owned = player;
                }
            });

            return owned;
        }

        // One per remote ped.
        //
        // THE FRAMEWORK'S, NOT ONE OF OUR OWN. It implements the same error
        // compensation MTA does - correct the DIFFERENCE between where the game
        // has put an entity and where its owner says it is, a share at a time,
        // rather than lerping the position and fighting the game's own physics.
        // It also carries rotation as a quaternion slerp and scales the
        // compensation by how late an update was, neither of which a
        // reimplementation here would have had.
        //
        // Vehicles will want exactly this too, which is the other reason not to
        // grow a private copy in the player path.
        std::unordered_map<int, Framework::Utils::Interpolator> gInterpolationBySlot;

        // The last state actually aimed at, per slot.
        //
        // UpdatePlayerSync runs EVERY FRAME but state arrives ten times a
        // second, so five frames in six carry the same numbers as the last.
        // Re-aiming on those restarts the interpolation from wherever the ped
        // has got to, towards the same target, with a fresh deadline - so it
        // never finishes converging and the ped trails its owner permanently.
        //
        // Comparing against what was last aimed at is what makes a new target
        // new. Position and rotation together, because either can change alone.
        struct AimedAt {
            glm::vec3 position {};
            glm::quat rotation {};

            // What the owner was doing when they reported it, for the warp
            // threshold - a sprinter covers more ground between two updates
            // than a walker, so the gap that counts as absurd is not fixed.
            float speed = 0.0f;

            // When this slot was last aimed, so it can be re-aimed on a cadence
            // rather than only when the numbers change.
            std::uint32_t whenMs = 0;

            // How many updates this slot has had. Counts the velocity gating
            // below, which follows MTA in applying move speed on the ground only
            // every fourth update.
            std::uint32_t updates = 0;

            // The heading this ped is turning FROM and TO, and the window it has
            // to do it in. See the note where it is advanced.
            float headingFrom = 0.0f;
            float headingTo   = 0.0f;

            std::uint32_t headingBeginMs = 0;
            std::uint32_t headingEndMs   = 0;

            // Camera and gun aim, lerped in the same window. Snapping either is
            // what makes a remote player who is aiming stutter at the hands:
            // the IK follows these, not the feet.
            float cameraFrom = 0.0f;
            float cameraTo   = 0.0f;
            float aimHFrom   = 0.0f;
            float aimHTo     = 0.0f;
            float aimVFrom   = 0.0f;
            float aimVTo     = 0.0f;

            std::uint32_t viewBeginMs = 0;
            std::uint32_t viewEndMs   = 0;

            // Analog pad, lerped across the send interval. Snapping the
            // stick is what makes a remote car stutter when it turns:
            // ProcessControl rebuilds steer from this every frame, so a
            // jump from 0 to full lock every 100ms yaws the whole cabin.
            // Buttons stay snapped - those need a rising edge.
            float stickXFrom = 0.0f;
            float stickXTo   = 0.0f;
            float stickYFrom = 0.0f;
            float stickYTo   = 0.0f;
            float accelFrom  = 0.0f;
            float accelTo    = 0.0f;
            float brakeFrom  = 0.0f;
            float brakeTo    = 0.0f;

            std::uint32_t analogBeginMs = 0;
            std::uint32_t analogEndMs   = 0;
            bool          analogAimed   = false;

            // A gun is up, so the heading lerp must not write the upper-body
            // target - that field is the IK's while aiming.
            bool gunUp = false;

            bool valid = false;
        };

        std::unordered_map<int, AimedAt> gAimedBySlot;

        // Canonical NetworkID <-> compact wire id for players. Separate
        // registry instance from the vehicle one: a slot number means nothing
        // across entity kinds, and packets are already typed by their RPC
        // identifier, so there is nothing to disambiguate on the wire.
        Shared::RPC::Common::WireRegistry gPlayerWireIds;

        // Newest sequence applied per player, per stream. Movement and aim
        // carry SEPARATE sequences because they are separate streams at
        // separate cadences - sharing one would drop every aim update that
        // arrived between two movement packets.
        struct AppliedSequence {
            Shared::RPC::Sequence onFoot = 0;
            std::uint16_t aim        = 0;
            bool          haveOnFoot = false;
            bool          haveAim    = false;
        };

        std::unordered_map<std::uint64_t, AppliedSequence> gAppliedSequence;


        // The default player model. CJ, which is what the local player wears
        // until a server says otherwise.
        constexpr int kDefaultModel = 7;

        // Remote players start at slot 2.
        //
        // SLOT 1 IS SKIPPED DELIBERATELY. It is a leftover from the game's
        // two-player co-op mode and is not treated like the others - it has its
        // own handling in places, which shows up as one player behaving
        // differently to everyone else for no visible reason.
        constexpr int kFirstRemoteSlot = 2;

        int ClaimSlot() {
            for (int slot = kFirstRemoteSlot; slot < kMaxPlayers; ++slot) {
                if (!PedInSlot(slot)) {
                    return slot;
                }
            }

            return -1;
        }

        // A quaternion down to the one angle GTA cares about.
        //
        // The wire carries a full rotation because that is what the framework's
        // transform tier replicates, but a ped on foot only ever yaws - so
        // anything else in it describes a pose the game cannot adopt anyway.
        float HeadingFrom(const glm::quat &rotation) {
            // glm::ROLL, NOT glm::yaw.
            //
            // GLM names euler angles by AXIS, not by what they mean for a
            // character: pitch is X, yaw is Y, roll is Z. GTA's heading is a
            // rotation about Z, so the accessor that reads it back is roll.
            //
            // glm::yaw here is not merely inaccurate, it is identically zero
            // for a Z-only quaternion - so every remote ped faced due north
            // regardless of where its owner was looking.
            return glm::roll(rotation);
        }

        glm::quat HeadingTo(float heading) {
            return glm::quat(glm::vec3(0.0f, 0.0f, heading));
        }

        // The signed short way round from one heading to another.
        //
        // THE SHORT WAY, because headings wrap. A ped at 179 degrees told to
        // face -179 has moved two degrees, not three hundred and fifty eight,
        // and interpolating the plain difference would spin it the whole way
        // round the wrong way once per crossing.
        //
        // This is MTA's GetOffsetRadians, which its rotation interpolation is
        // built on.
        float OffsetRadians(float from, float to) {
            constexpr float kPi  = 3.14159265358979323846f;
            constexpr float kTau = 2.0f * kPi;

            float offset = to - from;

            if (offset > kPi) {
                offset -= kTau;
            } else if (offset <= -kPi) {
                offset += kTau;
            }

            return offset;
        }

        float LerpRadians(float from, float to, std::uint32_t beginMs, std::uint32_t endMs, std::uint32_t now) {
            if (endMs == 0 || now >= endMs || endMs <= beginMs) {
                return to;
            }

            const float span     = static_cast<float>(endMs - beginMs);
            const float progress = static_cast<float>(now - beginMs);

            return from + OffsetRadians(from, to) * (progress / span);
        }

        float LerpAmount(float from, float to, std::uint32_t beginMs, std::uint32_t endMs, std::uint32_t now) {
            if (endMs == 0 || now >= endMs || endMs <= beginMs) {
                return to;
            }

            const float span     = static_cast<float>(endMs - beginMs);
            const float progress = static_cast<float>(now - beginMs);

            return from + (to - from) * (progress / span);
        }

        void WriteLerpedRemotePad(int slot, AimedAt &aimed, const Shared::PlayerEntity *entity) {
            const std::uint32_t now = GetTickCount();

            const bool analogChanged = !aimed.analogAimed || aimed.stickXTo != static_cast<float>(entity->leftStickX) ||
                aimed.stickYTo != static_cast<float>(entity->leftStickY) || aimed.accelTo != static_cast<float>(entity->accelerator) ||
                aimed.brakeTo != static_cast<float>(entity->brake);

            if (!aimed.analogAimed) {
                aimed.stickXFrom = aimed.stickXTo = static_cast<float>(entity->leftStickX);
                aimed.stickYFrom = aimed.stickYTo = static_cast<float>(entity->leftStickY);
                aimed.accelFrom = aimed.accelTo = static_cast<float>(entity->accelerator);
                aimed.brakeFrom = aimed.brakeTo = static_cast<float>(entity->brake);
                aimed.analogBeginMs             = now;
                aimed.analogEndMs               = now;
                aimed.analogAimed               = true;
            }
            else if (analogChanged) {
                aimed.stickXFrom = LerpAmount(aimed.stickXFrom, aimed.stickXTo, aimed.analogBeginMs, aimed.analogEndMs, now);
                aimed.stickYFrom = LerpAmount(aimed.stickYFrom, aimed.stickYTo, aimed.analogBeginMs, aimed.analogEndMs, now);
                aimed.accelFrom  = LerpAmount(aimed.accelFrom, aimed.accelTo, aimed.analogBeginMs, aimed.analogEndMs, now);
                aimed.brakeFrom  = LerpAmount(aimed.brakeFrom, aimed.brakeTo, aimed.analogBeginMs, aimed.analogEndMs, now);
                aimed.stickXTo   = static_cast<float>(entity->leftStickX);
                aimed.stickYTo   = static_cast<float>(entity->leftStickY);
                aimed.accelTo    = static_cast<float>(entity->accelerator);
                aimed.brakeTo    = static_cast<float>(entity->brake);
                aimed.analogBeginMs = now;
                aimed.analogEndMs   = now + Shared::SyncRate::kPlayerMs;
            }

            SDK::ControllerInput input;
            input.leftStickX  = static_cast<std::int16_t>(std::lround(LerpAmount(aimed.stickXFrom, aimed.stickXTo, aimed.analogBeginMs, aimed.analogEndMs, now)));
            input.leftStickY  = static_cast<std::int16_t>(std::lround(LerpAmount(aimed.stickYFrom, aimed.stickYTo, aimed.analogBeginMs, aimed.analogEndMs, now)));
            input.accelerator = static_cast<std::uint8_t>(std::lround(LerpAmount(aimed.accelFrom, aimed.accelTo, aimed.analogBeginMs, aimed.analogEndMs, now)));
            input.brake       = static_cast<std::uint8_t>(std::lround(LerpAmount(aimed.brakeFrom, aimed.brakeTo, aimed.analogBeginMs, aimed.analogEndMs, now)));
            input.buttons     = entity->buttons;

            SDK::WriteRemoteInput(slot, input);
        }

        // Copies the local player into the entity the server sees.
        // --- Outbound player packets ----------------------------------------
        //
        // Thresholds below which a change is not worth a field on the wire.
        // Each sits under what a receiver can show after interpolation.
        constexpr float kPlayerPositionEpsilon = 0.01f;   // 1 cm
        constexpr float kPlayerVelocityEpsilon = 0.05f;   // 5 cm/s
        constexpr float kPlayerAngleEpsilon    = 0.002f;  // ~0.11 deg

        // KEYFRAME, BECAUSE THE STREAM IS UNRELIABLE. Deltas compare against
        // what was last SENT, and an unreliable packet may never have landed -
        // so without this a receiver that missed the one packet carrying a
        // field would hold the wrong value indefinitely. As with vehicles this
        // bounds the SEND interval, not the divergence: a keyframe drops like
        // anything else, so real recovery is a multiple of it.
        constexpr std::uint32_t kPlayerKeyframeMs = 1000;

        struct LocalSendState {
            Shared::RPC::Sequence sequence = 0;

            glm::vec3 sentPosition {};
            glm::vec3 sentVelocity {};
            float     sentHeading = 0.0f;
            float     sentCamera  = 0.0f;

            std::int16_t  sentStickX = 0;
            std::int16_t  sentStickY = 0;
            std::uint8_t  sentAccel  = 0;
            std::uint8_t  sentBrake  = 0;
            std::uint16_t sentButtons = 0;
            std::uint8_t  sentFlags   = 0xFF;

            std::uint32_t lastKeyframeMs = 0;
            bool          primed         = false;


            float         sentAimH    = 0.0f;
            float         sentAimV    = 0.0f;
            bool          aimPrimed   = false;
        };

        LocalSendState gLocalSend;

        // Our own compact id, learned from the server. Until it arrives the
        // replica still carries everything, so nothing is lost by waiting.
        Shared::RPC::Common::WireId gLocalWireId {};

        [[nodiscard]] std::uint8_t PackPedFlags(const SDK::LocalPedState &local) {
            namespace Flag = Shared::RPC::Player::PedFlag;

            std::uint8_t flags = 0;

            if (local.ducked) {
                flags |= Flag::kDucked;
            }

            if (local.inAir) {
                flags |= Flag::kInAir;
            }

            if (local.inWater) {
                flags |= Flag::kInWater;
            }

            return flags;
        }

        void SendOnFootSync(Shared::PlayerEntity *entity, const SDK::LocalPedState &local, const SDK::ControllerInput &input, const SDK::PedAim &aim,
                            bool seated) {
            namespace Field = Shared::RPC::Player::OnFootField;

            (void)entity;

            if (!gLocalWireId.Valid()) {
                return;
            }

            // SEATED PLAYERS SEND NO MOVEMENT AT ALL. Their transform is the
            // vehicle's and VehicleDriverSync carries it; the input they are
            // feeding the car travels there too. Sending an on-foot packet as
            // well would be the duplication this protocol exists to remove.
            if (seated) {
                gLocalSend.primed = false;
                return;
            }

            auto *net = Framework::CoreModules::GetNetworkPeer();

            if (!net) {
                return;
            }

            const std::uint32_t now      = GetTickCount();
            const bool          keyframe = !gLocalSend.primed || (now - gLocalSend.lastKeyframeMs) >= kPlayerKeyframeMs;

            const glm::vec3 position(local.x, local.y, local.z);
            const glm::vec3 velocity(local.velocityX, local.velocityY, local.velocityZ);
            const float     camera = SDK::ReadLocalCameraRotation();
            const auto      flags  = PackPedFlags(local);

            Shared::RPC::PlayerOnFootSync sync;

            sync.playerId = gLocalWireId;
            sync.sequence = static_cast<std::uint16_t>(gLocalSend.sequence + 1);

            if (keyframe || glm::distance(position, gLocalSend.sentPosition) > kPlayerPositionEpsilon) {
                sync.fields |= Field::kPosition;
                sync.position = position;
            }

            if (keyframe || std::fabs(local.heading - gLocalSend.sentHeading) > kPlayerAngleEpsilon) {
                sync.fields |= Field::kHeading;
                sync.heading = local.heading;
            }

            if (keyframe || glm::distance(velocity, gLocalSend.sentVelocity) > kPlayerVelocityEpsilon) {
                sync.fields |= Field::kVelocity;
                sync.velocity = velocity;
            }

            // THE CAMERA TRAVELS WITH THE INPUT, not on its own: a stick is
            // resolved against the view, so the two are one reading. Note
            // none of these tests look at the VALUE - a stick returning to
            // centre is a change and must be sent.
            if (keyframe || input.leftStickX != gLocalSend.sentStickX || input.leftStickY != gLocalSend.sentStickY
                || input.accelerator != gLocalSend.sentAccel || input.brake != gLocalSend.sentBrake || entity->buttons != gLocalSend.sentButtons
                || std::fabs(camera - gLocalSend.sentCamera) > kPlayerAngleEpsilon) {
                sync.fields |= Field::kInput;
                sync.leftStickX     = input.leftStickX;
                sync.leftStickY     = input.leftStickY;
                sync.accelerator    = input.accelerator;
                sync.brake          = input.brake;
                sync.buttons        = entity->buttons;
                sync.cameraRotation = camera;
            }

            // AIM ONLY WHILE ACTUALLY AIMING, which is MTA's rule: keysync and
            // puresync both write the arm directions only when
            // RightShoulder1 || ButtonCircle is down (CNetAPI.cpp:1128). A
            // player running across town has a stale aim nobody can see, and
            // paying four bytes a tick for it - let alone a second packet -
            // buys nothing.
            const bool aiming = (entity->buttons & (SDK::Button::Fire | SDK::Button::Aim)) != 0;

            if (aiming
                && (keyframe || std::fabs(aim.horizontal - gLocalSend.sentAimH) > kPlayerAngleEpsilon
                    || std::fabs(aim.vertical - gLocalSend.sentAimV) > kPlayerAngleEpsilon)) {
                sync.fields |= Field::kAim;
                sync.aimHorizontal = aim.horizontal;
                sync.aimVertical   = aim.vertical;
            }

            if (keyframe || flags != gLocalSend.sentFlags) {
                sync.fields |= Field::kFlags;
                sync.flags = flags;
            }

            if (sync.fields == 0) {
                return;
            }

            MafiaNet::BitStream bs;

            sync.Serialize(&bs, true);
            net->BroadcastRPC(sync, MafiaNet::Priority::High, MafiaNet::Reliability::Unreliable);

            gLocalSend.sequence = sync.sequence;

            if (keyframe) {
                gLocalSend.lastKeyframeMs = now;
            }

            // Only what was actually sent moves the baseline. A suppressed
            // field must keep comparing against what the receiver last heard.
            if (sync.fields & Field::kPosition) {
                gLocalSend.sentPosition = position;
            }

            if (sync.fields & Field::kHeading) {
                gLocalSend.sentHeading = local.heading;
            }

            if (sync.fields & Field::kVelocity) {
                gLocalSend.sentVelocity = velocity;
            }

            if (sync.fields & Field::kInput) {
                gLocalSend.sentStickX  = input.leftStickX;
                gLocalSend.sentStickY  = input.leftStickY;
                gLocalSend.sentAccel   = input.accelerator;
                gLocalSend.sentBrake   = input.brake;
                gLocalSend.sentButtons = entity->buttons;
                gLocalSend.sentCamera  = camera;
            }

            if (sync.fields & Field::kAim) {
                gLocalSend.sentAimH = aim.horizontal;
                gLocalSend.sentAimV = aim.vertical;
            }

            if (sync.fields & Field::kFlags) {
                gLocalSend.sentFlags = flags;
            }

            gLocalSend.primed = true;

            Shared::RPC::Metrics::Counters::Get().Record(Shared::RPC::Metrics::PacketType::PlayerOnFootSync, bs.GetNumberOfBytesUsed());
        }

        void PushLocalState(Shared::PlayerEntity *entity) {
            if (!IsPlayerSpawned()) {
                return;
            }

            const auto local = SDK::QueryLocalPed();

            if (!local.valid) {
                return;
            }

            // A SEATED PED'S TRANSFORM IS THE VEHICLE'S. DO NOT SEND IT TWICE.
            //
            // While this player is in a seat, every receiver positions their
            // ped by seating it in the car (SeatRemoteInVehicle) and never
            // reads entity->position at all - GTA puts a seated ped where the
            // vehicle is. So pushing the ped pose as well was ~41 bytes a tick
            // of a fact already travelling in VehicleDriverSync, and the two
            // could even disagree for a frame because they are independent
            // messages with independent loss.
            //
            // ONE FACT, ONE SOURCE OF TRUTH: the vehicle packet owns the
            // transform while driving; the player replica owns it on foot.
            // Everything else below (health, weapon, aim, input) is genuinely
            // player state and keeps flowing either way.
            //
            // Queried here rather than reusing the occupancy read further down
            // because the pose write has to happen before it, and one GTA read
            // is cheaper than reordering the whole function.
            const auto seat = SDK::QueryPedOccupancy(0);

            if (!seat.seated) {
                entity->position = glm::vec3(local.x, local.y, local.z);
                entity->rotation = HeadingTo(local.heading);
            }

            entity->health = local.health;
            entity->armour = local.armour;

            const auto weapon = SDK::QueryLocalPedWeapon();

            entity->weaponType  = weapon.type;
            entity->ammoClip    = weapon.ammoClip;
            entity->ammoTotal   = weapon.ammoTotal;
            entity->isReloading = weapon.reloading;

            const auto aim = SDK::ReadLocalPedAim();

            entity->aimHorizontal = aim.horizontal;
            entity->aimVertical   = aim.vertical;

            // VELOCITY IS SENT, AND IT IS NOT REDUNDANT WITH THE INPUT.
            //
            // A remote ped given only input and a position has no momentum of
            // its own: between two updates it is dragged by the correction
            // alone, so it trails its owner by however far they travelled in
            // that interval - metres, at 100ms. Handing the game a move speed
            // lets the ped carry itself between updates, which is what MTA does
            // (SetMoveSpeed before the position target) and what closes the gap.
            //
            // ON FOOT ONLY, for the same reason as the pose above: a seated
            // ped's momentum is the vehicle's, and VehicleDriverSync already
            // carries it.
            if (!seat.seated) {
                entity->velocity = glm::vec3(local.velocityX, local.velocityY, local.velocityZ);
            }

            entity->modelId = static_cast<std::uint32_t>(local.modelIndex);

            // WHAT MAKES A REMOTE PLAYER WALK RATHER THAN GLIDE. GTA derives
            // animation, speed, crouching and strafing from controller input,
            // so replaying it on the other client reproduces the movement with
            // a correct walk cycle. Position alone leaves the player sliding
            // with their legs still on every other screen.
            const auto input = SDK::ReadLocalInput();

            entity->leftStickX  = input.leftStickX;
            entity->leftStickY  = input.leftStickY;
            entity->accelerator = input.accelerator;
            entity->brake       = input.brake;
            entity->buttons     = input.buttons;

            // SENT WITH THE INPUT, BECAUSE IT IS WHAT THE INPUT MEANS. A stick
            // pushed forward means "away from the camera", so the two are one
            // reading: pair this tick's stick with last tick's view and the
            // player moves in a direction neither of them described.
            entity->cameraRotation = SDK::ReadLocalCameraRotation();

            entity->isDucked  = local.ducked;
            entity->isInAir   = local.inAir;
            entity->isInWater = local.inWater;

            // The same read taken at the top of this function for the pose
            // gate. One GTA query per tick, not two.
            const auto &occupancy = seat;

            // Keep the id while they climb out. Clearing it the moment the
            // leave task starts is what made remotes warp-exit from the
            // snapshot and skip the door animation. MTA keeps OccupiedVehicle
            // until NOTIFY_OUT.
            if (occupancy.seated || occupancy.entering || occupancy.leaving) {
                entity->vehicleNetworkId = VehicleNetworkIdOf(occupancy.vehicle);
                entity->vehicleSeat      = occupancy.seat;
            }
            else {
                entity->vehicleNetworkId = 0;
                entity->vehicleSeat      = 0;
            }

            // Area code is a GTA fact. Dimension is not — leave the replica
            // alone so a script write the owner was told about survives.
            entity->interior     = local.interior;
            gLocalWorld.interior  = local.interior;
            gLocalWorld.dimension = entity->dimension;

            // Money, freeze and collision are script-owned. Wanted,
            // fighting style and oxygen are GTA facts — cops, fists
            // and swimming change them, so the owner reports them.
            entity->wantedLevel    = SDK::LocalPlayerWanted();
            entity->fightingStyle  = SDK::LocalPedFightingStyle();
            entity->oxygen         = SDK::LocalPlayerOxygen();
            entity->walkingStyle   = SDK::LocalPedWalkingStyle();
            entity->onFire         = SDK::LocalPedOnFire();
            entity->isDrowning     = local.drowning;
            // maxHealth / jetpack are script-owned — echo, do not invent from GTA.
            SDK::SetLocalPedAlpha(entity->alpha);
            SDK::SetLocalPedJetPack(entity->hasJetPack);

            // Movement, input and aim go out as their own packets; everything
            // written above stays on the replica, where an unchanged field
            // costs one bit.
            SendOnFootSync(entity, local, input, aim, seat.seated);

            // Tells every receiver the pose is coming from the packet instead.
            // Only true while we actually have a wire id AND are on foot: a
            // seated player sends no on-foot sync, so the replica has to carry
            // them again (the vehicle carries the transform, but the ped still
            // needs a pose when they get out).
            entity->onFootSyncActive = gLocalWireId.Valid() && !seat.seated;
        }

        // Applies a remote entity to its ped, creating one if needed.
        void ApplyRemoteState(Shared::PlayerEntity *entity) {
            const auto networkId = static_cast<std::uint64_t>(entity->GetNetworkID());

            auto found = gSlotByNetworkId.find(networkId);

            bool justCreated = false;

            if (found == gSlotByNetworkId.end()) {
                const int slot = ClaimSlot();

                if (slot < 0) {
                    Framework::Logging::GetLogger("Sync")->warn("No free slot for player {}; they will be invisible", networkId);
                    return;
                }

                const int model = entity->modelId != 0 ? static_cast<int>(entity->modelId) : kDefaultModel;

                if (!SDK::CreateRemotePed(slot, model, entity->position.x, entity->position.y, entity->position.z)) {
                    Framework::Logging::GetLogger("Sync")->error("Could not create a ped for player {}", networkId);
                    return;
                }

                found       = gSlotByNetworkId.emplace(networkId, slot).first;
                justCreated = true;
            }

            const int slot = found->second;

            // THE OWNER SAYS ON FOOT, OUR COPY IS STILL SEATED.
            //
            // This branch owns the ped completely - it returns before the
            // position correction below ever runs, on the reasoning that a
            // seated ped's position comes from the vehicle. That holds while
            // both machines agree.
            //
            // A knock-off breaks the agreement. The owner's rider is thrown
            // clear, so vehicleNetworkId goes to 0, but our copy is still in
            // the seat, so IsPedOccupyingVehicle is still true and we take
            // this branch anyway - and return without ever correcting their
            // position. The remote ped stayed welded to the bike, or stood
            // wherever the seat left them, drifting further every packet.
            //
            // MTA guards the same way round: CClientPed::SetPosition only
            // skips the write when GetRealOccupiedVehicle() is non-null
            // (CClientPed.cpp:1070, 1128) - the ped's ACTUAL vehicle, not the
            // one the network last mentioned. When those disagree the
            // position write goes through.
            //
            // So: only skip the position correction while the owner still
            // claims a seat. Once they report on foot, fall through and
            // correct them, whatever our local copy still believes.
            const bool ownerInVehicle = entity->vehicleNetworkId != 0;

            if (ownerInVehicle || SDK::IsPedOccupyingVehicle(slot)) {
                const auto occupancy = SDK::QueryPedOccupancy(slot);

                // Only a seated ped loses collision. Doing it while they
                // walk to the door freezes the enter task in place.
                SDK::SetRemotePedCollision(slot, entity->collisions && !occupancy.seated);
                SDK::SetRemotePedHealth(slot, entity->health, entity->armour);
                SDK::SetRemotePedModel(slot, entity->modelId != 0 ? static_cast<int>(entity->modelId) : 0);
                SDK::SetRemotePedFrozen(slot, entity->frozen);
                SDK::SetRemotePedFightingStyle(slot, entity->fightingStyle);
                SDK::SetRemotePedOxygen(slot, entity->oxygen);
                SDK::SetRemotePedWalkingStyle(slot, entity->walkingStyle);
                SDK::SetRemotePedOnFire(slot, entity->onFire);
                SDK::SetRemotePedJetPack(slot, entity->hasJetPack);
                SDK::SetRemotePedAlpha(slot, entity->alpha);
                gRemoteAlpha[slot] = entity->alpha;
                SDK::SetRemotePedWeapon(slot, entity->weaponType, entity->ammoClip, entity->ammoTotal);

                if (entity->vehicleNetworkId != 0) {
                    // MTA: stream-in / late join warps. A ped that already
                    // existed on foot is walked in by PlayerEnterVehicle.
                    SeatRemoteInVehicle(slot, entity->vehicleNetworkId, entity->vehicleSeat, justCreated);
                }
                else if (occupancy.leaving || occupancy.entering) {
                    // The RPC owns the animation. Warping here is why the
                    // leave door did not always play: server Vacate clears
                    // the id immediately, and this snapshot arrived mid-task.
                }
                else if (occupancy.seated) {
                    // BEING JACKED. GTA is dragging them out through the
                    // jacker's EnterCarAsDriver. LeaveCar here fights that
                    // and leaves them stuck on the back of the car.
                    //
                    // ALSO: not while they are being thrown off. The exit RPC
                    // already raised CEventKnockOffBike, which ejects them
                    // over the next few frames. Calling ExitVehicle here as
                    // well is a SECOND ejection racing the first - it builds a
                    // LeaveCar over a ped mid-tumble, which is why the fall
                    // looked right once and wrong afterwards.
                    if (!SDK::IsAnyoneEnteringVehicleAsDriver(occupancy.vehicle) && !SDK::PedIsFallingOffBike(slot)) {
                        SDK::ExitVehicle(slot, false);
                        SDK::SetRemotePedCollision(slot, true);
                    }
                }

                // THE PAD, EVEN IN A SEAT. Vehicle ProcessControl reads
                // GetPad(0), which we swap to this slot's pad. Without this
                // write the remote driver holds nothing and the wheels stay
                // straight - writing m_fSteerAngle does not survive that
                // recompute. Analog is lerped across the send interval so
                // a turn is a ramp, not a 100ms snap of the cabin.
                WriteLerpedRemotePad(slot, gAimedBySlot[slot], entity);
                SDK::SetRemotePedInterior(slot, entity->interior);

                // Only the vehicle owns this ped's position while the OWNER
                // agrees they are in it. Otherwise fall through to the
                // correction below - see the note above.
                if (ownerInVehicle || occupancy.entering || occupancy.leaving) {
                    return;
                }

            }
            else {
                SDK::SetRemotePedCollision(slot, true);
            }

            // AIMED AT, NOT SNAPPED TO.
            //
            // The ped is given a target and a deadline, and moved towards it a
            // frame at a time by CorrectRemotePlayer. Writing the position
            // here instead puts the ped in a new place ten times a second and
            // leaves it still in between - which is what makes a remote player
            // read as teleporting rather than walking.
            //
            // The game's own processing does the walking, driven by the input in
            // the entity; this only corrects where that walking has drifted from
            // what the owner reports.
            // THE ORDER BELOW IS MTA'S, AND IT IS NOT ARBITRARY.
            //
            // ReadPlayerPuresync applies move speed, then health and armour,
            // then the position target and rotation, and only THEN the
            // controller state, camera and pose. Input applied before the
            // transform acts on the previous frame's position; velocity applied
            // after it fights the correction that was just aimed.

            // WHAT ARRIVES WITH AN UPDATE IS APPLIED ON AN UPDATE.
            //
            // Momentum, vitals and the transform are per-packet facts in MTA and
            // are applied once per packet there. Writing them every frame instead
            // reapplies the same ten-times-a-second numbers sixty times a second,
            // which for velocity means overwriting the move speed GTA just
            // computed from the walk animation - sixty times a second, against a
            // ped the game is already moving from the replayed input.
            //
            // Input, camera and pose stay per-frame below: the pad is consumed
            // each time the ped is processed, so it has to be rewritten.
            auto &aimed = gAimedBySlot[slot];



            const std::uint32_t now = GetTickCount();

            // RE-AIMED ON A CADENCE, NOT ONLY WHEN THE NUMBERS CHANGE.
            //
            // Change alone is not enough. The error compensation pays off the
            // gap it measured when it was aimed and then stops, so whatever is
            // left when a player stops moving stays there for good - measured at
            // a third of a metre, permanent, and invisible to a change test
            // because nothing is changing. MTA re-aims every tick regardless,
            // which closes that residue on the next update.
            //
            // Not every frame either: state arrives ten times a second and the
            // game draws sixty, so re-aiming on a frame carrying the same
            // numbers restarts the window from wherever the ped has got to
            // towards the same place, and it never finishes converging.
            //
            // The cadence is the rate state is sent at, which is what MTA's
            // tick rate is doing.
            const bool changed = !aimed.valid || aimed.position != entity->position || aimed.rotation != entity->rotation;
            const bool due     = aimed.valid && (now - aimed.whenMs) >= Shared::SyncRate::kPlayerMs;

            if (changed || due) {
                ++aimed.updates;

                // 1. Momentum, but NOT ON EVERY UPDATE WHILE ON FOOT.
                //
                // A ped standing on the ground gets its move speed from its own
                // walk animation, recomputed every frame. Overwriting that with
                // the owner's reading fights it: the ped is being carried by the
                // replayed input already, so the injected speed is movement
                // counted twice, and the correction then drags it back.
                //
                // In the AIR there is no animation driving it and the game is
                // integrating gravity, so the owner's velocity is the only thing
                // that makes a jump arc match - it goes every update.
                //
                // On the ground it still goes occasionally, to catch drift the
                // input replay alone would not. MTA's exact rule: every sync
                // while airborne, every fourth otherwise.
                if (entity->isInAir || (aimed.updates % 4u) == 0u) {
                    SDK::SetRemotePedVelocity(slot, entity->velocity.x, entity->velocity.y, entity->velocity.z);
                }

                // 2. Health, armour, and what is in their hands.
                SDK::SetRemotePedHealth(slot, entity->health, entity->armour);
                SDK::SetRemotePedModel(slot, entity->modelId != 0 ? static_cast<int>(entity->modelId) : 0);
                SDK::SetRemotePedFrozen(slot, entity->frozen);
                SDK::SetRemotePedCollision(slot, entity->collisions);
                SDK::SetRemotePedFightingStyle(slot, entity->fightingStyle);
                SDK::SetRemotePedOxygen(slot, entity->oxygen);
                SDK::SetRemotePedWalkingStyle(slot, entity->walkingStyle);
                SDK::SetRemotePedOnFire(slot, entity->onFire);
                SDK::SetRemotePedJetPack(slot, entity->hasJetPack);
                SDK::SetRemotePedAlpha(slot, entity->alpha);
                gRemoteAlpha[slot] = entity->alpha;

                // On the update path rather than per-frame. It costs nothing
                // when the weapon has not changed, but the frame path runs six
                // times per update and there is nothing here that needs it.
                SDK::SetRemotePedWeapon(slot, entity->weaponType, entity->ammoClip, entity->ammoTotal);

                if (entity->isReloading) {
                    SDK::ReloadRemotePedWeapon(slot);
                }

                // 3. The transform.

                const auto current = SDK::GetRemotePedPosition(slot);

                auto &interpolator = gInterpolationBySlot[slot];

                // FULL CORRECTION, NOT A QUARTER OF ONE.
                //
                // The framework scales the error by mix(0.25, 1.0, unlerp(min,
                // max, delay)), so at the default range of 100..400ms an update
                // arriving every 100ms lands exactly on the lower bound and gets
                // 0.25 - a quarter of the gap closed per update, for ever, which
                // is a remote player permanently metres behind where their owner
                // says they are. MTA scales the error by nothing at all:
                // vecError is target - current, whole.
                //
                // The range is a statement about the rates a project sends at,
                // and ours is 100ms. Putting the minimum below it puts our rate
                // inside the range rather than on its floor.
                interpolator.GetPosition()->SetErrorContributionDelayRange(0.0f, static_cast<float>(Shared::SyncRate::kPlayerMs));

                interpolator.GetPosition()->SetTargetValue(glm::vec3(current.x, current.y, current.z), entity->position, static_cast<float>(Shared::SyncRate::kPlayerMs));

                // Rotation is AIMED here and turned frame by frame in
                // CorrectRemotePlayer, the same shape as the position above.
                // From wherever the ped is actually facing now, so the turn
                // starts where the last one ended rather than from the last
                // value received.
                aimed.headingFrom    = HeadingFrom(SDK::GetRemotePedRotation(slot));
                aimed.headingTo      = HeadingFrom(entity->rotation);
                aimed.headingBeginMs = now;
                aimed.headingEndMs   = now + Shared::SyncRate::kPlayerMs;

                // Same window as heading. Starts from wherever the last lerp
                // left the value, so consecutive updates join up instead of
                // jumping back to the previous target.
                const auto shown = SDK::GetRemotePedAim(slot);

                aimed.cameraFrom  = SDK::GetRemoteCameraRotation(slot);
                aimed.cameraTo    = entity->cameraRotation;
                aimed.aimHFrom    = shown.horizontal;
                aimed.aimHTo      = entity->aimHorizontal;
                aimed.aimVFrom    = shown.vertical;
                aimed.aimVTo      = entity->aimVertical;
                aimed.viewBeginMs = now;
                aimed.viewEndMs   = now + Shared::SyncRate::kPlayerMs;

                aimed.position = entity->position;
                aimed.rotation = entity->rotation;
                aimed.speed    = glm::length(entity->velocity);
                aimed.whenMs   = now;
                aimed.valid    = true;
            }

            // 4. Input, AFTER the transform, so the game walks the ped from
            //    where it now is rather than from where it was last frame.
            //    Analog is lerped; discrete buttons still snap for edges.
            WriteLerpedRemotePad(slot, aimed, entity);

            // 5. The view, LERPED, not snapped. Camera rotation decides which
            //    way "forward" is, and the two aim angles decide where the
            //    hands point. All three used to be written straight from the
            //    last packet, ten times a second, which is invisible while
            //    walking and obvious the moment a gun comes up.
            aimed.gunUp = (entity->buttons & (SDK::Button::Aim | SDK::Button::Fire)) != 0;

            {
                const std::uint32_t nowView = GetTickCount();

                SDK::SetRemoteCameraRotation(slot, LerpRadians(aimed.cameraFrom, aimed.cameraTo, aimed.viewBeginMs, aimed.viewEndMs, nowView));

                SDK::SetRemotePedAim(slot, LerpRadians(aimed.aimHFrom, aimed.aimHTo, aimed.viewBeginMs, aimed.viewEndMs, nowView), LerpRadians(aimed.aimVFrom, aimed.aimVTo, aimed.viewBeginMs, aimed.viewEndMs, nowView));
            }

            // 6. Pose, LAST, and through the game's own tasks.
            //
            // NONE OF THESE ARE APPLIED BY WRITING THE FLAG, because all three
            // are outputs the game recomputes every frame from its own physics.
            // Each is either handed the task that produces it, or left alone.
            //
            // isInAir is left alone on purpose, and is not a gap. The ped is
            // falling because the game walked it off something; saying so again
            // changes nothing. Its real job is deciding whether velocity gets
            // applied above, which is what MTA uses its own copy for too.
            SDK::SetRemotePedDucked(slot, entity->isDucked);

            if (entity->isInWater && SDK::StartRemotePedSwimming(slot)) {
            }

            SDK::SetRemotePedInterior(slot, entity->interior);
        }
    } // namespace

    void UpdatePlayerSync() {
        PulseWorldEnv();

        auto *replication = Framework::CoreModules::GetReplication();

        if (!replication) {
            return;
        }

        // Which entities are still here, so the peds of the ones that are not
        // can be removed. Collected during the walk rather than from a destroy
        // callback: an entity can vanish for reasons other than a disconnect -
        // streaming out is the common one - and every reason looks the same to
        // the ped, which has to go either way.
        std::unordered_set<std::uint64_t> seen;

        replication->ForEachEntity([&seen](Replication::NetworkEntity *entity) {
            auto *player = dynamic_cast<Shared::PlayerEntity *>(entity);

            if (!player) {
                return;
            }

            if (player->IsOwner()) {
                gLocalNetworkId = static_cast<std::uint64_t>(player->GetNetworkID());

                // Pick up an id that was announced before we knew which entity
                // was ours. Cheap: a hash lookup once a tick until it resolves,
                // and nothing at all afterwards.
                if (!gLocalWireId.Valid()) {
                    gLocalWireId = gPlayerWireIds.Find(gLocalNetworkId);
                }

                PushLocalState(player);
                CheckLocalDeath();
                return;
            }

            // Other dimension: leave them out of `seen` so the stream-out
            // path below destroys a ped that was already here.
            if (!Shared::SameDimension(gLocalWorld.dimension, player->dimension)) {
                return;
            }

            seen.insert(static_cast<std::uint64_t>(player->GetNetworkID()));
            ApplyRemoteState(player);
        });


        for (auto it = gSlotByNetworkId.begin(); it != gSlotByNetworkId.end();) {
            if (seen.contains(it->first)) {
                ++it;
                continue;
            }

            gInterpolationBySlot.erase(it->second);
            gRemoteAlpha.erase(it->second);
            gAimedBySlot.erase(it->second);

            // The wire id and the applied sequence die with the player.
            // Leaving the mapping behind would let a packet still in flight
            // resolve to a player who is gone, and leaving the sequence would
            // make a REJOINING player's first packets look stale until their
            // counter climbed past the old one.
            gPlayerWireIds.Release(it->first);
            gAppliedSequence.erase(it->first);

            ClearRemoteOccupancy(it->second);
            SDK::DestroyRemotePed(it->second);
            it = gSlotByNetworkId.erase(it);
        }
    }

    void CorrectRemotePlayer(int slot) {
        if (const auto found = gRemoteAlpha.find(slot); found != gRemoteAlpha.end()) {
            SDK::SetRemotePedAlpha(slot, found->second);
        }

        if (SDK::IsPedOccupyingVehicle(slot)) {
            return;
        }

        // MID-FALL: GTA OWNS THIS PED, POSITION AND HEADING BOTH.
        //
        // CEventKnockOffBike applies a launch force and hands the ped to a
        // fall/get-up task. For that second or so they are tumbling under the
        // game's own physics, and every write from here fights it: the
        // position correction drags them back toward a target they are
        // deliberately flying away from, and the heading lerp above stands a
        // tumbling body upright.
        //
        // Standing off is safe because both machines run the SAME simulation
        // from the same starting state - the owner fell from the bike we have
        // synced all along. Any residual gap is paid off by the ordinary
        // correction once they are on their feet.
        if (SDK::PedIsFallingOffBike(slot)) {
            return;
        }

        const auto found = gInterpolationBySlot.find(slot);

        if (found == gInterpolationBySlot.end()) {
            return;
        }

        auto &interpolator = found->second;

        // TURNED FRAME BY FRAME, NOT SNAPPED TEN TIMES A SECOND.
        //
        // Heading used to be written once per update, straight to the value
        // received. In between, the game turned the ped with its own animation,
        // and then every hundred milliseconds this put it back - so a remote
        // player who was turning visibly stepped round in tenths.
        //
        // IT SHOWED UP IN THE WATER FIRST because that is where a turn is all
        // there is to see: the swim animation yaws the whole body with no
        // footfalls to hide the seam, and the ped is moving slowly enough that
        // the step is not mistaken for speed.
        //
        // An earlier note here said rotation must NOT be interpolated, on the
        // grounds that the write and the read addressed different things - the
        // write went to m_fCurrentRotation and the read came off the ped's
        // matrix, which the game rebuilds from that field only during its own
        // processing, so a correction was always measured against a stale value
        // and never converged. That was true when it was written and is not any
        // more: HeadingOf reads m_fCurrentRotation now, the same field
        // SetPedRotation writes, which is what the ninety degree fix was.
        //
        // With the read honest, this is what MTA has always done - see
        // CClientPed::Interpolate, which lerps from the heading held when the
        // update arrived to the one it carried, across the send interval.
        {
            auto aiming = gAimedBySlot.find(slot);

            if (aiming != gAimedBySlot.end() && aiming->second.headingEndMs != 0) {
                auto &aimed = aiming->second;

                if (const std::uint32_t now = GetTickCount(); now < aimed.headingEndMs) {
                    const float span     = static_cast<float>(aimed.headingEndMs - aimed.headingBeginMs);
                    const float progress = static_cast<float>(now - aimed.headingBeginMs);
                    const float offset   = OffsetRadians(aimed.headingFrom, aimed.headingTo);

                    SDK::SetRemotePedHeading(slot, aimed.headingFrom + offset * (progress / span), !aimed.gunUp);
                } else {
                    // Landed exactly, rather than left wherever the last frame
                    // of the window happened to fall.
                    SDK::SetRemotePedHeading(slot, aimed.headingTo, !aimed.gunUp);

                    aimed.headingEndMs = 0;
                }
            }
        }

        // WHERE THE GAME JUST PUT THIS PED, read after its own processing has
        // walked it. The interpolator returns that position plus a share of the
        // error, so the walk is kept and only the drift from what the owner
        // reports is taken out.
        const auto current = SDK::GetRemotePedPosition(slot);
        const glm::vec3 here(current.x, current.y, current.z);

        // WARPED, NOT SLID, WHEN THE GAP IS ABSURD.
        //
        // Correcting a share of the error at a time is right for the metre or
        // so a player drifts between updates. It is wrong for a player who has
        // teleported, respawned, or come back from a stall: the ped would walk
        // the whole way there in full view, through walls, taking as long as the
        // distance demands.
        //
        // The threshold scales with how fast the owner was going, because a
        // sprinter legitimately covers far more ground between two updates than
        // someone walking, and a fixed number either warps them constantly or
        // lets a real teleport slide. MTA's rule, and its numbers.
        if (const auto aimed = gAimedBySlot.find(slot); aimed != gAimedBySlot.end() && aimed->second.valid) {
            const float threshold = Shared::Interpolation::WarpThresholdFor(aimed->second.speed, Shared::SyncRate::kPlayerMs);

            if (const float gap = glm::distance(here, aimed->second.position); gap > threshold) {
                const auto &target = aimed->second.position;

                // COUNTED BEFORE THE EARLY RETURN. Warping used to leave through
                // here without recording anything, so the one event most worth
                // seeing was the one the telemetry could not show.


                SDK::SetRemotePedPosition(slot, target.x, target.y, target.z);

                // Aimed at itself, so the error left over from before the warp
                // is not paid off on top of a position that is already correct.
                interpolator.GetPosition()->SetTargetValue(target, target, static_cast<float>(Shared::SyncRate::kPlayerMs));

                return;
            }
        }

        const auto next = interpolator.GetPosition()->UpdateTargetValue(here);

        SDK::SetRemotePedPosition(slot, next.x, next.y, next.z);


    }

    void InstallRemotePlayerCorrection() {
        SetRemotePedCorrector(&CorrectRemotePlayer);

        Shared::gOnPlayerForced = [](Shared::PlayerEntity *player) {
            if (!player || !player->IsOwner()) {
                return;
            }

            SDK::SetLocalPedTransform(player->position.x, player->position.y, player->position.z, glm::roll(player->rotation));
        };

        Shared::gOnVehicleForced = [](Shared::VehicleEntity *vehicle) {
            if (!vehicle || !vehicle->IsOwner()) {
                return;
            }

            if (auto *handle = VehicleHandleOf(static_cast<std::uint64_t>(vehicle->GetNetworkID()))) {
                SDK::SetVehiclePose(handle, vehicle->position.x, vehicle->position.y, vehicle->position.z, vehicle->rotation);
                SDK::ApplyVehicleDamage(handle, vehicle->damageEngine, vehicle->doorsPacked, vehicle->wheelsPacked, vehicle->panels, vehicle->lights);
                SDK::ApplyVehicleDoorRatios(handle, vehicle->doorRatiosPacked, SDK::OccupiedDoorMask(handle));
                SDK::ApplyVehicleScriptState(handle, vehicle->engineOn, vehicle->sirenOn, vehicle->overrideLights, vehicle->locked);
                SDK::ApplyVehicleExtras(handle, vehicle->dirtLevel, vehicle->taxiLight, vehicle->headlightColor, vehicle->frozen, vehicle->damageProof,
                                        vehicle->collisions, vehicle->landingGearDown, vehicle->lightsOn, vehicle->engineBroken, vehicle->wheelsCanBurst,
                                        vehicle->canBeDamaged, vehicle->nitroCount);
                SDK::SetVehiclePlateText(handle, vehicle->plateText);
                SDK::SetVehicleAlpha(handle, vehicle->alpha);

                if (vehicle->IsBlown()) {
                    SDK::WreckVehicle(handle, true);
                }

                SDK::ApplyVehicleStates(handle, vehicle->health, vehicle->engineOn, vehicle->sirenOn);
            }
        };
    }

    void ClearRemotePlayers() {
        for (const auto &[networkId, slot] : gSlotByNetworkId) {
            ClearRemoteOccupancy(slot);
            SDK::DestroyRemotePed(slot);
        }

        gSlotByNetworkId.clear();
        gInterpolationBySlot.clear();
        gAimedBySlot.clear();
        gRemoteAlpha.clear();

        // A disconnect invalidates every mapping: the next server mints its
        // own id space, and our own id is no longer ours.
        gPlayerWireIds.Clear();
        gAppliedSequence.clear();
        gLocalWireId = Shared::RPC::Common::WireId {};
        gLocalSend   = LocalSendState {};
    }

    int SlotOfNetworkId(std::uint64_t networkId) {
        const auto found = gSlotByNetworkId.find(networkId);

        return found != gSlotByNetworkId.end() ? found->second : -1;
    }

    std::uint64_t NetworkIdOfSlot(int slot) {
        for (const auto &[networkId, occupied] : gSlotByNetworkId) {
            if (occupied == slot) {
                return networkId;
            }
        }

        return 0;
    }

    std::uint64_t LocalNetworkId() {
        return gLocalNetworkId;
    }

    LocalWorldSpace QueryLocalWorldSpace() {
        return gLocalWorld;
    }

    void ApplyOwnedWorldSpace(std::uint8_t interior, std::uint16_t dimension, std::uint8_t flags) {
        if ((flags & Shared::kWorldSpaceInterior) != 0) {
            SDK::SetCurrentInterior(interior);
            SDK::SetLocalPedInterior(interior);
            gLocalWorld.interior = interior;
        }

        if ((flags & Shared::kWorldSpaceDimension) != 0) {
            gLocalWorld.dimension = dimension;
        }

        if (auto *player = OwnedPlayer(Framework::CoreModules::GetReplication())) {
            if ((flags & Shared::kWorldSpaceInterior) != 0) {
                player->interior = interior;
            }

            if ((flags & Shared::kWorldSpaceDimension) != 0) {
                player->dimension = dimension;
            }
        }
    }

    void ApplyWorldSpace(const Shared::RPC::WorldSpace &space) {
        ApplyOwnedVehicleWorldSpace(space.networkId, space.interior, space.dimension, space.flags);

        auto *replication = Framework::CoreModules::GetReplication();
        auto *player      = OwnedPlayer(replication);

        if (!player || static_cast<std::uint64_t>(player->GetNetworkID()) != space.networkId) {
            if (space.networkId == 0 || space.networkId == gLocalNetworkId) {
                ApplyOwnedWorldSpace(space.interior, space.dimension, space.flags);
            }

            return;
        }

        ApplyOwnedWorldSpace(space.interior, space.dimension, space.flags);
    }

    void ApplyPlayerScript(const Shared::RPC::PlayerScript &script) {
        if ((script.flags & Shared::kPlayerScriptHealth) != 0 || (script.flags & Shared::kPlayerScriptArmour) != 0) {
            const auto local = SDK::QueryLocalPed();

            SDK::SetLocalPedVitals((script.flags & Shared::kPlayerScriptHealth) != 0 ? script.health : local.health,
                                   (script.flags & Shared::kPlayerScriptArmour) != 0 ? script.armour : local.armour);
        }

        if ((script.flags & Shared::kPlayerScriptModel) != 0 && script.modelId != 0) {
            SDK::SetLocalPedModel(static_cast<int>(script.modelId));
        }

        if ((script.flags & Shared::kPlayerScriptMoney) != 0) {
            SDK::SetLocalPlayerMoney(script.money);
        }

        if ((script.flags & Shared::kPlayerScriptWanted) != 0) {
            SDK::SetLocalPlayerWanted(script.wantedLevel);
        }

        if ((script.flags & Shared::kPlayerScriptFrozen) != 0) {
            SDK::SetLocalPedFrozen(script.frozen);
        }

        if ((script.flags2 & Shared::kPlayerScript2Collisions) != 0) {
            SDK::SetLocalPedCollision(script.collisions);
        }

        if ((script.flags2 & Shared::kPlayerScript2FightingStyle) != 0) {
            SDK::SetLocalPedFightingStyle(script.fightingStyle);
        }

        if ((script.flags2 & Shared::kPlayerScript2Oxygen) != 0) {
            SDK::SetLocalPlayerOxygen(script.oxygen);
        }

        if ((script.flags2 & Shared::kPlayerScript2WalkingStyle) != 0) {
            SDK::SetLocalPedWalkingStyle(script.walkingStyle);
        }

        if ((script.flags2 & Shared::kPlayerScript2Alpha) != 0) {
            SDK::SetLocalPedAlpha(script.alpha);
        }

        if ((script.flags2 & Shared::kPlayerScript2OnFire) != 0) {
            SDK::SetLocalPedOnFire(script.onFire);
        }

        if ((script.flags2 & Shared::kPlayerScript2MaxHealth) != 0) {
            SDK::SetLocalPedMaxHealth(script.maxHealth);
        }

        if ((script.flags2 & Shared::kPlayerScript2HasJetPack) != 0) {
            SDK::SetLocalPedJetPack(script.hasJetPack);
        }

        if (auto *player = OwnedPlayer(Framework::CoreModules::GetReplication())) {
            if ((script.flags & Shared::kPlayerScriptHealth) != 0) {
                player->health = script.health;
            }

            if ((script.flags & Shared::kPlayerScriptArmour) != 0) {
                player->armour = script.armour;
            }

            if ((script.flags & Shared::kPlayerScriptModel) != 0) {
                player->modelId = script.modelId;
            }

            if ((script.flags & Shared::kPlayerScriptMoney) != 0) {
                player->money = script.money;
            }

            if ((script.flags & Shared::kPlayerScriptWanted) != 0) {
                player->wantedLevel = script.wantedLevel;
            }

            if ((script.flags & Shared::kPlayerScriptFrozen) != 0) {
                player->frozen = script.frozen;
            }

            if ((script.flags2 & Shared::kPlayerScript2Collisions) != 0) {
                player->collisions = script.collisions;
            }

            if ((script.flags2 & Shared::kPlayerScript2FightingStyle) != 0) {
                player->fightingStyle = script.fightingStyle;
            }

            if ((script.flags2 & Shared::kPlayerScript2Oxygen) != 0) {
                player->oxygen = script.oxygen;
            }

            if ((script.flags2 & Shared::kPlayerScript2WalkingStyle) != 0) {
                player->walkingStyle = script.walkingStyle;
            }

            if ((script.flags2 & Shared::kPlayerScript2Alpha) != 0) {
                player->alpha = script.alpha;
            }

            if ((script.flags2 & Shared::kPlayerScript2OnFire) != 0) {
                player->onFire = script.onFire;
            }

            if ((script.flags2 & Shared::kPlayerScript2MaxHealth) != 0) {
                player->maxHealth = script.maxHealth;
            }

            if ((script.flags2 & Shared::kPlayerScript2HasJetPack) != 0) {
                player->hasJetPack = script.hasJetPack;
            }
        }
    }

    void AdoptPlayerWireId(std::uint64_t networkId, std::uint16_t wireId) {
        gPlayerWireIds.Adopt(networkId, Shared::RPC::Common::WireId {wireId});

        // OUR OWN ID MAY ARRIVE BEFORE WE KNOW WHICH ENTITY IS OURS.
        //
        // The server announces wire ids from OnPlayerReady, which can land
        // before UpdatePlayerSync has run and discovered the owned entity - so
        // gLocalNetworkId is still 0 and a `networkId == gLocalNetworkId` test
        // would miss. The mapping is kept regardless (above), and
        // ResolveLocalWireId below picks it up once the entity appears, so the
        // two can arrive in either order.
        if (gLocalNetworkId != 0 && networkId == gLocalNetworkId) {
            gLocalWireId = Shared::RPC::Common::WireId {wireId};
        }
    }

    void SetLocalPlayerWireId(std::uint16_t wireId) {
        gLocalWireId = Shared::RPC::Common::WireId {wireId};
    }

    namespace {
        // Resolves a wire id to the entity it names, refusing our own.
        //
        // The server broadcasts, so our own packets come back to us; applying
        // them would fight the local simulation with a 100 ms-old copy of it.
        [[nodiscard]] Shared::PlayerEntity *ResolveRemotePlayer(Shared::RPC::Common::WireId wireId, std::uint64_t &networkIdOut) {
            networkIdOut = gPlayerWireIds.Resolve(wireId);

            // A wire id that does not resolve names a player who has left, or
            // was minted before a slot was reused. Dropping it is the point of
            // the generation half of the id.
            if (networkIdOut == 0 || networkIdOut == gLocalNetworkId) {
                return nullptr;
            }

            auto *replication = Framework::CoreModules::GetReplication();

            if (!replication) {
                return nullptr;
            }

            auto *entity = dynamic_cast<Shared::PlayerEntity *>(replication->GetEntityByNetworkID(static_cast<MafiaNet::NetworkID>(networkIdOut)));

            return (entity && !entity->IsOwner()) ? entity : nullptr;
        }
    } // namespace

    void ApplyRemoteOnFootSync(const Shared::RPC::PlayerOnFootSync &sync) {
        namespace Field = Shared::RPC::Player::OnFootField;

        std::uint64_t networkId = 0;
        auto         *entity    = ResolveRemotePlayer(sync.playerId, networkId);

        if (!entity) {
            return;
        }

        auto &seen = gAppliedSequence[networkId];

        // LATEST WINS. The stream is Unreliable, so datagrams reorder: an
        // older snapshot applied after a newer one makes the ped jump back.
        if (seen.haveOnFoot && !Shared::RPC::IsNewerSequence(sync.sequence, seen.onFoot)) {
            return;
        }

        seen.onFoot     = sync.sequence;
        seen.haveOnFoot = true;

        // ONLY WHAT THE MASK NAMES. A suppressed field keeps the value we
        // hold - the sender suppressed it BECAUSE it still matches that
        // value, so overwriting it with a default would be the stale-state
        // bug this protocol is built to avoid.
        if (sync.Has(Field::kPosition)) {
            entity->position = sync.position;
        }

        if (sync.Has(Field::kHeading)) {
            entity->rotation = HeadingTo(sync.heading);
        }

        if (sync.Has(Field::kVelocity)) {
            entity->velocity = sync.velocity;
        }

        if (sync.Has(Field::kInput)) {
            entity->leftStickX     = sync.leftStickX;
            entity->leftStickY     = sync.leftStickY;
            entity->accelerator    = sync.accelerator;
            entity->brake          = sync.brake;
            entity->buttons        = sync.buttons;
            entity->cameraRotation = sync.cameraRotation;
        }

        // Aim is present only when the sender was actually aiming; absent
        // means "not aiming", NOT "aiming at zero".
        if (sync.Has(Field::kAim)) {
            entity->aimHorizontal = sync.aimHorizontal;
            entity->aimVertical   = sync.aimVertical;
        }

        if (sync.Has(Field::kFlags)) {
            namespace Flag = Shared::RPC::Player::PedFlag;

            entity->isDucked  = (sync.flags & Flag::kDucked) != 0;
            entity->isInAir   = (sync.flags & Flag::kInAir) != 0;
            entity->isInWater = (sync.flags & Flag::kInWater) != 0;
        }

        // ApplyRemoteState picks these up from the entity on the next frame,
        // exactly as it did when the replica carried them. Nothing below this
        // point knows the difference.
    }


    void ApplyPlayerWeapon(std::uint8_t action, std::uint32_t weaponType, std::uint32_t ammo) {
        if (action == Shared::RPC::PlayerWeapon::kTakeAll) {
            SDK::TakeAllLocalPedWeapons();
            return;
        }

        if (action == Shared::RPC::PlayerWeapon::kTake) {
            SDK::TakeLocalPedWeapon(weaponType);
            return;
        }

        if (action == Shared::RPC::PlayerWeapon::kSetAmmo) {
            SDK::SetLocalPedWeaponAmmo(weaponType, ammo);
            return;
        }

        if (action == Shared::RPC::PlayerWeapon::kSetSlot) {
            SDK::SetLocalPedWeaponSlot(weaponType);
            return;
        }

        SDK::GiveLocalPedWeapon(weaponType, ammo);
    }
} // namespace VantaMP::Game
