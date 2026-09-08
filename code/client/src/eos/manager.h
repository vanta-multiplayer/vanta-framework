#pragma once

#include <cstdint>
#include <string>

#if defined(VANTAMP_HAS_EOS)
#include <eos_anticheatclient_types.h>
#include <eos_connect_types.h>
#include <eos_sdk.h>
#endif

namespace VantaMP::Eos {
    // Where the local EOS_ProductUserId came from.
    // Protected EAC requires VantaOpenId (never DeviceId / Epic Account).
    enum class AuthSource : std::uint8_t {
        None = 0,
        VantaOpenId,
    };

    enum class State : std::uint8_t {
        Uninitialized = 0,
        Initializing,
        PlatformReady,
        Authenticating,
        Connected,
        Failed,
        ShuttingDown,
    };

    class Manager {
      public:
        [[nodiscard]] bool Initialize(const std::string &projectPath, const std::string &displayName);
        void Tick();
        void Shutdown();

        [[nodiscard]] bool IsInitialized() const;
        [[nodiscard]] bool IsConnected() const;
        [[nodiscard]] bool IsEacEligible() const;
        [[nodiscard]] State GetState() const;
        [[nodiscard]] AuthSource GetAuthSource() const;
        [[nodiscard]] const char *GetAuthSourceName() const;
        [[nodiscard]] const std::string &GetProductUserId() const;
        [[nodiscard]] const std::string &GetConnectJwt() const;

#if defined(VANTAMP_HAS_EOS)
        [[nodiscard]] EOS_HPlatform GetPlatform() const;
        [[nodiscard]] EOS_HConnect GetConnect() const;
        [[nodiscard]] EOS_HAntiCheatClient GetAntiCheatClient() const;
#endif
    };

    Manager &GetManager();
} // namespace VantaMP::Eos
