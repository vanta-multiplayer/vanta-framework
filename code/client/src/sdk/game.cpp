#include "game.h"

#include "game/player_slots.h"
#include "game/remote_players.h"
#include "game_sdk.h"
#include "patterns.h"

#include <cmath>
#include <algorithm>
#include <cstdint>

static_assert(sizeof(CPlaceable) == 0x18, "CPlaceable layout changed");
static_assert(sizeof(CEntity) == 0x38, "CEntity layout changed");

namespace VantaMP::SDK {
    namespace {
        CPed *LocalPed() {
            // Slot 0, out of the RELOCATED array. Reading gPatterns.CWorld__Players
            // instead would find the array the game shipped with, which nothing
            // writes to any more once the slots have been expanded.
            const auto slot = Game::PlayerInfoAt(0);

            return slot ? *reinterpret_cast<CPed **>(slot) : nullptr;
        }

        // CPed's rotation fields, which plugin-sdk does not name.
        //
        // BOTH, NOT EITHER. The game uses m_fCurrentRotation while a ped is on
        // the ground and the matrix while it is in the air, so setting only one
        // leaves a remote player facing the right way until they step off a
        // kerb. MTA writes both for the same reason.
        constexpr std::size_t kCurrentRotationOffset = 0x558;
        constexpr std::size_t kAimingRotationOffset  = 0x55C;

        // plugin-sdk is header-only here, so it declares these but their bodies
        // live at 1.0 US addresses in .cpp files this build does not compile.
        // Calling them through resolved addresses keeps the SDK to layouts,
        // which is what it is here for.
        using OperatorNewFn    = void *(__cdecl *)(unsigned int);
        using OperatorDeleteFn = void(__cdecl *)(void *);
        using PlayerPedCtorFn  = void *(__thiscall *)(void *, int, bool);
        using SetModelIndexFn  = void(__thiscall *)(void *, unsigned int);
        using SetOrientationFn = void(__thiscall *)(void *, float, float, float);
        using FlushTasksFn     = void(__thiscall *)(void *, bool);
        using ClothesCtorFn    = void *(__thiscall *)(void *);
        using ClothesInitFn    = void(__thiscall *)(void *);
        using WorldAddFn       = void(__cdecl *)(void *);
        using WorldRemoveFn    = void(__cdecl *)(void *);
        using GetPadFn         = void *(__cdecl *)(int);

        using SetPosnFn = void(__thiscall *)(void *, float, float, float);

        void SetPedPosition(CPed *ped, float x, float y, float z) {
            reinterpret_cast<SetPosnFn>(gPatterns.CPlaceable__SetPosn)(ped, x, y, z);
        }

        void SetPedRotation(CPed *ped, float heading) {
            auto *base = reinterpret_cast<std::uint8_t *>(ped);

            *reinterpret_cast<float *>(base + kCurrentRotationOffset) = heading;
            *reinterpret_cast<float *>(base + kAimingRotationOffset)  = heading;
        }

        // plugin-sdk only declares CPlaceable::GetHeading; its definition is at a 1.0 US address.
        float HeadingOf(const CPed *ped) {
            if (!ped->m_matrix) {
                return ped->m_placement.m_fHeading;
            }
            return std::atan2(-ped->m_matrix->right.x, ped->m_matrix->right.y);
        }
    } // namespace

    LocalPedState QueryLocalPed() {
        LocalPedState state;

        // Null for as long as the game sits in the menus
        const auto ped = LocalPed();
        if (!ped) {
            return state;
        }

        const auto &position = ped->GetPosition();

        state.valid      = true;
        state.x          = position.x;
        state.y          = position.y;
        state.z          = position.z;
        state.heading    = HeadingOf(ped);
        state.modelIndex = ped->m_nModelIndex;
        state.pedType    = ped->m_nPedType;
        state.health     = ped->m_fHealth;
        state.maxHealth  = ped->m_fMaxHealth;

        if (ped->bInVehicle && ped->m_pVehicle) {
            state.inVehicle         = true;
            state.vehicleModelIndex = ped->m_pVehicle->m_nModelIndex;
        }

        return state;
    }

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
        auto *info = reinterpret_cast<CPlayerInfo *>(Game::PlayerInfoAt(slot));

        if (!info || !info->m_pPed) {
            return;
        }

        SetPedPosition(info->m_pPed, x, y, z);
        SetPedRotation(info->m_pPed, heading);
    }

    ControllerInput ReadLocalInput() {
        ControllerInput input;

        const auto pad = reinterpret_cast<CPad *>(reinterpret_cast<GetPadFn>(gPatterns.CPad__GetPad)(0));

        if (!pad) {
            return input;
        }

        const auto &state = pad->NewState;

        input.leftStickX = state.LeftStickX;
        input.leftStickY = state.LeftStickY;

        // ANALOGUE AND INDEPENDENT, NOT TWO BITS OF THE MASK. In a car these
        // are accelerate and brake, both can be held at once - that is a drift -
        // and they carry pressure, which a bit cannot. Without this every remote
        // car is flat out or coasting.
        input.accelerator = static_cast<std::uint8_t>(std::clamp<int>(state.ButtonCross, 0, 255));
        input.brake       = static_cast<std::uint8_t>(std::clamp<int>(state.ButtonSquare, 0, 255));

        const auto held = [](std::int16_t value) { return value != 0; };

        std::uint16_t buttons = 0;

        // SAMPLED FOR THE CONTEXT THE PLAYER IS IN. GTA reuses its buttons:
        // cross is sprint on foot and accelerate at the wheel, square is jump
        // and reverse. Reading them by their on-foot names while driving puts
        // "sprinting, jumping" on the wire for someone sitting still.
        //
        // The pressure values that DO mean something in a car are already sent
        // above as their own fields, so nothing is lost by leaving these clear.
        const auto *ped = LocalPed();
        const bool seated = ped && ped->bInVehicle;

        if (held(state.ButtonCircle)) {
            buttons |= Button::Fire;
        }

        if (held(state.RightShoulder1)) {
            buttons |= Button::Aim;
        }

        if (!seated) {
            if (held(state.ButtonSquare)) {
                buttons |= Button::Jump;
            }
            if (held(state.ButtonCross)) {
                buttons |= Button::Sprint;
            }
            if (held(state.ButtonTriangle)) {
                buttons |= Button::EnterExit;
            }
            if (held(state.ShockButtonL)) {
                buttons |= Button::Crouch;
            }
            if (held(state.m_bPedWalk)) {
                buttons |= Button::Walk;
            }
        }

        input.buttons = buttons;
        return input;
    }

    void WriteRemoteInput(int slot, const ControllerInput &input) {
        auto *pad = reinterpret_cast<CPad *>(Game::PadAt(slot));

        if (!pad) {
            return;
        }

        // CLEARED FIRST. Anything not set below has to read as released, or the
        // remote player inherits whatever this client happens to be holding.
        auto &state = pad->NewState;
        state       = CControllerState {};

        state.LeftStickX = input.leftStickX;
        state.LeftStickY = input.leftStickY;

        state.ButtonCross  = static_cast<std::int16_t>(input.accelerator);
        state.ButtonSquare = static_cast<std::int16_t>(input.brake);

        const auto set = [](bool pressed) -> std::int16_t { return pressed ? 255 : 0; };

        state.ButtonCircle   = set((input.buttons & Button::Fire) != 0);
        state.RightShoulder1 = set((input.buttons & Button::Aim) != 0);
        state.ButtonTriangle = set((input.buttons & Button::EnterExit) != 0);
        state.ShockButtonL   = set((input.buttons & Button::Crouch) != 0);
        state.m_bPedWalk     = set((input.buttons & Button::Walk) != 0);

        // Jump and sprint share their buttons with the pedals, which were
        // already written above from their own analogue fields. Setting them
        // again here would overwrite a car's throttle with an on-foot bit.
        if ((input.buttons & Button::Jump) != 0) {
            state.ButtonSquare = 255;
        }

        if ((input.buttons & Button::Sprint) != 0) {
            state.ButtonCross = 255;
        }

        // OldState IS MADE TO MATCH, so the game sees no press edges it was not
        // told about. Left as whatever was there before, every button this
        // client just released reads as a fresh release on the remote ped.
        pad->OldState = state;
    }

    void SetRemotePedHealth(int slot, float health, float armour) {
        auto *info = reinterpret_cast<CPlayerInfo *>(Game::PlayerInfoAt(slot));

        if (!info || !info->m_pPed) {
            return;
        }

        info->m_pPed->m_fHealth = health;
        info->m_pPed->m_fArmour = armour;
    }
} // namespace VantaMP::SDK
