#include "eac.h"

#include "eos/manager.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <utils/hashing.h>

#include <string>

#if defined(VANTAMP_HAS_EOS)
#include <eos_anticheatclient.h>
#include <eos_common.h>
#endif

namespace VantaMP::Eac {
    namespace {
        bool gActive           = false;
        bool gWantSession      = false;
        bool gProtectedSession = false;
        bool gIdentityRejected = false;

#if defined(VANTAMP_HAS_EOS)
        EOS_NotificationId gMsgNotify = EOS_INVALID_NOTIFICATIONID;

        MafiaNet::RakNetGUID ServerGuid(Framework::Networking::NetworkPeer *net) {
            auto *peer = net ? net->GetPeer() : nullptr;
            if (!peer || peer->NumberOfConnections() == 0) {
                return MafiaNet::UNASSIGNED_RAKNET_GUID;
            }
            return peer->GetGUIDFromIndex(0);
        }

        std::uint32_t BlobCrc(const void *bytes, std::uint32_t size) {
            if (!bytes || size == 0) {
                return 0;
            }
            return Framework::Utils::Hashing::CalculateCRC32(static_cast<const char *>(bytes), size);
        }

        void SendOpaqueToServer(const Shared::RPC::EacOpaque &opaque) {
            auto *net = Framework::CoreModules::GetNetworkPeer();
            if (!net || opaque.length == 0) {
                return;
            }

            const auto guid = ServerGuid(net);
            if (guid == MafiaNet::UNASSIGNED_RAKNET_GUID) {
                Framework::Logging::GetLogger("EAC")->error("EAC opaque drop: no server connection ({} bytes)", opaque.length);
                return;
            }

            auto copy = opaque;
            net->SendRPC(copy, guid, MafiaNet::Priority::Immediate, MafiaNet::Reliability::ReliableOrdered);
        }

        void EOS_CALL OnMessageToServer(const EOS_AntiCheatClient_OnMessageToServerCallbackInfo *info) {
            if (!info || !info->MessageData || info->MessageDataSizeBytes == 0) {
                return;
            }

            if (info->MessageDataSizeBytes > Shared::RPC::kEacOpaqueMax) {
                Framework::Logging::GetLogger("EAC")->error(
                    "EAC c->s drop: EOS message {} bytes exceeds max {}", info->MessageDataSizeBytes, Shared::RPC::kEacOpaqueMax);
                return;
            }

            Shared::RPC::EacOpaque opaque;
            opaque.Set(info->MessageData, info->MessageDataSizeBytes);
            if (opaque.length == 0) {
                Framework::Logging::GetLogger("EAC")->error("EAC c->s drop: Set() rejected {} bytes", info->MessageDataSizeBytes);
                return;
            }
            SendOpaqueToServer(opaque);
        }

        void SendRegister(const std::string &puid, const std::string &jwt, std::uint32_t clientType) {
            auto *net = Framework::CoreModules::GetNetworkPeer();
            if (!net || puid.empty() || jwt.empty()) {
                return;
            }

            const auto guid = ServerGuid(net);
            if (guid == MafiaNet::UNASSIGNED_RAKNET_GUID) {
                return;
            }

            Shared::RPC::EacRegister reg;
            reg.productUserId  = puid;
            reg.connectJwt     = jwt;
            reg.clientPlatform = 1;
            reg.clientType     = clientType;
            net->SendRPC(reg, guid, MafiaNet::Priority::Immediate, MafiaNet::Reliability::ReliableOrdered);
        }

        void BeginSessionWithIdentity(const std::string &puid, const std::string &jwt) {
            auto *ac = Eos::GetManager().GetAntiCheatClient();
            if (!ac || puid.empty() || jwt.empty() || gActive) {
                return;
            }

            if (!Eos::GetManager().IsEacEligible()) {
                gIdentityRejected = true;
                return;
            }

            {
                EOS_AntiCheatClient_AddNotifyMessageToServerOptions options {};
                options.ApiVersion = EOS_ANTICHEATCLIENT_ADDNOTIFYMESSAGETOSERVER_API_LATEST;
                gMsgNotify         = EOS_AntiCheatClient_AddNotifyMessageToServer(ac, &options, nullptr, OnMessageToServer);
            }

            EOS_AntiCheatClient_BeginSessionOptions begin {};
            begin.ApiVersion  = EOS_ANTICHEATCLIENT_BEGINSESSION_API_LATEST;
            begin.LocalUserId = EOS_ProductUserId_FromString(puid.c_str());
            begin.Mode        = EOS_EAntiCheatClientMode::EOS_ACCM_ClientServer;

            if (!EOS_ProductUserId_IsValid(begin.LocalUserId)) {
                return;
            }

            const auto result = EOS_AntiCheatClient_BeginSession(ac, &begin);
            if (result != EOS_EResult::EOS_Success) {
                return;
            }

            SendRegister(puid, jwt, 1);
            gActive           = true;
            gProtectedSession = true;

            Framework::Logging::GetLogger("EAC")->info("EAC session begin");
        }
#endif
    } // namespace

    bool IsActive() {
        return gActive;
    }

    void Init() {
        gActive            = false;
        gWantSession       = false;
        gProtectedSession  = false;
        gIdentityRejected  = false;
    }

    void Shutdown() {
        EndSession();
    }

    void Tick() {
#if defined(VANTAMP_HAS_EOS)
        if (gWantSession && !gActive && !gIdentityRejected && Eos::GetManager().IsConnected()) {
            BeginSessionWithIdentity(Eos::GetManager().GetProductUserId(), Eos::GetManager().GetConnectJwt());
        }
#endif
    }

    void OnGameConnected() {
#if !defined(VANTAMP_HAS_EOS)
        return;
#else
        gWantSession = true;

        if (!Eos::GetManager().GetAntiCheatClient()) {
            Framework::Logging::GetLogger("EAC")->info("Anti-Cheat session skipped: client interface unavailable");
            return;
        }

        if (Eos::GetManager().IsConnected()) {
            BeginSessionWithIdentity(Eos::GetManager().GetProductUserId(), Eos::GetManager().GetConnectJwt());
        }
        else if (Eos::GetManager().GetState() == Eos::State::Failed) {
            Framework::Logging::GetLogger("EAC")->error("EAC cannot start: EOS authentication failed");
        }
        else {
            Framework::Logging::GetLogger("EAC")->info("EAC waiting for stable EOS identity before BeginSession");
        }

        Framework::Logging::GetLogger("EAC")->info("EAC game connected");
#endif
    }

    void EndSession() {
#if defined(VANTAMP_HAS_EOS)
        auto *ac = Eos::GetManager().GetAntiCheatClient();
        if (ac && gActive) {
            if (gMsgNotify != EOS_INVALID_NOTIFICATIONID) {
                EOS_AntiCheatClient_RemoveNotifyMessageToServer(ac, gMsgNotify);
                gMsgNotify = EOS_INVALID_NOTIFICATIONID;
            }

            if (gProtectedSession) {
                EOS_AntiCheatClient_EndSessionOptions end {};
                end.ApiVersion = EOS_ANTICHEATCLIENT_ENDSESSION_API_LATEST;
                EOS_AntiCheatClient_EndSession(ac, &end);
            }

            Framework::Logging::GetLogger("EAC")->info("EAC session end");
        }
#endif
        gActive           = false;
        gWantSession      = false;
        gProtectedSession = false;
        gIdentityRejected = false;
    }

    void HandleOpaqueFromServer(const Shared::RPC::EacOpaque &msg) {
#if !defined(VANTAMP_HAS_EOS)
        (void)msg;
        return;
#else
        auto *ac = Eos::GetManager().GetAntiCheatClient();
        if (!ac || !gActive || !gProtectedSession || msg.length == 0) {
            return;
        }

        EOS_AntiCheatClient_ReceiveMessageFromServerOptions options {};
        options.ApiVersion      = EOS_ANTICHEATCLIENT_RECEIVEMESSAGEFROMSERVER_API_LATEST;
        options.Data            = msg.data;
        options.DataLengthBytes = msg.length;
        const auto result       = EOS_AntiCheatClient_ReceiveMessageFromServer(ac, &options);
        if (result != EOS_EResult::EOS_Success) {
            Framework::Logging::GetLogger("EAC")->error(
                "ReceiveMessageFromServer failed: {} (len={} crc=0x{:08x})", static_cast<int>(result), msg.length,
                BlobCrc(msg.data, msg.length));
        }
#endif
    }

    void HandleActionFromServer(const Shared::RPC::EacAction &msg) {
        (void)msg;
        EndSession();
    }
} // namespace VantaMP::Eac
