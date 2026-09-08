#pragma once

namespace VantaMP::SDK {
    // Mid-function headlight tint. Lives outside the SDK TU so it can use
    // framework inject_hook (plugin-sdk and jitasm cannot share a file).
    void InstallVehicleHeadlightHooks();
} // namespace VantaMP::SDK
