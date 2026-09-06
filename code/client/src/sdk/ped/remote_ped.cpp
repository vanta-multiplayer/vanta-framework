#include "remote_ped.h"

#include "access.h"
#include "game/entities/remote_players.h"

#include <cstddef>

namespace VantaMP::SDK {
    namespace {
        using Internal::HeadingOf;
        using Internal::OperatorDeleteFn;
        using Internal::OperatorNewFn;
        using Internal::PedAt;
        using Internal::SetPedPosition;
        using Internal::SetPedRotation;

        using PlayerPedCtorFn  = void *(__thiscall *)(void *, int, bool);
        using SetModelIndexFn  = void(__thiscall *)(void *, unsigned int);
        using SetOrientationFn = void(__thiscall *)(void *, float, float, float);
        using FlushTasksFn     = void(__thiscall *)(void *, bool);
        using ClothesCtorFn    = void *(__thiscall *)(void *);
        using ClothesInitFn    = void(__thiscall *)(void *);
        using WorldAddFn       = void(__cdecl *)(void *);
        using WorldRemoveFn    = void(__cdecl *)(void *);

        using DuckCtorFn         = void *(__thiscall *)(void *, eDuckControlTypes, unsigned short, short);
        using GetTaskSecondaryFn = void *(__thiscall *)(void *, int);
        using SetTaskSecondaryFn = void(__thiscall *)(void *, void *, int);

        // CTask's virtuals, reached through the vtable rather than by name.
        //
        // plugin-sdk DECLARES them but their bodies are at 1.0 US addresses in
        // .cpp files this build does not compile, so `task->GetId()` would want
        // a definition to link against. The vtable is where the answer lives in
        // any case, and the slot is the one plugin-sdk itself uses.
        constexpr std::size_t kTaskGetIdSlot = 4;

        void *TaskVirtual(void *task, std::size_t slot) {
            return (*reinterpret_cast<void ***>(task))[slot];
        }

        // Which task this is. The secondary slots hold whatever is running, so
        // finding something in the duck slot does not mean it is a duck.
        eTaskType TaskIdOf(void *task) {
            using GetIdFn = eTaskType(__thiscall *)(void *);

            return reinterpret_cast<GetIdFn>(TaskVirtual(task, kTaskGetIdSlot))(task);
        }
    } // namespace

    void *CreateRemotePed(int slot, int modelIndex, float x, float y, float z) {
        auto *info = reinterpret_cast<CPlayerInfo *>(Game::PlayerInfoAt(slot));

        if (!info) {
            return nullptr;
        }

        // The order below mirrors what the game does for the local player.
        // Every step exists because leaving it out produced something specific.
        const auto &pat = gPatterns;

        // `new CPlayerPed(...)` cannot be written directly: it would emit calls
        // to plugin-sdk's operator new and constructor, neither of which is
        // linked. Allocating through the game's own allocator also keeps the ped
        // in the pool the game expects to find it in.
        auto *storage = reinterpret_cast<OperatorNewFn>(pat.CPed__operator_new)(sizeof(CPlayerPed));

        if (!storage) {
            return nullptr;
        }

        reinterpret_cast<PlayerPedCtorFn>(pat.CPlayerPed__Constructor)(storage, slot, false);

        auto *ped = static_cast<CPlayerPed *>(storage);

        if (!ped) {
            return nullptr;
        }

        info->m_pPed             = ped;
        info->m_nPlayerState     = PLAYERSTATE_PLAYING;
        info->m_bDoesNotGetTired = true;

        ped->m_nPedType = PED_TYPE_PLAYER1;

        // TAKE THE PED OUT OF THE STREAMER'S HANDS, at creation rather than at
        // teardown. Without these flags GTA's streaming owns the ped and frees
        // whatever it still owns during shutdown - by which point RenderWare has
        // closed, and freeing a ped releases RenderWare resources. Vanta creates
        // remote players and Vanta destroys them; the streamer must not join in.
        ped->bStreamingDontDelete = true;
        ped->bDontStream          = true;

        // GIVE THE PED A USABLE CPlayerData. The constructor points it at the
        // relocated player-info array, which is correct - but that memory is a
        // fresh allocation the game never initialised, so its embedded pointers
        // are null or stale and the game will dereference and free them.
        //
        // The local player's data is a known-good starting point. The clothes
        // descriptor is then replaced with one of this ped's own: sharing a
        // descriptor lets a change to one player alter another.
        if (auto *playerData = ped->m_pPlayerData) {
            const auto *localInfo = reinterpret_cast<const CPlayerInfo *>(Game::PlayerInfoAt(0));

            if (localInfo && localInfo->m_pPed && localInfo->m_pPed->m_pPlayerData) {
                *playerData = *localInfo->m_pPed->m_pPlayerData;
            }

            auto *clothes = reinterpret_cast<OperatorNewFn>(pat.CPed__operator_new)(sizeof(CPedClothesDesc));

            if (clothes) {
                reinterpret_cast<ClothesCtorFn>(pat.CPedClothesDesc__Constructor)(clothes);
                reinterpret_cast<ClothesInitFn>(pat.CPedClothesDesc__Initialise)(clothes);
                playerData->m_pPedClothesDesc = static_cast<CPedClothesDesc *>(clothes);
            }
        }

        // Not a target - neither for other remote players nor for the local
        // player's auto-aim.
        ped->bNeverEverTargetThisPed = true;

        // HITTABLE WHILE SEATED. GTA clears these on a ped it did not set up as
        // a player, so a remote player sitting in a car cannot be shot - bullets
        // pass through.
        ped->bCanBeShotInVehicle   = true;
        ped->bTestForShotInVehicle = true;

        // A FRESH PED IS NOT MID-LANDING. Left set, the ped is processed as
        // though recovering from a fall it never took, which suppresses its
        // normal movement until the game decides the landing finished.
        ped->bIsLanding = false;

        // REGISTERED BEFORE CWorld::Add. GetPlayerInfoForThisPlayerPed is hooked
        // and resolves a ped to its slot through the registry; CWorld::Add runs
        // game code that may ask for this ped's info, and without the mapping
        // the hook falls back to slot 0 - handing out the LOCAL player's info
        // and corrupting it.
        Game::RegisterRemotePlayer(slot, ped);

        reinterpret_cast<WorldAddFn>(pat.CWorld__Add)(ped);

        reinterpret_cast<SetModelIndexFn>(pat.CPed__SetModelIndex)(ped, static_cast<unsigned int>(modelIndex));
        reinterpret_cast<SetOrientationFn>(pat.CPlaceable__SetOrientation)(ped, 0.0f, 0.0f, 0.0f);
        SetPedPosition(ped, x, y, z);
        SetPedRotation(ped, 0.0f);

        // GIVE THE PED SOMETHING TO DO. CPlayerPed's constructor leaves the
        // primary task slot empty, and a ped with no primary task has no idle
        // behaviour: it holds whatever pose it was created in and never settles.
        // That is the remote player who streams in and stands frozen.
        reinterpret_cast<FlushTasksFn>(pat.CPedIntelligence__FlushImmediately)(ped->m_pIntelligence, true);

        return ped;
    }

    void DestroyRemotePed(int slot) {
        auto *info = reinterpret_cast<CPlayerInfo *>(Game::PlayerInfoAt(slot));

        if (!info || !info->m_pPed) {
            return;
        }

        auto *ped = info->m_pPed;

        // The clothes descriptor was allocated per ped above, so it is released
        // here - the game does not know Vanta allocated it.
        if (auto *playerData = ped->m_pPlayerData; playerData && playerData->m_pPedClothesDesc) {
            reinterpret_cast<OperatorDeleteFn>(gPatterns.CPed__operator_delete)(playerData->m_pPedClothesDesc);
            playerData->m_pPedClothesDesc = nullptr;
        }

        Game::UnregisterRemotePlayer(slot);

        reinterpret_cast<WorldRemoveFn>(gPatterns.CWorld__Remove)(ped);
        reinterpret_cast<OperatorDeleteFn>(gPatterns.CPed__operator_delete)(ped);

        info->m_pPed = nullptr;
    }

    void SetRemotePedTransform(int slot, float x, float y, float z, float heading) {
        auto *ped = PedAt(slot);

        if (!ped) {
            return;
        }

        SetPedPosition(ped, x, y, z);
        SetPedRotation(ped, heading);
    }

    Position GetRemotePedPosition(int slot) {
        Position position;

        auto *ped = PedAt(slot);

        if (!ped) {
            return position;
        }

        const auto &current = ped->GetPosition();

        position.x = current.x;
        position.y = current.y;
        position.z = current.z;

        return position;
    }

    void SetRemotePedPosition(int slot, float x, float y, float z) {
        if (auto *ped = PedAt(slot)) {
            SetPedPosition(ped, x, y, z);
        }
    }

    glm::quat GetRemotePedRotation(int slot) {
        auto *ped = PedAt(slot);

        if (!ped) {
            return glm::identity<glm::quat>();
        }

        return glm::quat(glm::vec3(0.0f, 0.0f, HeadingOf(ped)));
    }

    void SetRemotePedHeading(int slot, float heading) {
        if (auto *ped = PedAt(slot)) {
            SetPedRotation(ped, heading);
        }
    }

    void SetRemotePedHealth(int slot, float health, float armour) {
        if (auto *ped = PedAt(slot)) {
            ped->m_fHealth = health;
            ped->m_fArmour = armour;
        }
    }

    void SetRemotePedVelocity(int slot, float x, float y, float z) {
        if (auto *ped = PedAt(slot)) {
            ped->m_vecMoveSpeed = CVector(x, y, z);
        }
    }

    void SetRemotePedDucked(int slot, bool ducked) {
        auto *ped = PedAt(slot);

        if (!ped || !ped->m_pIntelligence) {
            return;
        }

        auto *tasks = &ped->m_pIntelligence->m_TaskMgr;

        auto *running = reinterpret_cast<GetTaskSecondaryFn>(gPatterns.CTaskManager__GetTaskSecondary)(tasks, TASK_SECONDARY_DUCK);

        const bool crouching = running && TaskIdOf(running) == TASK_SIMPLE_DUCK;

        if (ducked == crouching) {
            return;
        }

        if (!ducked) {
            // STOOD UP BY CLEARING THE FLAG, NOT BY ABORTING THE TASK.
            //
            // The two are not symmetric, which is worth stating plainly because
            // it looks inconsistent with the constructor call below. Writing
            // bIsDucking TRUE does nothing: while no duck task is running the
            // game recomputes the flag every frame and it reverts. Writing it
            // FALSE is how the duck ends - CTaskSimpleDuck reads it and finishes
            // itself, playing its stand-up on the way out. MTA ends a duck the
            // same way for the same reason.
            //
            // Aborting instead - which this did, at ABORT_PRIORITY_IMMEDIATE -
            // makes the ped jump. The abort does not take the task away on the
            // spot, so the next frame still finds a duck and aborts it again,
            // sixty times a second, and the stand-up never gets to play. Worse,
            // ending a crouch immediately restores the ped's full collision
            // height in a single step, and a ped that grows instantly inside
            // whatever it was crouched behind gets pushed clear of it.
            //
            // Repeating the flag write while the task finishes is harmless; the
            // same could not be said of repeating the abort.
            ped->bIsDucking = false;
            return;
        }

        auto *storage = reinterpret_cast<OperatorNewFn>(gPatterns.CTask__operator_new)(sizeof(CTaskSimpleDuck));

        if (!storage) {
            return;
        }

        // DUCK_TASK_CONTROLLED, and a length of zero.
        //
        // The standalone types end themselves after nLengthOfDuck and take the
        // crouch with them, which for a remote player means bobbing up out of
        // cover between updates. This one lasts until something ends it, and
        // what ends it is the owner standing up - see the flag write above.
        //
        // -1 for shots-whizzing: that is the game deciding on its own to duck
        // when bullets pass, which is a decision the owner's game already made.
        reinterpret_cast<DuckCtorFn>(gPatterns.CTaskSimpleDuck__Constructor)(storage, DUCK_TASK_CONTROLLED, 0, -1);
        reinterpret_cast<SetTaskSecondaryFn>(gPatterns.CTaskManager__SetTaskSecondary)(tasks, storage, TASK_SECONDARY_DUCK);
    }
} // namespace VantaMP::SDK
