#include "runtime_config.h"

#include "credentials.h"

#include <logging/logger.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

namespace VantaMP::Shared::Eos {
    RuntimeConfig DefaultsFromCredentials() {
        RuntimeConfig config;
        config.productName     = kProductName;
        config.productVersion  = kProductVersion;
        config.productId       = kProductId;
        config.sandboxId       = kSandboxId;
        config.deploymentId    = kDeploymentId;
        config.clientId        = kClientCredentialsId;
        config.clientSecret    = kClientCredentialsSecret;
        config.encryptionKey   = kEncryptionKey;
        config.connectProvider = "vanta";
        config.authUrl         = "https://id.vantamp.xyz";
        return config;
    }

    bool RuntimeConfigFilled(const RuntimeConfig &config) {
        auto ok = [](const std::string &value) {
            return !value.empty() && value.rfind("YOUR_", 0) != 0;
        };
        return ok(config.productId) && ok(config.sandboxId) && ok(config.deploymentId) && ok(config.clientId) && ok(config.clientSecret)
               && config.encryptionKey.size() == 64;
    }

    RuntimeConfig LoadRuntimeConfig(const std::string &directory) {
        RuntimeConfig config = DefaultsFromCredentials();
        return config;
    }
} // namespace VantaMP::Shared::Eos
