#pragma once

#include <cstring>

namespace VantaMP::Shared::Eos {
    // Shared product ids (same on client and server).
    // https://dev.epicgames.com/portal → Product Settings / Deployments.
    //
    // Client credentials differ by policy:
    //   VANTA_CLIENT  → GameClient
    //   VANTA_SERVER  → TrustedServer
    // Leave YOUR_* placeholders to keep that side's EAC init skipped.

    inline constexpr const char *kProductName    = "VantaMP";
    inline constexpr const char *kProductVersion = "0.1.0";

    inline constexpr const char *kProductId    = "0a1b29e156b34cddad9fc70d94063fbf";
    inline constexpr const char *kSandboxId    = "a2a373a7ee614dd8bdd746b9551d1515";
    inline constexpr const char *kDeploymentId = "1a85a068ff364d47bb16abf807a7fd68";

#if defined(VANTA_CLIENT)
    // GameClient policy — untrusted player process.
    inline constexpr const char *kClientCredentialsId     = "xyza7891VLjKJlcvhcaSe17unb8LmwEd";
    inline constexpr const char *kClientCredentialsSecret = "QnSAtZUPGlGlDrRKJ0uJMB+hgJRamLlAKWba9UuDc/g";
#elif defined(VANTA_SERVER)
    // TrustedServer policy — dedicated server.
    inline constexpr const char *kClientCredentialsId     = "xyza7891rU9XkGPFxzZpqrQZw2HGROiv";
    inline constexpr const char *kClientCredentialsSecret = "GRVu263MO3HL5lF5+764eucYmluGlnPo9PW2HQ+Ty0M";
#else
#error "tehehehe you are not supposed to be here"
#endif

    // 64 hex chars (256-bit). Required by EOS_Platform_Create even if unused.
    inline constexpr const char *kEncryptionKey =
        "44334a117956a2965cca442a6e9e88de45f42475849d9f585f551c084a08f35f";

    [[nodiscard]] inline bool CredentialsFilled() {
        auto ok = [](const char *value) {
            return value && value[0] != '\0' && std::strncmp(value, "YOUR_", 5) != 0;
        };

        return ok(kProductId) && ok(kSandboxId) && ok(kDeploymentId) && ok(kClientCredentialsId) && ok(kClientCredentialsSecret);
    }
} // namespace VantaMP::Shared::Eos
