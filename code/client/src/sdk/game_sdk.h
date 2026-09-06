#pragma once

// plugin-sdk pulls in Windows headers of its own, so the framework's shim has to come first.
#include <utils/safe_win32.h>

#pragma warning(push)
#pragma warning(disable : 4005 4244 4267 4302 4311 4838)
#include <plugin_sa.h>

// plugin_sa.h does not pull these in, and the remote-ped code needs their
// layouts rather than just their names.
#include <CPlayerInfo.h>
#include <CPlayerPed.h>
#include <CPedClothesDesc.h>
#include <CTaskSimpleDuck.h>
#include <CTaskSimpleUseGun.h>
#include <CWorld.h>
#pragma warning(pop)
