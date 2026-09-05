#pragma once

#include <cstdint>

namespace VantaMP::SDK {
    // No build plugin-sdk knows matches the RGL one, so every address is scanned for at startup.
    class Patterns {
      public:
        uintptr_t CWorld__Players = 0;

        static void InitPatterns();
    };

    extern Patterns gPatterns;
} // namespace VantaMP::SDK
