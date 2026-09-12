#pragma once

// Shared state for the player sync subsystem.
//
// INTERNAL TO game/sync/player*. Same arrangement as the vehicle side: the
// subsystem is one per-slot registry that several files act on, and C++ has no
// way to share a translation unit. Nothing outside those files may include it;
// everyone else goes through player_sync.h.

#include "game/sync/player_sync.h"
#include "game/entities/remote_players.h"
#include "sdk/player/slots.h"
#include "shared/entity/player_entity.h"
#include "shared/rpc/common/sequence.h"
#include "shared/rpc/common/wire_registry.h"

#include <utils/interpolator.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <unordered_map>

namespace VantaMP::Game::PlayerSyncState {
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

    // Which slot each remote entity was given a ped in. Keyed by network id
    // rather than by pointer: an entity can be destroyed and its memory
    // reused, and a stale pointer would silently match the wrong player.
    inline std::unordered_map<std::uint64_t, int> gSlotByNetworkId;

    inline std::unordered_map<int, std::uint8_t>  gRemoteAlpha;

    inline std::uint64_t gLocalNetworkId = 0;

    inline LocalWorldSpace gLocalWorld {};

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
    inline std::unordered_map<int, Framework::Utils::Interpolator> gInterpolationBySlot;

    inline std::unordered_map<int, AimedAt> gAimedBySlot;

    // Canonical NetworkID <-> compact wire id for players. Separate
    // registry instance from the vehicle one: a slot number means nothing
    // across entity kinds, and packets are already typed by their RPC
    // identifier, so there is nothing to disambiguate on the wire.
    inline Shared::RPC::Common::WireRegistry gPlayerWireIds;

    inline std::unordered_map<std::uint64_t, AppliedSequence> gAppliedSequence;

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

    inline LocalSendState gLocalSend;

    // Our own compact id, learned from the server. Until it arrives the
    // replica still carries everything, so nothing is lost by waiting.
    inline Shared::RPC::Common::WireId gLocalWireId {};

    [[nodiscard]] inline float LerpAmount(float from, float to, std::uint32_t beginMs, std::uint32_t endMs, std::uint32_t now) {
        if (endMs == 0 || now >= endMs || endMs <= beginMs) {
            return to;
        }

        const float span     = static_cast<float>(endMs - beginMs);
        const float progress = static_cast<float>(now - beginMs);

        return from + (to - from) * (progress / span);
    }

    [[nodiscard]] inline int ClaimSlot() {
        for (int slot = kFirstRemoteSlot; slot < SDK::kMaxPlayers; ++slot) {
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
    [[nodiscard]] inline float HeadingFrom(const glm::quat &rotation) {
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

    // The signed short way round from one heading to another.
    //
    // THE SHORT WAY, because headings wrap. A ped at 179 degrees told to
    // face -179 has moved two degrees, not three hundred and fifty eight,
    // and interpolating the plain difference would spin it the whole way
    // round the wrong way once per crossing.
    //
    // This is MTA's GetOffsetRadians, which its rotation interpolation is
    // built on.
    [[nodiscard]] inline float OffsetRadians(float from, float to) {
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

    [[nodiscard]] inline float LerpRadians(float from, float to, std::uint32_t beginMs, std::uint32_t endMs, std::uint32_t now) {
        if (endMs == 0 || now >= endMs || endMs <= beginMs) {
            return to;
        }

        const float span     = static_cast<float>(endMs - beginMs);
        const float progress = static_cast<float>(now - beginMs);

        return from + OffsetRadians(from, to) * (progress / span);
    }
    // A ped has ONE heading, so its "rotation" is a yaw-only quaternion. Both
    // halves of the subsystem convert through here so they cannot disagree.
    [[nodiscard]] inline glm::quat HeadingTo(float heading) {
        return glm::quat(glm::vec3(0.0f, 0.0f, heading));
    }

    // --- inbound.cpp ---
    //
    // What this client does with other players, on the sync tick.
    void ApplyRemoteState(Shared::PlayerEntity *entity);

    // --- outbound.cpp ---
    //
    // What this client sends about itself. Driven by the per-frame pass.
    void PushLocalState(Shared::PlayerEntity *entity);
} // namespace VantaMP::Game::PlayerSyncState
