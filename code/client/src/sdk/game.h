#pragma once

// The SDK's public surface, in one include.
//
// plugin-sdk lives behind this boundary and nowhere else. Above it, the game is
// player slots and plain numbers; below it are CPed, CPad and raw offsets. The
// boundary is enforced by CMakeLists, which hands the plugin-sdk include
// directories to the listed sdk sources only - so a header that names a game
// type simply will not compile anywhere else.
//
// Prefer including the specific header when a file only needs one of these.

#include "camera.h"
#include "input.h"
#include "streaming.h"
#include "ped/aim.h"
#include "ped/local_ped.h"
#include "ped/remote_ped.h"
#include "ped/weapon.h"
