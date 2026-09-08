#include "weapon.h"

#include "access.h"

#include "sdk/world/streaming.h"

#include <cstddef>
#include <iterator>

namespace VantaMP::SDK {
    namespace {
        using Internal::LocalPed;
        using Internal::PedAt;

        // ARMING A PED IS MODEL WORK, NOT A FIELD WRITE.
        //
        // Writing the type into m_aWeapons describes an armed ped without arming
        // one: the weapon model has to be loaded, attached to the hand bone and
        // the ped's animation group chosen from it, and only the game's own
        // GiveWeapon does any of that. SetCurrentWeapon then draws it, and
        // ClearWeapon is what detaches the model again.
        using GiveWeaponFn       = int(__thiscall *)(void *, unsigned int, unsigned int, bool);
        using SetCurrentWeaponFn = void(__thiscall *)(void *, int);
        using ClearWeaponFn      = void *(__thiscall *)(void *, unsigned int);

        // Unarmed has a slot of its own, and it is the one a ped falls back to.
        constexpr int kSlotUnarmed = 0;

        // The weapon in the ped's selected slot, or nullptr if the slot index is
        // not one - which should not happen, and is cheap to be sure of before
        // indexing a thirteen-element array with a byte.
        CWeapon *HeldBy(CPed *ped) {
            if (!ped || ped->m_nSelectedWepSlot >= std::size(ped->m_aWeapons)) {
                return nullptr;
            }

            return &ped->m_aWeapons[ped->m_nSelectedWepSlot];
        }

        using GetTaskUseGunFn = CTaskSimpleUseGun *(__thiscall *)(void *);
        using UseGunCtorFn    = void *(__thiscall *)(void *, CEntity *, CVector, unsigned char, unsigned short, bool);
        using ControlGunFn    = bool(__thiscall *)(void *, CPed *, CEntity *, char);
        using SetTaskSecondaryFn = void(__thiscall *)(void *, void *, int);
        using OperatorNewFn   = void *(__cdecl *)(unsigned int);

        // UseGun command 1 is reload, 2 is fire. plugin-sdk names them on
        // m_nNextCommand. ControlGun writes the next command on a task that
        // is already running; the constructor takes the first one.
        constexpr unsigned char kUseGunReload = 1;

        bool CanReload(const CWeapon *held) {
            if (!held) {
                return false;
            }

            const auto type = static_cast<unsigned int>(held->m_eWeaponType);

            if (type < WEAPONTYPE_PISTOL || type > WEAPONTYPE_MINIGUN || type == WEAPONTYPE_SHOTGUN) {
                return false;
            }

            if (held->m_nAmmoTotal <= held->m_nAmmoInClip) {
                return false;
            }

            return true;
        }

        // Hands the ped the reload command. If they are already aiming, the
        // existing UseGun is told to reload; otherwise one is created on
        // TASK_SECONDARY_ATTACK, which is where the game keeps it.
        bool BeginReload(CPed *ped, CWeapon *held) {
            if (!ped || !held || !ped->m_pIntelligence || !CanReload(held)) {
                return false;
            }

            if (held->m_nState == WEAPONSTATE_RELOADING) {
                return false;
            }

            held->m_nState = WEAPONSTATE_RELOADING;

            auto *useGun = reinterpret_cast<GetTaskUseGunFn>(gPatterns.CPedIntelligence__GetTaskUseGun)(ped->m_pIntelligence);

            if (useGun) {
                reinterpret_cast<ControlGunFn>(gPatterns.CTaskSimpleUseGun__ControlGun)(useGun, ped, nullptr, static_cast<char>(kUseGunReload));
                return true;
            }

            auto *storage = reinterpret_cast<OperatorNewFn>(gPatterns.CTask__operator_new)(sizeof(CTaskSimpleUseGun));

            if (!storage) {
                return true;
            }

            reinterpret_cast<UseGunCtorFn>(gPatterns.CTaskSimpleUseGun__Constructor)(storage, nullptr, CVector {}, kUseGunReload, 1, false);
            reinterpret_cast<SetTaskSecondaryFn>(gPatterns.CTaskManager__SetTaskSecondary)(&ped->m_pIntelligence->m_TaskMgr, storage, TASK_SECONDARY_ATTACK);
            return true;
        }

        // Hands a ped a weapon and draws it, answering with the slot the game
        // filed it under. Slots group weapons by kind through a table in the
        // exe, so where it went is asked rather than worked out here.
        int Give(CPed *ped, unsigned int weaponType, unsigned int ammo) {
            // THE MODEL FIRST, AND THIS IS NOT OPTIONAL.
            //
            // GiveWeapon does not check whether the model it is naming is in
            // memory. It records the weapon and returns, and the game faults a
            // second or so later when it goes to draw an object that was never
            // loaded - an access violation deep in rendering with nothing left
            // to say who asked for it. MTA blocks on the same model for the same
            // reason before calling the same function.
            LoadModelNow(WeaponModelId(weaponType));

            const int slot = reinterpret_cast<GiveWeaponFn>(gPatterns.CPed__GiveWeapon)(ped, weaponType, ammo, true);

            reinterpret_cast<SetCurrentWeaponFn>(gPatterns.CPed__SetCurrentWeapon)(ped, slot);

            return slot;
        }
    } // namespace

    PedWeapon QueryLocalPedWeapon() {
        PedWeapon weapon;

        if (const auto *held = HeldBy(LocalPed())) {
            weapon.type      = static_cast<unsigned int>(held->m_eWeaponType);
            weapon.ammoClip  = held->m_nAmmoInClip;
            weapon.ammoTotal = held->m_nAmmoTotal;
            weapon.reloading = held->m_nState == WEAPONSTATE_RELOADING;
        }

        return weapon;
    }

    void GiveLocalPedWeapon(unsigned int weaponType, unsigned int ammo) {
        if (auto *ped = LocalPed()) {
            Give(ped, weaponType, ammo);
        }
    }

    void TakeLocalPedWeapon(unsigned int weaponType) {
        auto *ped = LocalPed();

        if (!ped || weaponType == 0) {
            return;
        }

        reinterpret_cast<ClearWeaponFn>(gPatterns.CPed__ClearWeapon)(ped, weaponType);
        reinterpret_cast<SetCurrentWeaponFn>(gPatterns.CPed__SetCurrentWeapon)(ped, kSlotUnarmed);
    }

    void TakeAllLocalPedWeapons() {
        auto *ped = LocalPed();

        if (!ped) {
            return;
        }

        for (auto &slot : ped->m_aWeapons) {
            if (slot.m_eWeaponType != WEAPONTYPE_UNARMED) {
                reinterpret_cast<ClearWeaponFn>(gPatterns.CPed__ClearWeapon)(ped, slot.m_eWeaponType);
            }
        }

        reinterpret_cast<SetCurrentWeaponFn>(gPatterns.CPed__SetCurrentWeapon)(ped, kSlotUnarmed);
    }

    bool ReloadLocalPedWeapon() {
        auto *ped = LocalPed();

        return BeginReload(ped, HeldBy(ped));
    }

    void SetRemotePedWeapon(int slot, unsigned int weaponType, unsigned int ammoClip, unsigned int ammoTotal) {
        auto *ped = PedAt(slot);

        auto *held = HeldBy(ped);

        if (!held) {
            return;
        }

        // ALREADY HOLDING IT? THEN THE COUNTS ARE ALL THAT CAN HAVE MOVED, and
        // those are fields with no model behind them. This is the case ten times
        // a second for a player who is not switching weapons, so it has to be
        // the cheap one - handing the ped a fresh weapon every update would
        // reload the model and restart the draw animation continuously, which is
        // the weapon equivalent of a task being reinstalled every frame.
        if (static_cast<unsigned int>(held->m_eWeaponType) == weaponType) {
            // MTA #4503: writing the clip while RELOADING cancels the anim
            // and the weapon stays empty. Total is fine - that is the reserve.
            if (held->m_nState != WEAPONSTATE_RELOADING) {
                held->m_nAmmoInClip = ammoClip;
            }

            held->m_nAmmoTotal = ammoTotal;
            return;
        }

        // Put away what is in hand first, which is what detaches the model. Skip
        // it and a player switching weapons accumulates them, holding one in
        // each hand and then in no hand at all.
        if (held->m_eWeaponType != WEAPONTYPE_UNARMED) {
            reinterpret_cast<ClearWeaponFn>(gPatterns.CPed__ClearWeapon)(ped, held->m_eWeaponType);
        }

        if (weaponType == WEAPONTYPE_UNARMED) {
            reinterpret_cast<SetCurrentWeaponFn>(gPatterns.CPed__SetCurrentWeapon)(ped, kSlotUnarmed);

            return;
        }

        const int given = Give(ped, weaponType, ammoTotal);

        if (given < 0 || static_cast<std::size_t>(given) >= std::size(ped->m_aWeapons)) {
            return;
        }

        // GiveWeapon takes a single total and splits it by the weapon's own clip
        // size, so the split it chose is not the one the owner is actually on.
        ped->m_aWeapons[given].m_nAmmoInClip = ammoClip;
        ped->m_aWeapons[given].m_nAmmoTotal  = ammoTotal;
    }

    void ReloadRemotePedWeapon(int slot) {
        auto *ped = PedAt(slot);

        BeginReload(ped, HeldBy(ped));
    }
} // namespace VantaMP::SDK
