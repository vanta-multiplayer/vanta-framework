#pragma once

#include <string>

namespace VantaMP::Shared::Eos {
    // Runtime EOS settings. Defaults come from credentials.h; optional eos.json
    // beside the binary overrides them without a rebuild.
    struct RuntimeConfig {
        std::string productName    = "VantaMP";
        std::string productVersion = "0.1.0";
        std::string productId;
        std::string sandboxId;
        std::string deploymentId;
        std::string clientId;
        std::string clientSecret;
        std::string encryptionKey;
        // Only "vanta" is supported for protected EAC (silent OpenID → Connect).
        std::string connectProvider = "vanta";
        // Base URL of the standalone Vanta Auth Backend (NOT the game server).
        // Production: https://id.vantamp.xyz — override via eos.json for local.
        std::string authUrl = "https://id.vantamp.xyz";
    };

    [[nodiscard]] RuntimeConfig DefaultsFromCredentials();
    [[nodiscard]] RuntimeConfig LoadRuntimeConfig(const std::string &directory);
    [[nodiscard]] bool RuntimeConfigFilled(const RuntimeConfig &config);
} // namespace VantaMP::Shared::Eos
