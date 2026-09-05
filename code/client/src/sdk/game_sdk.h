#pragma once

// plugin-sdk pulls in Windows headers of its own, so the framework's shim has to come first.
#include <utils/safe_win32.h>

#pragma warning(push)
#pragma warning(disable : 4005 4244 4267 4302 4311 4838)
#include <plugin_sa.h>
#pragma warning(pop)
