#include "shot.h"

#include "access.h"
#include "aim.h"

#include "sdk/patterns.h"
#include "sdk/player/slots.h"

#include <CCamera.h>
#include <CPad.h>
#include <CVector2D.h>
#include <CWeaponInfo.h>

#include <MinHook.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace VantaMP::SDK {
    namespace {
        using Internal::HeadingOf;
        using Internal::LocalPed;
        using Internal::PedAt;

        using FireFn = bool(__thiscall *)(CWeapon *, CEntity *, CVector *, CVector *, CEntity *, CVector *, CVector *);
        using FireInstantHitFn = bool(__thiscall *)(CWeapon *, CEntity *, CVector *, CVector *, CEntity *, CVector *, CVector *, bool, bool);
        using LosFn = bool(__cdecl *)(CVector *, CVector *, void *, CEntity **, bool, bool, bool, bool, bool, bool, bool, bool);
        using IsPlayerFn = bool(__thiscall *)(CPed *);

        FireFn             gOriginalFire         = nullptr;
        FireInstantHitFn   gOriginalFireInstantHit = nullptr;
        void (*gReportLocalShot)(unsigned int, float, float, float, float, float, float, int) = nullptr;

        bool         gReplayingRemoteShot = false;
        bool         gLocalFiring         = false;
        bool         gLocalHasRay         = false;
        unsigned int gLocalWeaponType     = 0;
        int          gLocalHitSlot        = -1;

        CVector gReplayOrigin {};
        CVector gReplayTarget {};
        CVector gLocalOrigin {};
        CVector gLocalTarget {};

        CWeapon *HeldByPed(CPed *ped) {
            if (!ped || ped->m_nSelectedWepSlot >= std::size(ped->m_aWeapons)) {
                return nullptr;
            }

            return &ped->m_aWeapons[ped->m_nSelectedWepSlot];
        }

        CEntity **IgnoreEntitySlot() {
            return reinterpret_cast<CEntity **>(gPatterns.CWorld__pIgnoreEntity);
        }

        bool IsLocalShooter(CEntity *entity) {
            return entity && (entity == LocalPed() || SDK::SlotOfPed(entity) == SDK::kLocalPlayerSlot);
        }

        CWeaponInfo *InfoOf(unsigned int weaponType) {
            using GetInfoFn = CWeaponInfo *(__cdecl *)(unsigned int, unsigned char);

            return reinterpret_cast<GetInfoFn>(gPatterns.CWeaponInfo__GetWeaponInfo)(weaponType, 1);
        }

        float WeaponRangeOf(unsigned int weaponType) {
            auto *info = InfoOf(weaponType);

            return info ? info->m_fWeaponRange : 70.0f;
        }

        // Lock-on hands FireInstantHit the ped's root, which is the feet.
        // At close range that ray is almost straight down. MTA's Mid hook
        // drops an aimed start with no XY length for the same reason.
        bool HasHorizontalAim(const CVector &from, const CVector &to) {
            const float dx = to.x - from.x;
            const float dy = to.y - from.y;

            return dx * dx + dy * dy > 1.0f;
        }

        const CCam *ActiveCam() {
            auto *camera = reinterpret_cast<CCamera *>(gPatterns.TheCamera);

            if (!camera || camera->m_nActiveCam > 2) {
                return nullptr;
            }

            return &camera->m_aCams[camera->m_nActiveCam];
        }

        bool IsFirstPersonCam(eCamMode mode) {
            switch (mode) {
                case MODE_SNIPER:
                case MODE_ROCKETLAUNCHER:
                case MODE_1STPERSON:
                case MODE_M16_1STPERSON:
                case MODE_SNIPER_RUNABOUT:
                case MODE_ROCKETLAUNCHER_RUNABOUT:
                case MODE_1STPERSON_RUNABOUT:
                case MODE_M16_1STPERSON_RUNABOUT:
                case MODE_HELICANNON_1STPERSON:
                case MODE_CAMERA:
                case MODE_ROCKETLAUNCHER_HS:
                case MODE_ROCKETLAUNCHER_RUNABOUT_HS:
                    return true;
                default:
                    return false;
            }
        }

        bool LocalIsAiming() {
            using GetPadFn = void *(__cdecl *)(int);

            if (auto *pad = reinterpret_cast<CPad *>(reinterpret_cast<GetPadFn>(gPatterns.CPad__GetPad)(0))) {
                // MTA GetShotData: RightShoulder1 == 255 is the crosshair.
                if (pad->NewState.RightShoulder1 != 0) {
                    return true;
                }
            }

            const auto *cam = ActiveCam();

            if (!cam) {
                return false;
            }

            switch (cam->m_nMode) {
                case MODE_AIMWEAPON:
                case MODE_AIMWEAPON_FROMCAR:
                case MODE_AIMWEAPON_ATTACHED:
                    return true;
                default:
                    return IsFirstPersonCam(cam->m_nMode);
            }
        }

        // MTA CClientPed::GetShotData first-person branch: camera source,
        // stepped forward two metres so the start is not inside the skull.
        bool AimRayFromFirstPerson(CVector &start, CVector &end, unsigned int weaponType) {
            const auto *cam = ActiveCam();

            if (!cam || !IsFirstPersonCam(cam->m_nMode)) {
                return false;
            }

            const CVector &dir = cam->m_vecFront;

            if (dir.x * dir.x + dir.y * dir.y + dir.z * dir.z < 0.01f) {
                return false;
            }

            const float range = WeaponRangeOf(weaponType);

            start = {cam->m_vecSource.x + dir.x * 2.0f, cam->m_vecSource.y + dir.y * 2.0f, cam->m_vecSource.z + dir.z * 2.0f};
            end   = {start.x + dir.x * range, start.y + dir.y * range, start.z + dir.z * range};
            return true;
        }

        // The function FireInstantHit calls for a player with the crosshair
        // up. Source is the muzzle; it writes the camera-adjusted start and
        // the point under the reticle at weapon range.
        bool AimRayFromCrosshair(CVector &start, CVector &end, unsigned int weaponType, const CVector &muzzle) {
            auto *camera = reinterpret_cast<CCamera *>(gPatterns.TheCamera);

            if (!camera) {
                return false;
            }

            using Find3rdFn = void(__thiscall *)(CCamera *, float, CVector, CVector *, CVector *);

            reinterpret_cast<Find3rdFn>(gPatterns.CCamera__Find3rdPersonCamTargetVector)(camera, WeaponRangeOf(weaponType), muzzle, &start, &end);
            return HasHorizontalAim(start, end);
        }

        // MTA hip-fire (no crosshair): heading on the XY plane, Z stays at
        // the muzzle. Camera.m_vecFront from a follow cam looks down at the
        // ped, which is the tracer into the dirt.
        CVector LevelShotAlongHeading(const CVector &origin, unsigned int weaponType) {
            const auto *ped     = LocalPed();
            const float heading = ped ? HeadingOf(ped) : ReadLocalPedAim().horizontal;
            const float range   = WeaponRangeOf(weaponType);

            return {origin.x - std::sin(heading) * range, origin.y + std::cos(heading) * range, origin.z};
        }

        CVector MuzzleOf(CPed *ped, CWeapon *held, const CVector &fallback) {
            auto *info = held ? InfoOf(static_cast<unsigned int>(held->m_eWeaponType)) : nullptr;

            CVector muzzle = info ? info->m_vecFireOffset : fallback;

            using TransformFn = void(__thiscall *)(CPed *, CVector *, unsigned int, bool);

            // MTA FireBullet / GetShotData: fire offset transformed on the
            // right wrist. That is the barrel, not the camera and not the feet.
            reinterpret_cast<TransformFn>(gPatterns.CPed__GetTransformedBonePosition)(ped, &muzzle, 24, true);
            return muzzle;
        }

        void ShellSizeOf(unsigned int weaponType, float &backOffset, float &size) {
            backOffset = 0.0f;
            size       = 0.0f;

            switch (weaponType) {
                case WEAPONTYPE_PISTOL:
                case WEAPONTYPE_PISTOL_SILENCED:
                case WEAPONTYPE_DESERT_EAGLE:
                case WEAPONTYPE_COUNTRYRIFLE:
                case WEAPONTYPE_SNIPERRIFLE:
                    backOffset = 0.2f;
                    size       = 0.25f;
                    break;
                case WEAPONTYPE_SHOTGUN:
                case WEAPONTYPE_SAWNOFF:
                case WEAPONTYPE_SPAS12:
                    backOffset = 0.3f;
                    size       = 0.45f;
                    break;
                case WEAPONTYPE_MICRO_UZI:
                case WEAPONTYPE_MP5:
                case WEAPONTYPE_TEC9:
                    backOffset = 0.2f;
                    size       = 0.3f;
                    break;
                case WEAPONTYPE_AK47:
                case WEAPONTYPE_M4:
                case WEAPONTYPE_MINIGUN:
                    backOffset = 0.65f;
                    size       = 0.25f;
                    break;
                default:
                    break;
            }
        }

        // MTA CClientPed native-task presentation, which is what a remote
        // viewer actually sees. CWeapon::Fire is where the local player gets
        // DoGunFlash (250, false); we swallow that for remotes, and the
        // IsPlayer patch keeps FireInstantHit off the player-flash branch.
        void PlayRemoteMuzzleFx(CPed *ped, CWeapon *held, unsigned int weaponType, const CVector &origin, const CVector &target) {
            CVector muzzle = MuzzleOf(ped, held, origin);

            CVector dir = {target.x - muzzle.x, target.y - muzzle.y, target.z - muzzle.z};
            const float length = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);

            if (length < 0.001f) {
                if (ped->m_matrix) {
                    dir = ped->m_matrix->at;
                } else {
                    dir = {0.0f, 1.0f, 0.0f};
                }
            } else {
                dir.x /= length;
                dir.y /= length;
                dir.z /= length;
            }

            float backOffset = 0.0f;
            float shellSize  = 0.0f;

            ShellSizeOf(weaponType, backOffset, shellSize);

            if (shellSize <= 0.0f) {
                backOffset = 0.2f;
                shellSize  = 0.25f;
            }

            CVector shell = {muzzle.x - dir.x * backOffset, muzzle.y - dir.y * backOffset, muzzle.z - dir.z * backOffset};

            using AddLightFn = void(__cdecl *)(unsigned char, CVector, CVector, float, float, float, float, unsigned char, bool, CEntity *);

            reinterpret_cast<AddLightFn>(gPatterns.CPointLights__AddLight)(0, muzzle, CVector {}, 3.0f, 220.0f / 255.0f, 1.0f, 0.0f, 0, false, nullptr);

            using TriggerFn = void(__thiscall *)(void *, CEntity *, CVector *, CVector *, bool);

            reinterpret_cast<TriggerFn>(gPatterns.Fx_c__TriggerGunshot)(reinterpret_cast<void *>(gPatterns.g_fx), ped, &muzzle, &shell, true);

            using FlashFn = bool(__thiscall *)(CPed *, int, bool);

            reinterpret_cast<FlashFn>(gPatterns.CPed__DoGunFlash)(ped, 250, false);

            CVector2D right {1.0f, 0.0f};

            if (ped->m_matrix) {
                right.x = ped->m_matrix->right.x;
                right.y = ped->m_matrix->right.y;
            }

            using ShellFn = void(__thiscall *)(CWeapon *, CEntity *, CVector *, CVector2D *, float);

            reinterpret_cast<ShellFn>(gPatterns.CWeapon__AddGunshell)(held, ped, &shell, &right, shellSize);

            using AudioFn = void(__thiscall *)(void *, std::uint16_t);

            const std::uint16_t event = weaponType == WEAPONTYPE_MINIGUN ? 0x96 : 0x91;

            reinterpret_cast<AudioFn>(gPatterns.CAEPedWeaponAudioEntity__AddAudioEvent)(&ped->m_weaponAudio, event);
        }

        int SlotHitByRay(CVector start, CVector end, CEntity *ignore) {
            if (!gPatterns.CWorld__ProcessLineOfSight) {
                return -1;
            }

            alignas(4) unsigned char col[0x40] {};
            CEntity *hit = nullptr;

            CEntity **ignoreSlot = IgnoreEntitySlot();
            CEntity  *previous   = nullptr;

            if (ignoreSlot) {
                previous    = *ignoreSlot;
                *ignoreSlot = ignore;
            }

            const bool struck = reinterpret_cast<LosFn>(gPatterns.CWorld__ProcessLineOfSight)(&start, &end, col, &hit, true, true, true, true, false, true, false, true);

            if (ignoreSlot) {
                *ignoreSlot = previous;
            }

            return (struck && hit) ? SDK::SlotOfPed(hit) : -1;
        }

        void ReportLocal(unsigned int weaponType, const CVector &origin, const CVector &target, int hitSlot) {
            if (!gReportLocalShot) {
                return;
            }

            gReportLocalShot(weaponType, origin.x, origin.y, origin.z, target.x, target.y, target.z, hitSlot);
        }

        // hooking.h cannot live in this file: plugin-sdk and jitasm both claim
        // the same tokens. The two FireInstantHit sites are ordinary relative
        // calls, so the five-byte rewrite is all they need.
        void PatchCall(uintptr_t site, const void *fn) {
            DWORD old = 0;
            VirtualProtect(reinterpret_cast<void *>(site), 5, PAGE_EXECUTE_READWRITE, &old);
            *reinterpret_cast<std::uint8_t *>(site) = 0xE8;
            *reinterpret_cast<std::int32_t *>(site + 1) =
                static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(fn) - (site + 5));
            VirtualProtect(reinterpret_cast<void *>(site), 5, old, &old);
        }

        // MTA HandleRemoteInstantHit: the ProcessLineOfSight arguments still
        // hold the camera-derived ray. Overwrite them with the owner's packet
        // before the world is asked.
        //
        // Also where a local shot's real start/end and hit live, when this
        // site actually runs. Aimed fire often never reaches it, so reporting
        // stays in FireInstantHit_Hook.
        bool __cdecl FireInstantHit_Los(CVector *start, CVector *end, void *col, CEntity **hit, bool buildings, bool vehicles, bool peds, bool objects, bool dummies, bool seeThrough, bool cameraIgnore, bool shootThrough) {
            if (gReplayingRemoteShot && start && end) {
                *start = gReplayOrigin;
                *end   = gReplayTarget;
            }

            const bool struck = reinterpret_cast<LosFn>(gPatterns.CWorld__ProcessLineOfSight)(start, end, col, hit, buildings, vehicles, peds, objects, dummies, seeThrough, cameraIgnore, shootThrough);

            if (gLocalFiring && start && end) {
                gLocalOrigin  = *start;
                gLocalTarget  = *end;
                gLocalHasRay  = true;
                gLocalHitSlot = (struck && hit && *hit) ? SDK::SlotOfPed(*hit) : -1;
            }

            return struck;
        }

        // MTA HOOK_CWeapon_FireInstantHit_IsPlayer. A remote CPlayerPed is a
        // player as far as the game is concerned, so this call would take the
        // first-person / camera branch and ignore the target we passed.
        bool __fastcall FireInstantHit_IsPlayer(CPed *ped) {
            if (SDK::SlotOfPed(ped) > SDK::kLocalPlayerSlot) {
                return false;
            }

            return reinterpret_cast<IsPlayerFn>(gPatterns.CPed__IsPlayer)(ped);
        }

        // MTA's FireInstantHit_Mid: the shot is real once this returns true.
        // The gun task often calls this without going through CWeapon::Fire,
        // which is why a Fire-only reporter missed every aimed burst.
        bool __fastcall FireInstantHit_Hook(CWeapon *weapon, void *unused, CEntity *firingEntity, CVector *origin, CVector *muzzle, CEntity *targetEntity, CVector *target, CVector *driveBy, bool extra, bool doMuzzle) {
            (void)unused;

            const bool local = !gReplayingRemoteShot && IsLocalShooter(firingEntity);

            if (local) {
                gLocalFiring      = true;
                gLocalHasRay      = false;
                gLocalHitSlot     = -1;
                gLocalWeaponType  = static_cast<unsigned int>(weapon->m_eWeaponType);
            }

            const bool fired = gOriginalFireInstantHit(weapon, firingEntity, origin, muzzle, targetEntity, target, driveBy, extra, doMuzzle);

            if (local) {
                gLocalFiring = false;
            }

            if (!local || !fired) {
                return fired;
            }

            CVector start {};
            CVector end {};

            // MTA OnMy_CWeapon_FireInstantHit_Mid, then GetShotData:
            //
            // 1. The LOS start/end FireInstantHit already computed - unless
            //    that line has no XY length (lock-on at the ped's feet).
            // 2. First-person: camera source + front, not the muzzle.
            // 3. Crosshair up: Find3rdPersonCamTargetVector, the same call
            //    the game uses. Camera.m_vecFront from the barrel is the
            //    follow cam looking down at the ped.
            // 4. Hip-fire: heading on the XY plane, Z stays at the muzzle.
            const CVector barrel = muzzle ? *muzzle : (origin ? *origin : firingEntity->GetPosition());
            bool          have   = false;

            if (gLocalHasRay && HasHorizontalAim(gLocalOrigin, gLocalTarget)) {
                start = gLocalOrigin;
                end   = gLocalTarget;
                have  = true;
            }

            if (!have) {
                have = AimRayFromFirstPerson(start, end, gLocalWeaponType);
            }

            if (!have && LocalIsAiming()) {
                have = AimRayFromCrosshair(start, end, gLocalWeaponType, barrel);
            }

            if (!have) {
                start = barrel;
                end   = LevelShotAlongHeading(start, gLocalWeaponType);
            }

            int hit = gLocalHitSlot;

            if (hit < 0 && targetEntity) {
                hit = SDK::SlotOfPed(targetEntity);
            }

            if (hit < 0) {
                hit = SlotHitByRay(start, end, firingEntity);
            }

            ReportLocal(gLocalWeaponType, start, end, hit);
            return fired;
        }

        // __fastcall with a spare edx is how a __thiscall is written: this in
        // ecx, the six stack arguments exactly as the game pushed them.
        bool __fastcall Fire_Hook(CWeapon *weapon, void *unused, CEntity *firingEntity, CVector *origin, CVector *muzzle, CEntity *targetEntity, CVector *target, CVector *driveBy) {
            (void)unused;

            if (gReplayingRemoteShot) {
                return gOriginalFire(weapon, firingEntity, origin, muzzle, targetEntity, target, driveBy);
            }

            if (IsLocalShooter(firingEntity)) {
                return gOriginalFire(weapon, firingEntity, origin, muzzle, targetEntity, target, driveBy);
            }

            // A remote ped's Fire is the replayed fire button aimed with THIS
            // client's camera. Swallow it; the owner's PlayerShot is the shot.
            if (SDK::SlotOfPed(firingEntity) > SDK::kLocalPlayerSlot) {
                return true;
            }

            return gOriginalFire(weapon, firingEntity, origin, muzzle, targetEntity, target, driveBy);
        }
    } // namespace

    void InstallShotSync() {
        if (gOriginalFire && gOriginalFireInstantHit) {
            return;
        }

        if (!gOriginalFire) {
            MH_CreateHook(reinterpret_cast<void *>(gPatterns.CWeapon__Fire), reinterpret_cast<void *>(&Fire_Hook), reinterpret_cast<void **>(&gOriginalFire));
            MH_EnableHook(reinterpret_cast<void *>(gPatterns.CWeapon__Fire));
        }

        if (!gOriginalFireInstantHit) {
            MH_CreateHook(reinterpret_cast<void *>(gPatterns.CWeapon__FireInstantHit), reinterpret_cast<void *>(&FireInstantHit_Hook),
                          reinterpret_cast<void **>(&gOriginalFireInstantHit));
            MH_EnableHook(reinterpret_cast<void *>(gPatterns.CWeapon__FireInstantHit));
        }

        PatchCall(gPatterns.CWeapon__FireInstantHit_LosCall, reinterpret_cast<const void *>(&FireInstantHit_Los));
        PatchCall(gPatterns.CWeapon__FireInstantHit_IsPlayerCall, reinterpret_cast<const void *>(&FireInstantHit_IsPlayer));
    }

    void SetLocalShotReporter(void (*report)(unsigned int, float, float, float, float, float, float, int)) {
        gReportLocalShot = report;
    }

    unsigned int WeaponDamageOf(unsigned int weaponType) {
        auto *info = InfoOf(weaponType);

        return info ? info->m_nDamage : 0;
    }

    void FireRemotePedShot(int slot, unsigned int weaponType, float ox, float oy, float oz, float tx, float ty, float tz) {
        auto *ped  = PedAt(slot);
        auto *held = HeldByPed(ped);

        if (!ped || !held || !gOriginalFireInstantHit) {
            return;
        }

        // One spare round so FireInstantHit does not no-op when a 10 Hz
        // snapshot has already written zero. Do not touch RELOADING: the
        // last shot of a magazine used to force READY here, which is why
        // remotes never played the empty-clip reload.
        if (held->m_nState != WEAPONSTATE_RELOADING && held->m_nAmmoInClip < 1) {
            held->m_nAmmoInClip = 1;
        }

        if (held->m_nState == WEAPONSTATE_OUT_OF_AMMO) {
            held->m_nState = WEAPONSTATE_READY;
        }

        gReplayOrigin = {ox, oy, oz};
        gReplayTarget = {tx, ty, tz};

        CVector origin = gReplayOrigin;
        CVector target = gReplayTarget;

        // MTA FireBullet: the shooter is not a valid hit, the ray is the
        // owner's, and FireInstantHit (not Fire) is what traces it.
        if (auto *ignore = IgnoreEntitySlot()) {
            *ignore = ped;
        }

        gReplayingRemoteShot = true;
        gOriginalFireInstantHit(held, ped, &origin, &origin, nullptr, &target, nullptr, false, true);
        gReplayingRemoteShot = false;

        if (auto *ignore = IgnoreEntitySlot()) {
            *ignore = nullptr;
        }

        PlayRemoteMuzzleFx(ped, held, weaponType, origin, target);
    }
} // namespace VantaMP::SDK
