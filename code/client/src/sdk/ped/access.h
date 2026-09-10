#pragma once

// Reaching into a CPed, for the SDK's own use.
//
// INTERNAL TO src/sdk. This header names plugin-sdk types, so it can only be
// included from a translation unit that CMakeLists has granted the plugin-sdk
// include directories - see VANTAMP_CLIENT_SDK_FILES there. Nothing above the
// SDK boundary may include it.
//
// What lives here is the handful of primitives that both the local player and
// remote peds need. Anything used by only one of them belongs in that file.

#include "game/entities/player_slots.h"
#include "sdk/game_sdk.h"
#include "sdk/patterns.h"

#include <cstddef>
#include <cstdint>

// Layouts this whole boundary is built on. Checked here so every SDK
// translation unit re-checks them, rather than in whichever file happened to
// hold them when the SDK was one file.
static_assert(sizeof(CPlaceable) == 0x18, "CPlaceable layout changed");
static_assert(sizeof(CEntity) == 0x38, "CEntity layout changed");

namespace VantaMP::SDK::Internal {
    // plugin-sdk is header-only here, so it declares the game's functions but
    // their bodies live at 1.0 US addresses in .cpp files this build does not
    // compile. Calling them through resolved addresses keeps the SDK to
    // layouts, which is what it is here for.
    using OperatorNewFn    = void *(__cdecl *)(unsigned int);
    using OperatorDeleteFn = void(__cdecl *)(void *);
    using SetPosnFn        = void(__thiscall *)(void *, float, float, float);
    using GetPadFn         = void *(__cdecl *)(int);

    // CPed's rotation fields, which plugin-sdk does not name.
    //
    // BOTH, NOT EITHER. The game uses m_fCurrentRotation while a ped is on the
    // ground and the matrix while it is in the air, so setting only one leaves a
    // remote player facing the right way until they step off a kerb. MTA writes
    // both for the same reason.
    inline constexpr std::size_t kCurrentRotationOffset = 0x558;
    inline constexpr std::size_t kAimingRotationOffset  = 0x55C;

    // CTask's virtuals, reached through the vtable rather than by name.
    //
    // plugin-sdk DECLARES them but their bodies are at 1.0 US addresses in .cpp
    // files this build does not compile, so `task->GetId()` would want a
    // definition to link against. The vtable is where the answer lives in any
    // case, and the slot is the one plugin-sdk itself uses.
    // CTask::GetSubTask - after the destructor (0) and Clone (1). A complex
    // task runs its work through the simple task underneath it, so following
    // this is how you find what a ped is ACTUALLY doing.
    inline constexpr std::size_t kTaskGetSubTaskSlot = 2;

    inline constexpr std::size_t kTaskGetIdSlot = 4;

    // Where the in-water task sits. plugin-sdk names the primary task array but
    // not the priorities that index it; this is MTA's
    // TASK_PRIORITY_EVENT_RESPONSE_NONTEMP, which is where it looks for the same
    // task and where we put ours.
    inline constexpr int kTaskPriorityEventResponseNonTemp = 2;

    inline void *TaskVirtual(void *task, std::size_t slot) {
        return (*reinterpret_cast<void ***>(task))[slot];
    }

    // Which task this is. A slot holds whatever is running, so finding something
    // in the duck slot does not mean it is a duck.
    inline eTaskType TaskIdOf(void *task) {
        using GetIdFn = eTaskType(__thiscall *)(void *);

        return reinterpret_cast<GetIdFn>(TaskVirtual(task, kTaskGetIdSlot))(task);
    }

    // Whether this ped is in the water, ASKED THE WAY THE GAME ANSWERS IT.
    //
    // Not from bSubmergedInWater, which is false for a player swimming along the
    // surface, and not from bTouchingWater, which is true for one standing in
    // the shallows. Being in water is the in-water TASK running, the flags being
    // things that task maintains. MTA's CClientPed::IsInWater asks exactly this.
    inline bool IsPedInWater(const CPed *ped) {
        if (!ped || !ped->m_pIntelligence) {
            return false;
        }

        auto *running = ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[kTaskPriorityEventResponseNonTemp];

        return running && TaskIdOf(running) == TASK_COMPLEX_IN_WATER;
    }

    inline void SetClumpAlpha(CEntity *entity, std::uint8_t alpha) {
        if (!entity || !entity->m_pRwObject) {
            return;
        }

        using SetAlphaFn = void(__cdecl *)(void *, int);

        reinterpret_cast<SetAlphaFn>(gPatterns.CVisibilityPlugins__SetClumpAlpha)(entity->m_pRwObject, static_cast<int>(alpha));
    }

    inline void SetPedWalkingStyle(CPed *ped, std::uint16_t style) {
        if (!ped || ped->m_nAnimGroup == style) {
            return;
        }

        ped->m_nAnimGroup = style;
        reinterpret_cast<void(__thiscall *)(CPed *)>(gPatterns.CPed__SetMoveAnim)(ped);
    }

    inline bool PedOnFire(const CPed *ped) {
        return ped && ped->m_pFire != nullptr;
    }

    inline void SetPedOnFire(CPed *ped, bool on) {
        if (!ped) {
            return;
        }

        if (!on) {
            if (ped->m_pFire) {
                reinterpret_cast<void(__thiscall *)(CFire *)>(gPatterns.CFire__Extinguish)(ped->m_pFire);
            }

            return;
        }

        if (ped->m_pFire) {
            return;
        }

        using StartFireFn = CFire *(__thiscall *)(void *, CEntity *, CEntity *, float, unsigned char, unsigned int, signed char);

        reinterpret_cast<StartFireFn>(gPatterns.CFireManager__StartFire)(reinterpret_cast<void *>(gPatterns.gFireManager), ped, ped, 0.8f, 1, 0xFFFFFFFFu, 0);
    }

    // CTask::MakeAbortable — after GetId (slot 4) and StopTimer (slot 5).
    inline constexpr std::size_t kTaskMakeAbortableSlot = 6;

    // Primary priority. MTA installs the jetpack here.
    inline constexpr int kTaskPriorityPrimary = 3;
    inline constexpr int kTaskPriorityEventResponseTemp = 1;

    inline bool PedHasJetPack(CPed *ped) {
        if (!ped || !ped->m_pIntelligence || !gPatterns.CPedIntelligence__GetTaskJetPack) {
            return false;
        }

        using GetJetFn = void *(__thiscall *)(void *);

        auto *task = reinterpret_cast<GetJetFn>(gPatterns.CPedIntelligence__GetTaskJetPack)(ped->m_pIntelligence);

        if (!task || TaskIdOf(task) != TASK_SIMPLE_JETPACK) {
            return false;
        }

        // m_bIsFinished at +0x8. MTA treats a finished pack as not worn.
        return !*reinterpret_cast<bool *>(reinterpret_cast<std::uint8_t *>(task) + 0x8);
    }

    inline void AbortPrimaryTask(CPed *ped, int priority, int /*abortPriority*/) {
        if (!ped || !ped->m_pIntelligence) {
            return;
        }

        auto *tasks = &ped->m_pIntelligence->m_TaskMgr;

        if (!tasks->m_aPrimaryTasks[priority]) {
            return;
        }

        using SetTaskFn = void(__thiscall *)(void *, void *, int, bool);

        // SetTask(nullptr) aborts and deletes the occupant of the slot.
        reinterpret_cast<SetTaskFn>(gPatterns.CTaskManager__SetTask)(tasks, nullptr, priority, false);
    }

    inline void SetPedJetPack(CPed *ped, bool hasJetPack) {
        if (!ped || !ped->m_pIntelligence || !gPatterns.CTaskSimpleJetPack__Constructor) {
            return;
        }

        if (!hasJetPack) {
            if (PedHasJetPack(ped)) {
                using GetJetFn = void *(__thiscall *)(void *);
                using MakeAbortableFn = bool(__thiscall *)(void *, CPed *, int, void *);

                auto *task = reinterpret_cast<GetJetFn>(gPatterns.CPedIntelligence__GetTaskJetPack)(ped->m_pIntelligence);

                if (task) {
                    reinterpret_cast<MakeAbortableFn>(TaskVirtual(task, kTaskMakeAbortableSlot))(task, ped, ABORT_PRIORITY_URGENT, nullptr);
                }
            }

            return;
        }

        if (PedHasJetPack(ped) || ped->bInVehicle) {
            return;
        }

        AbortPrimaryTask(ped, kTaskPriorityPrimary, ABORT_PRIORITY_IMMEDIATE);
        AbortPrimaryTask(ped, kTaskPriorityEventResponseTemp, ABORT_PRIORITY_IMMEDIATE);
        AbortPrimaryTask(ped, kTaskPriorityEventResponseNonTemp, ABORT_PRIORITY_IMMEDIATE);

        constexpr unsigned int kJetPackBytes = 0x70;

        auto *storage = reinterpret_cast<OperatorNewFn>(gPatterns.CTask__operator_new)(kJetPackBytes);

        if (!storage) {
            return;
        }

        // MTA: push pTargetEnt=0, nHoverTime, fCruiseHeight, pVecTargetPos.
        using JetCtorFn = void(__thiscall *)(void *, const CVector *, float, int, CEntity *);

        reinterpret_cast<JetCtorFn>(gPatterns.CTaskSimpleJetPack__Constructor)(storage, nullptr, 10.0f, 0, nullptr);

        using SetTaskFn = void(__thiscall *)(void *, void *, int, bool);

        reinterpret_cast<SetTaskFn>(gPatterns.CTaskManager__SetTask)(&ped->m_pIntelligence->m_TaskMgr, storage, kTaskPriorityPrimary, true);
    }

    inline CPed *LocalPed() {
        // Slot 0, out of the RELOCATED array. Reading gPatterns.CWorld__Players
        // instead would find the array the game shipped with, which nothing
        // writes to any more once the slots have been expanded.
        const auto slot = Game::PlayerInfoAt(0);

        return slot ? *reinterpret_cast<CPed **>(slot) : nullptr;
    }

    // The ped in a slot, or nullptr if that slot is empty.
    inline CPed *PedAt(int slot) {
        auto *info = reinterpret_cast<CPlayerInfo *>(Game::PlayerInfoAt(slot));

        return info ? info->m_pPed : nullptr;
    }

    // Hands a realtime shadow back to the manager while its owner is still
    // allocated. CPhysical's destructor does this; Vanta pool-deletes peds
    // and vehicles without running it, so the manager keeps m_pOwner. The
    // next ReturnRealTimeShadow (0x705B30) then writes m_pShadowData through
    // that freed pointer and AVs at 0x705B3B - the disconnect crash a frame
    // or two after a remote player leaves.
    inline void ReleaseRealTimeShadow(CPhysical *physical) {
        if (!physical || !physical->m_pShadowData) {
            return;
        }

        physical->m_pShadowData->m_pOwner = nullptr;
        physical->m_pShadowData           = nullptr;
    }

    inline void SetPedPosition(CPed *ped, float x, float y, float z) {
        reinterpret_cast<SetPosnFn>(gPatterns.CPlaceable__SetPosn)(ped, x, y, z);
    }

    inline void SetPedRotation(CPed *ped, float heading, bool includeAiming = true) {
        auto *base = reinterpret_cast<std::uint8_t *>(ped);

        *reinterpret_cast<float *>(base + kCurrentRotationOffset) = heading;

        // BOTH, unless a gun is up. m_fAimingRotation is the upper body's
        // target, and PointGunInDirection writes it while aiming. Overwriting
        // it with the walk heading every frame is the arms stuttering against
        // the IK - the feet step smoothly and the hands jump.
        if (includeAiming) {
            *reinterpret_cast<float *>(base + kAimingRotationOffset) = heading;
        }
    }

    // READ FROM THE FIELD THAT IS WRITTEN, NOT FROM THE MATRIX.
    //
    // THIS WAS A NINETY DEGREE BUG. Deriving the heading from the matrix as
    // atan2(-right.x, right.y) - which is what CPlaceable::GetHeading does -
    // does NOT return m_fCurrentRotation. The two use different zero
    // directions, and the difference is exactly a quarter turn.
    //
    // That made the read and the write disagree: SetPedRotation writes
    // m_fCurrentRotation, so a heading that went out through here came back
    // ninety degrees rotated. And because the SENDER reports its own facing
    // through this same function, every player announced a heading a quarter
    // turn from where they stood, and every receiver wrote that value straight
    // into the ped's real rotation - so every remote player was drawn ninety
    // degrees off, permanently, with no drift to give it away.
    //
    // Measured: a ped told 78.6 degrees read back as -11.4.
    //
    // MTA has no such asymmetry - it sends GetCurrentRotation and applies
    // SetCurrentRotation, the same field both ways. This does likewise.
    inline float HeadingOf(const CPed *ped) {
        const auto *base = reinterpret_cast<const std::uint8_t *>(ped);

        return *reinterpret_cast<const float *>(base + kCurrentRotationOffset);
    }

    // Health is not the death state. GTA keeps PEDSTATE_DIE / DEAD, the DIE
    // task and the lie-down anim after m_fHealth is written back, which is a
    // ped with a full bar still on the ground.
    inline bool PedLooksDead(const CPed *ped) {
        if (!ped) {
            return false;
        }

        return ped->m_fHealth <= 0.0f || ped->m_ePedState == PEDSTATE_DIE || ped->m_ePedState == PEDSTATE_DEAD ||
               ped->m_ePedState == PEDSTATE_DIE_BY_STEALTH || ped->bIsPedDieAnimPlaying;
    }

    // MTA CClientPed::Respawn: SetInitialState first so a DIE task is not
    // still running when health comes back. Position, heading and health are
    // the caller's - this function resets those.
    inline void StandPedUp(CPed *ped) {
        if (!ped) {
            return;
        }

        using SetInitialStateFn = void(__thiscall *)(void *, bool);
        using FlushTasksFn      = void(__thiscall *)(void *, bool);

        reinterpret_cast<SetInitialStateFn>(gPatterns.CPlayerPed__SetInitialState)(ped, false);

        if (ped->m_pIntelligence) {
            reinterpret_cast<FlushTasksFn>(gPatterns.CPedIntelligence__FlushImmediately)(ped->m_pIntelligence, true);
        }

        ped->m_ePedState          = PEDSTATE_IDLE;
        ped->bIsPedDieAnimPlaying = false;
        ped->bBloodPuddleCreated  = false;
        ped->bIsDyingStuck        = false;
        ped->m_nDeathTime         = 0;
        ped->m_vecMoveSpeed       = CVector {};
    }
} // namespace VantaMP::SDK::Internal
