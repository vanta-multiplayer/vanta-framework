#pragma once

namespace VantaMP::Core::Net {
    // Entity types and inbound RPCs. PostInit calls this once; a new
    // packet is registered here, not in application.cpp.
    void Register();
} // namespace VantaMP::Core::Net
