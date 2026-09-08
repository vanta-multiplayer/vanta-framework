#include "runtime_config.h"

#include "credentials.h"

#include <logging/logger.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

namespace VantaMP::Shared::Eos {
    namespace {
        constexpr const char *kFileName = "eos.json";

        std::string ReadTextFile(const std::string &path) {
            std::ifstream file(path);
            if (!file) {
                return {};
            }
            std::ostringstream contents;
            contents << file.rdbuf();
            return contents.str();
        }

        std::string JsonString(const nlohmann::json &document, const char *key, const std::string &fallback) {
            if (!document.contains(key) || document[key].is_null()) {
                return fallback;
            }
            if (document[key].is_string()) {
                return document[key].get<std::string>();
            }
            return fallback;
        }
    } // namespace

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
        const auto path      = directory.empty() ? std::string(kFileName) : (directory + "\\" + kFileName);
        const auto logger    = Framework::Logging::GetLogger("EOS");
        const auto contents  = ReadTextFile(path);

        if (contents.empty()) {
            logger->info("No {} beside the binary; using built-in credentials defaults", kFileName);
            return config;
        }

        nlohmann::json document;
        try {
            document = nlohmann::json::parse(contents);
        }
        catch (const std::exception &ex) {
            logger->warn("{} could not be parsed ({}); using built-in defaults", kFileName, ex.what());
            return config;
        }

        config.productName     = JsonString(document, "product_name", config.productName);
        config.productVersion  = JsonString(document, "product_version", config.productVersion);
        config.productId       = JsonString(document, "product_id", config.productId);
        config.sandboxId       = JsonString(document, "sandbox_id", config.sandboxId);
        config.deploymentId    = JsonString(document, "deployment_id", config.deploymentId);
        config.clientId        = JsonString(document, "client_id", config.clientId);
        config.clientSecret    = JsonString(document, "client_secret", config.clientSecret);
        config.encryptionKey   = JsonString(document, "encryption_key", config.encryptionKey);
        config.connectProvider = JsonString(document, "connect_provider", config.connectProvider);
        // Prefer auth_service_url; keep auth_url as alias for existing eos.json files.
        config.authUrl = JsonString(document, "auth_service_url", config.authUrl);
        config.authUrl = JsonString(document, "auth_url", config.authUrl);
        logger->info("Loaded {} (connect_provider={}, auth_service_url={})", path, config.connectProvider, config.authUrl);
        return config;
    }
} // namespace VantaMP::Shared::Eos
