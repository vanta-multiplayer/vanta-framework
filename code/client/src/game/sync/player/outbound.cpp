#include "game/sync/player_sync.h"

#include "game/sync/player/state.h"
#include "game/sync/vehicle_sync.h"
#include "game/entities/local_player.h"
#include "sdk/game.h"
#include "shared/entity/player_entity.h"
#include "shared/rpc/common/metrics.h"
#include "shared/rpc/player/onfoot_sync.h"
#include "shared/world/world_space.h"

#include <utils/safe_win32.h>

#include <core_modules.h>
#include <networking/network_peer.h>

#include <cmath>

// WHAT THIS CLIENT SENDS ABOUT ITS OWN PLAYER.
//
// PlayerOnFootSync carries movement, input and - only while actually aiming -
// the two arm angles. Everything slower rides the replica, where an unchanged
// field costs one bit; putting it in a packet would cost a Huffman identifier
// to say the same thing.
//
// NOTHING IS SENT WHILE SEATED: a seated ped's transform is the vehicle's and
// VehicleDriverSync already carries it.
//
// Split out of player_sync.cpp. Shared per-slot state is in player/state.h.
namespace VantaMP::Game::PlayerSyncState {
    // The replica fields that are not movement: world placement plus the
    // slow-changing ped state.
    //
    // These ride the replica's delta channel, where an unchanged field
    // costs one bit - which is why they are NOT in PlayerOnFootSync. Some
    // are GTA facts the owner reports (wanted level, fighting style,
    // oxygen, drowning); the rest are script-owned and the owner only
    // echoes them back, never invents them from the game.
    void PushLocalWorldAndScriptState(Shared::PlayerEntity *entity, const SDK::LocalPedState &local) {
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
    }

    // What is in this player's hands, and where it is pointing.
    //
    // Returns the aim because the on-foot packet needs it too - reading it
    // twice would be two GTA queries for one fact.
    [[nodiscard]] SDK::PedAim PushLocalWeaponAndAim(Shared::PlayerEntity *entity) {
        const auto weapon = SDK::QueryLocalPedWeapon();

        entity->weaponType  = weapon.type;
        entity->ammoClip    = weapon.ammoClip;
        entity->ammoTotal   = weapon.ammoTotal;
        entity->isReloading = weapon.reloading;

        const auto aim = SDK::ReadLocalPedAim();

        entity->aimHorizontal = aim.horizontal;
        entity->aimVertical   = aim.vertical;

        return aim;
    }

    // The controller state and the pose flags GTA cannot derive from it.
    //
    // WHAT MAKES A REMOTE PLAYER WALK RATHER THAN GLIDE: GTA derives
    // animation, speed, crouching and strafing from controller input, so
    // replaying it on the other client reproduces the movement with a
    // correct walk cycle. Position alone leaves the player sliding with
    // their legs still on every other screen.
    //
    // The camera goes with the input because it is what the input MEANS - a
    // stick pushed forward means "away from the camera", so the two are one
    // reading. Pair this tick's stick with last tick's view and the player
    // moves in a direction neither of them described.
    //
    // Returns the input because the on-foot packet carries it as well.
    [[nodiscard]] SDK::ControllerInput PushLocalInputAndPose(Shared::PlayerEntity *entity, const SDK::LocalPedState &local) {
        const auto input = SDK::ReadLocalInput();

        entity->leftStickX  = input.leftStickX;
        entity->leftStickY  = input.leftStickY;
        entity->accelerator = input.accelerator;
        entity->brake       = input.brake;
        entity->buttons     = input.buttons;

        entity->cameraRotation = SDK::ReadLocalCameraRotation();

        entity->isDucked  = local.ducked;
        entity->isInAir   = local.inAir;
        entity->isInWater = local.inWater;

        return input;
    }

    // Copies the local player into the entity the server sees.
    // --- Outbound player packets ----------------------------------------
    //
    // Thresholds below which a change is not worth a field on the wire.
    // Each sits under what a receiver can show after interpolation.
    constexpr float kPlayerPositionEpsilon = 0.01f;   // 1 cm
    constexpr float kPlayerVelocityEpsilon = 0.05f;   // 5 cm/s
    constexpr float kPlayerAngleEpsilon    = 0.002f;  // ~0.11 deg

    // AIM IS HELD TIGHTER THAN HEADING, because it is what a weapon points
    // along. 0.11 deg is a fifth of a metre of miss at 100 m, which is fine
    // for which way a body faces and far too loose for where a gun is looking.
    // 0.029 deg is 5 cm at the same range, and still five times the 16-bit
    // quantizer step, so it cannot thrash on rounding alone.
    constexpr float kPlayerAimEpsilon      = 0.0005f; // ~0.029 deg

    // KEYFRAME, BECAUSE THE STREAM IS UNRELIABLE. Deltas compare against
    // what was last SENT, and an unreliable packet may never have landed -
    // so without this a receiver that missed the one packet carrying a
    // field would hold the wrong value indefinitely. As with vehicles this
    // bounds the SEND interval, not the divergence: a keyframe drops like
    // anything else, so real recovery is a multiple of it.
    constexpr std::uint32_t kPlayerKeyframeMs = 1000;




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

        // WRAPPED DIFFERENCE, NOT A RAW ONE. A heading runs -pi..pi, so a ped
        // turning through north goes 3.13 -> -3.13: a real movement of 0.02
        // rad that `fabs` reports as 6.26. That sent a full packet on every
        // crossing whether or not the player had actually turned.
        if (keyframe || std::fabs(OffsetRadians(gLocalSend.sentHeading, local.heading)) > kPlayerAngleEpsilon) {
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

        // WRAPPED DIFFERENCE FOR THE YAW. aim.horizontal is an ABSOLUTE world
        // angle (gta-reversed CPedIK::PointGunInDirection subtracts the ped's
        // own rotation internally: LimitRadianAngle(zAngle - m_fCurrentRotation)),
        // so it wraps at +/-pi like any heading. A raw fabs reports a player
        // tracking a target through north as a 6.28 rad swing.
        //
        // The pitch does not wrap - it is an arm angle, roughly -1..1 - so a
        // plain difference is correct there.
        if (aiming
            && (keyframe || std::fabs(OffsetRadians(gLocalSend.sentAimH, aim.horizontal)) > kPlayerAimEpsilon
                || std::fabs(aim.vertical - gLocalSend.sentAimV) > kPlayerAimEpsilon)) {
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

        const auto aim = PushLocalWeaponAndAim(entity);

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

        const auto input = PushLocalInputAndPose(entity, local);

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

        PushLocalWorldAndScriptState(entity, local);

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
} // namespace VantaMP::Game::PlayerSyncState
