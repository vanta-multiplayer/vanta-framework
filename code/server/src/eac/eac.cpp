#include "eac.h"

#include "core/server.h"
#include "shared/entity/player_entity.h"
#include "shared/eos/credentials.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/connection.h>
#include <networking/network_peer.h>
#include <utils/hashing.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

#if defined(VANTAMP_HAS_EOS)
#include <eos_anticheatserver.h>
#include <eos_connect.h>
#include <eos_init.h>
#include <eos_logging.h>
#include <eos_sdk.h>
#endif

namespace VantaMP::Eac {
    namespace {
        bool gActive = false;

#if defined(VANTAMP_HAS_EOS)
        EOS_HPlatform        gPlatform  = nullptr;
        EOS_HAntiCheatServer gAc        = nullptr;
        EOS_HConnect         gConnect   = nullptr;
        EOS_NotificationId   gMsgNotify = EOS_INVALID_NOTIFICATIONID;
        EOS_NotificationId   gActNotify = EOS_INVALID_NOTIFICATIONID;

        std::unordered_map<std::uint64_t, MafiaNet::PeerGuid> gHandleToGuid;
        std::unordered_map<std::uint64_t, std::uint64_t> gGuidToHandle;
        std::unordered_map<std::uint64_t, std::chrono::steady_clock::time_point> gRegisterDeadline;
        // BeginSession on the client emits opaque traffic before VerifyIdToken /
        // RegisterClient finish. Dropping those packets causes
        // EOS_ACCCAR_InvalidMessage ("Corrupt or invalid backend message").
        std::unordered_map<std::uint64_t, std::vector<std::vector<std::uint8_t>>> gPendingOpaque;

        constexpr auto kRegisterTimeout = std::chrono::seconds(60);

        void *HandleOf(std::uint64_t networkId) {
            return reinterpret_cast<void *>(static_cast<uintptr_t>(networkId));
        }

        std::uint64_t NetworkIdOf(void *handle) {
            return static_cast<std::uint64_t>(reinterpret_cast<uintptr_t>(handle));
        }

        std::uint64_t GuidKey(MafiaNet::PeerGuid guid) {
            return static_cast<std::uint64_t>(guid);
        }

        std::uint32_t BlobCrc(const void *bytes, std::uint32_t size) {
            if (!bytes || size == 0) {
                return 0;
            }
            return Framework::Utils::Hashing::CalculateCRC32(static_cast<const char *>(bytes), size);
        }

        void Kick(MafiaNet::PeerGuid guid, const std::string &reason) {
            auto *net = Framework::CoreModules::GetNetworkPeer();

            if (!net) {
                return;
            }

            net->KickPlayer(MafiaNet::ToGuid(guid), Framework::Networking::DisconnectionReason::KICKED_CUSTOM, reason);
        }

        void DeliverOpaqueToSdk(std::uint64_t networkId, const std::uint8_t *data, std::uint32_t length) {
            if (!gAc || !data || length == 0) {
                return;
            }

            Framework::Logging::GetLogger("EAC")->debug("EAC c→s deliver id={} len={} crc=0x{:08x}", networkId, length, BlobCrc(data, length));

            EOS_AntiCheatServer_ReceiveMessageFromClientOptions options {};
            options.ApiVersion      = EOS_ANTICHEATSERVER_RECEIVEMESSAGEFROMCLIENT_API_LATEST;
            options.ClientHandle    = HandleOf(networkId);
            options.Data            = data;
            options.DataLengthBytes = length;
            const auto result       = EOS_AntiCheatServer_ReceiveMessageFromClient(gAc, &options);
            if (result != EOS_EResult::EOS_Success) {
                Framework::Logging::GetLogger("EAC")->error(
                    "ReceiveMessageFromClient failed: {} (id={} len={} crc=0x{:08x})", static_cast<int>(result), networkId, length,
                    BlobCrc(data, length));
            }
        }

        void EOS_CALL OnEosLog(const EOS_LogMessage *message) {
            if (!message || !message->Message) {
                return;
            }
            // AntiCheat category only — keep noise down while diagnosing InvalidMessage.
            if (message->Category && std::strstr(message->Category, "AntiCheat") == nullptr) {
                return;
            }
            Framework::Logging::GetLogger("EOS")->info("[{}] {}", message->Category ? message->Category : "EOS", message->Message);
        }

        void FlushPendingOpaque(MafiaNet::PeerGuid guid, std::uint64_t networkId) {
            const auto key = GuidKey(guid);
            auto found     = gPendingOpaque.find(key);
            if (found == gPendingOpaque.end()) {
                return;
            }

            for (const auto &blob : found->second) {
                DeliverOpaqueToSdk(networkId, blob.data(), static_cast<std::uint32_t>(blob.size()));
            }
            gPendingOpaque.erase(found);
        }

        void EOS_CALL OnMessageToClient(const EOS_AntiCheatCommon_OnMessageToClientCallbackInfo *info) {
            if (!info || !info->ClientHandle || !info->MessageData || info->MessageDataSizeBytes == 0) {
                return;
            }

            const auto networkId = NetworkIdOf(info->ClientHandle);
            auto foundGuid       = gHandleToGuid.find(networkId);

            if (foundGuid == gHandleToGuid.end()) {
                return;
            }

            auto *net = Framework::CoreModules::GetNetworkPeer();

            if (!net) {
                return;
            }

            if (info->MessageDataSizeBytes > Shared::RPC::kEacOpaqueMax) {
                Framework::Logging::GetLogger("EAC")->error(
                    "EAC s→c drop: EOS message {} bytes exceeds max {}", info->MessageDataSizeBytes, Shared::RPC::kEacOpaqueMax);
                return;
            }

            Shared::RPC::EacOpaque opaque;
            opaque.Set(info->MessageData, info->MessageDataSizeBytes);
            if (opaque.length == 0) {
                Framework::Logging::GetLogger("EAC")->error("EAC s→c drop: Set() rejected {} bytes", info->MessageDataSizeBytes);
                return;
            }
            Framework::Logging::GetLogger("EAC")->debug("EAC s→c send id={} len={} crc=0x{:08x}", networkId, opaque.length,
                                                       BlobCrc(opaque.data, opaque.length));
            net->SendRPC(opaque, MafiaNet::ToGuid(foundGuid->second), MafiaNet::Priority::Immediate, MafiaNet::Reliability::ReliableOrdered);
        }

        void EOS_CALL OnClientActionRequired(const EOS_AntiCheatCommon_OnClientActionRequiredCallbackInfo *info) {
            if (!info || !info->ClientHandle) {
                return;
            }

            const auto networkId = NetworkIdOf(info->ClientHandle);
            auto foundGuid       = gHandleToGuid.find(networkId);

            if (foundGuid == gHandleToGuid.end()) {
                return;
            }

            auto *net = Framework::CoreModules::GetNetworkPeer();

            if (net) {
                Shared::RPC::EacAction action;
                action.action     = static_cast<std::int32_t>(info->ClientAction);
                action.reasonCode = static_cast<std::int32_t>(info->ActionReasonCode);
                action.details    = info->ActionReasonDetailsString ? info->ActionReasonDetailsString : "";
                net->SendRPC(action, MafiaNet::ToGuid(foundGuid->second), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);

                const std::string reason = action.details.empty() ? "eac" : ("eac:" + action.details);
                net->KickPlayer(MafiaNet::ToGuid(foundGuid->second), Framework::Networking::DisconnectionReason::KICKED_CUSTOM, reason);
            }

            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)
                ->info("EAC action required for {} code={} details={}", networkId, static_cast<int>(info->ActionReasonCode),
                       info->ActionReasonDetailsString ? info->ActionReasonDetailsString : "");
        }

        struct VerifyContext {
            MafiaNet::PeerGuid guid {};
            std::uint64_t      networkId = 0;
            std::uint32_t      platform  = 1;
            std::uint32_t      clientType = 0; // 0 unprotected, 1 protected
            std::string        puid;
        };

        void EOS_CALL OnVerifyIdToken(const EOS_Connect_VerifyIdTokenCallbackInfo *info) {
            auto *ctx = static_cast<VerifyContext *>(info ? info->ClientData : nullptr);

            if (!ctx) {
                return;
            }

            if (!info || info->ResultCode != EOS_EResult::EOS_Success || !gAc) {
                gPendingOpaque.erase(GuidKey(ctx->guid));
                Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("EAC VerifyIdToken failed for {}", ctx->networkId);
                Kick(ctx->guid, "eac:verify_failed");
                delete ctx;
                return;
            }

            EOS_AntiCheatServer_RegisterClientOptions options {};
            options.ApiVersion     = EOS_ANTICHEATSERVER_REGISTERCLIENT_API_LATEST;
            options.ClientHandle   = HandleOf(ctx->networkId);
            options.ClientPlatform = static_cast<EOS_EAntiCheatCommonClientPlatform>(ctx->platform);
            options.ClientType     = ctx->clientType != 0 ? EOS_EAntiCheatCommonClientType::EOS_ACCCT_ProtectedClient
                                                         : EOS_EAntiCheatCommonClientType::EOS_ACCCT_UnprotectedClient;
            options.UserId         = EOS_ProductUserId_FromString(ctx->puid.c_str());

            const auto result = EOS_AntiCheatServer_RegisterClient(gAc, &options);

            if (result == EOS_EResult::EOS_Success) {
                gHandleToGuid[ctx->networkId]     = ctx->guid;
                gGuidToHandle[GuidKey(ctx->guid)] = ctx->networkId;
                gRegisterDeadline.erase(GuidKey(ctx->guid));
                FlushPendingOpaque(ctx->guid, ctx->networkId);
                Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)
                    ->info("EAC registered client {} ({})", ctx->networkId, ctx->clientType != 0 ? "protected" : "unprotected");
            }
            else {
                gPendingOpaque.erase(GuidKey(ctx->guid));
                Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("EAC RegisterClient failed: {}", static_cast<int>(result));
                Kick(ctx->guid, "eac:register_failed");
            }

            delete ctx;
        }
#endif
    } // namespace

    bool IsActive() {
        return gActive;
    }

    void Init() {
        if (!Shared::Eos::CredentialsFilled()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("EAC disabled — fill Shared::Eos credentials placeholders");
            return;
        }

#if !defined(VANTAMP_HAS_EOS)
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("EAC credentials set but EOSSDK was not linked (missing Win64 lib)");
        return;
#else
        EOS_InitializeOptions initOptions {};
        initOptions.ApiVersion     = EOS_INITIALIZE_API_LATEST;
        initOptions.ProductName    = Shared::Eos::kProductName;
        initOptions.ProductVersion = Shared::Eos::kProductVersion;

        const auto initResult = EOS_Initialize(&initOptions);

        if (initResult != EOS_EResult::EOS_Success && initResult != EOS_EResult::EOS_AlreadyConfigured) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("EOS_Initialize failed: {}", static_cast<int>(initResult));
            return;
        }

        EOS_Logging_SetCallback(OnEosLog);
        EOS_Logging_SetLogLevel(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES, EOS_ELogLevel::EOS_LOG_Off);
        EOS_Logging_SetLogLevel(EOS_ELogCategory::EOS_LC_AntiCheat, EOS_ELogLevel::EOS_LOG_VeryVerbose);

        EOS_Platform_ClientCredentials credentials {};
        credentials.ClientId     = Shared::Eos::kClientCredentialsId;
        credentials.ClientSecret = Shared::Eos::kClientCredentialsSecret;

        EOS_Platform_Options platformOptions {};
        platformOptions.ApiVersion        = EOS_PLATFORM_OPTIONS_API_LATEST;
        platformOptions.bIsServer         = EOS_TRUE;
        platformOptions.Flags             = EOS_PF_DISABLE_OVERLAY;
        platformOptions.ProductId         = Shared::Eos::kProductId;
        platformOptions.SandboxId         = Shared::Eos::kSandboxId;
        platformOptions.DeploymentId      = Shared::Eos::kDeploymentId;
        platformOptions.ClientCredentials = credentials;
        platformOptions.EncryptionKey     = Shared::Eos::kEncryptionKey;

        gPlatform = EOS_Platform_Create(&platformOptions);

        if (!gPlatform) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("EOS_Platform_Create failed");
            return;
        }

        gConnect = EOS_Platform_GetConnectInterface(gPlatform);
        gAc      = EOS_Platform_GetAntiCheatServerInterface(gPlatform);

        if (!gAc || !gConnect) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("EAC server interface missing");
            return;
        }

        {
            EOS_AntiCheatServer_AddNotifyMessageToClientOptions options {};
            options.ApiVersion = EOS_ANTICHEATSERVER_ADDNOTIFYMESSAGETOCLIENT_API_LATEST;
            gMsgNotify         = EOS_AntiCheatServer_AddNotifyMessageToClient(gAc, &options, nullptr, OnMessageToClient);
        }

        {
            EOS_AntiCheatServer_AddNotifyClientActionRequiredOptions options {};
            options.ApiVersion = EOS_ANTICHEATSERVER_ADDNOTIFYCLIENTACTIONREQUIRED_API_LATEST;
            gActNotify         = EOS_AntiCheatServer_AddNotifyClientActionRequired(gAc, &options, nullptr, OnClientActionRequired);
        }

        EOS_AntiCheatServer_BeginSessionOptions begin {};
        begin.ApiVersion             = EOS_ANTICHEATSERVER_BEGINSESSION_API_LATEST;
        begin.RegisterTimeoutSeconds = 60;
        begin.ServerName             = "VantaMP";
        begin.bEnableGameplayData    = EOS_FALSE;
        begin.LocalUserId            = nullptr;

        const auto beginResult = EOS_AntiCheatServer_BeginSession(gAc, &begin);

        if (beginResult != EOS_EResult::EOS_Success) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("EAC BeginSession failed: {}", static_cast<int>(beginResult));
            return;
        }

        gActive = true;
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("EAC server session started");
#endif
    }

    void Shutdown() {
#if defined(VANTAMP_HAS_EOS)
        if (gAc) {
            if (gMsgNotify != EOS_INVALID_NOTIFICATIONID) {
                EOS_AntiCheatServer_RemoveNotifyMessageToClient(gAc, gMsgNotify);
                gMsgNotify = EOS_INVALID_NOTIFICATIONID;
            }

            if (gActNotify != EOS_INVALID_NOTIFICATIONID) {
                EOS_AntiCheatServer_RemoveNotifyClientActionRequired(gAc, gActNotify);
                gActNotify = EOS_INVALID_NOTIFICATIONID;
            }

            if (gActive) {
                EOS_AntiCheatServer_EndSessionOptions end {};
                end.ApiVersion = EOS_ANTICHEATSERVER_ENDSESSION_API_LATEST;
                EOS_AntiCheatServer_EndSession(gAc, &end);
            }
        }

        if (gPlatform) {
            EOS_Platform_Release(gPlatform);
            gPlatform = nullptr;
        }

        gAc      = nullptr;
        gConnect = nullptr;
        gHandleToGuid.clear();
        gGuidToHandle.clear();
        gRegisterDeadline.clear();
        gPendingOpaque.clear();
        EOS_Shutdown();
#endif
        gActive = false;
    }

    void Tick() {
#if defined(VANTAMP_HAS_EOS)
        if (gPlatform) {
            EOS_Platform_Tick(gPlatform);
        }

        if (gActive && !gRegisterDeadline.empty()) {
            const auto now = std::chrono::steady_clock::now();

            for (auto it = gRegisterDeadline.begin(); it != gRegisterDeadline.end();) {
                if (now < it->second) {
                    ++it;
                    continue;
                }

                // Soft for now: launcher inject / non-Steam clients often cannot
                // complete EAC yet. Log and drop the deadline instead of kicking.
                Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)
                    ->info("EAC register still pending for {} (not kicking; bootstrapper/Steam path incomplete)", it->first);
                gPendingOpaque.erase(it->first);
                it = gRegisterDeadline.erase(it);
            }
        }
#endif
    }

    void OnPlayerConnected(MafiaNet::PeerGuid guid) {
#if defined(VANTAMP_HAS_EOS)
        if (!gActive) {
            return;
        }

        gRegisterDeadline[GuidKey(guid)] = std::chrono::steady_clock::now() + kRegisterTimeout;
#else
        (void)guid;
#endif
    }

    void OnPlayerDisconnect(MafiaNet::PeerGuid guid) {
#if defined(VANTAMP_HAS_EOS)
        const auto key = GuidKey(guid);
        gRegisterDeadline.erase(key);
        gPendingOpaque.erase(key);

        if (!gAc || !gActive) {
            return;
        }

        const auto found = gGuidToHandle.find(key);

        if (found == gGuidToHandle.end()) {
            return;
        }

        EOS_AntiCheatServer_UnregisterClientOptions options {};
        options.ApiVersion   = EOS_ANTICHEATSERVER_UNREGISTERCLIENT_API_LATEST;
        options.ClientHandle = HandleOf(found->second);
        EOS_AntiCheatServer_UnregisterClient(gAc, &options);

        gHandleToGuid.erase(found->second);
        gGuidToHandle.erase(found);
#else
        (void)guid;
#endif
    }

    void HandleRegister(Server &server, MafiaNet::PeerGuid guid, const Shared::RPC::EacRegister &msg) {
        auto *player = server.Players().Find(guid);

        if (!player) {
            return;
        }

#if !defined(VANTAMP_HAS_EOS)
        (void)msg;
        return;
#else
        if (!gActive || !gConnect || !gAc) {
            return;
        }

        if (msg.productUserId.empty() || msg.connectJwt.empty()) {
            Kick(guid, "eac:missing_token");
            return;
        }

        auto *ctx       = new VerifyContext();
        ctx->guid       = guid;
        ctx->networkId  = static_cast<std::uint64_t>(player->GetNetworkID());
        ctx->platform   = msg.clientPlatform;
        ctx->clientType = msg.clientType;
        ctx->puid       = msg.productUserId;

        EOS_Connect_IdToken token {};
        token.ApiVersion    = EOS_CONNECT_IDTOKEN_API_LATEST;
        token.ProductUserId = EOS_ProductUserId_FromString(msg.productUserId.c_str());
        token.JsonWebToken  = msg.connectJwt.c_str();

        EOS_Connect_VerifyIdTokenOptions options {};
        options.ApiVersion = EOS_CONNECT_VERIFYIDTOKEN_API_LATEST;
        options.IdToken    = &token;

        EOS_Connect_VerifyIdToken(gConnect, &options, ctx, OnVerifyIdToken);
#endif
    }

    void HandleOpaqueFromClient(MafiaNet::PeerGuid guid, const Shared::RPC::EacOpaque &msg) {
#if !defined(VANTAMP_HAS_EOS)
        (void)guid;
        (void)msg;
        return;
#else
        if (!gActive || !gAc || msg.length == 0) {
            return;
        }

        const auto key   = GuidKey(guid);
        const auto found = gGuidToHandle.find(key);

        if (found == gGuidToHandle.end()) {
            auto &queue = gPendingOpaque[key];
            queue.emplace_back(msg.data, msg.data + msg.length);
            Framework::Logging::GetLogger("EAC")->debug("EAC c→s queue (pre-register) guid={} len={} crc=0x{:08x} queued={}", key, msg.length,
                                                       BlobCrc(msg.data, msg.length), queue.size());
            return;
        }

        DeliverOpaqueToSdk(found->second, msg.data, msg.length);
#endif
    }
} // namespace VantaMP::Eac
