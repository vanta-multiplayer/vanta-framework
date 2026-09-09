#include "connection.h"

#include "application.h"
#include "client_config.h"
#include "diagnostics/breadcrumbs.h"
#include "nui/nui.h"
#include "voice_io.h"

#include "game/entities/local_player.h"
#include "game/entities/remote_players.h"
#include "game/sync/player_sync.h"
#include "game/sync/vehicle_sync.h"
#include "sdk/vehicle/vehicle.h"

#include <logging/logger.h>

#include <exception>
#include <string>

namespace VantaMP::Core {
    namespace {
        const char *PhaseKey(Framework::Integrations::Client::ConnectionPhase phase) {
            using Phase = Framework::Integrations::Client::ConnectionPhase;

            switch (phase) {
            case Phase::Disconnected: return "disconnected";
            case Phase::Connecting: return "connecting";
            case Phase::Authenticating: return "authenticating";
            case Phase::Downloading: return "downloading";
            case Phase::Starting: return "starting";
            case Phase::InGame: return "inGame";
            }

            return "unknown";
        }

        void EmitConnectionProgress(Framework::Integrations::Client::ConnectionPhase phase,
                                    const Framework::Integrations::Client::AssetDownloadStatus &status) {
            if (phase == Framework::Integrations::Client::ConnectionPhase::Disconnected ||
                phase == Framework::Integrations::Client::ConnectionPhase::InGame) {
                return;
            }

            Nui::ConnectionProgress progress;
            progress.phase           = PhaseKey(phase);
            progress.downloading     = status.downloading;
            progress.progress        = status.progress;
            progress.filesDownloaded = status.filesDownloaded;
            progress.filesTotal      = status.filesTotal;
            progress.bytesDownloaded = status.bytesDownloaded;
            progress.bytesTotal      = status.bytesTotal;
            progress.currentFile     = status.currentFile;
            Nui::ReportConnectionProgress(progress);
        }
    } // namespace

    void OnConnectionFinalized(Application &app, float serverTickRate) {
        (void)app;

        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Connected; the server ticks every {:.0f}ms", serverTickRate * 1000.0f);

        Diagnostics::Breadcrumbs::SetStage(Diagnostics::Stage::InGame);

        StopMicTest();
        Nui::ReportMicTest(false);
        ApplyVoiceSettings();

        Nui::ReportConnected(GetClientConfig().serverHost);
        Nui::ReportConnectionState("connected", "");
    }

    void OnConnectionClosed(Application &app) {
        StopMicTest();
        Nui::ReportMicTest(false);
        SetTalkLatched(false);

        const auto reason       = app.GetLastDisconnectionReason();
        const bool reachedWorld = Nui::IsInWorld();

        if (reason.empty()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Disconnected from the server");
        }
        else {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Disconnected from the server: {}", reason);
        }

        // MTA ~CClientGame: explosions and projectiles first, then every
        // script entity, then the local session. Dead or seated does not
        // change the order. The GTA ped stays allocated or the next
        // frame's RegisterReference is a null-pointer crash.
        Game::SuspendRemotePlayers();
        SDK::ClearTransientWorldEffects();
        Game::ClearRemotePlayers();
        Game::ClearRemoteVehicles();
        Game::DestroyLocalPlayer();
        Game::ResumeRemotePlayers();

        // A join that never landed used to report "disconnected" with no
        // detail, so the connect screen vanished and the reason with it.
        if (!reachedWorld && !reason.empty()) {
            Nui::ReportConnectionState("failed", reason);
            return;
        }

        if (reason.empty()) {
            Nui::ReportConnectionState("disconnected", reachedWorld ? "Disconnected" : "Cancelled");
            return;
        }

        Nui::ReportConnectionState("disconnected", reason);
    }

    void OnAssetsDownloadProgress(Application &app, const Framework::Integrations::Client::AssetDownloadStatus &status) {
        EmitConnectionProgress(app.GetConnectionPhase(), status);
    }

    void OnConnectionPhaseChanged(Application &app, Framework::Integrations::Client::ConnectionPhase phase) {
        EmitConnectionProgress(phase, app.GetAssetDownloadStatus());
    }

    void RequestDisconnect() {
        if (!gApplication) {
            return;
        }

        gApplication->RequestDisconnect();
    }

    void RequestConnect(const std::string &address, const std::string &nickname) {
        if (!gApplication) {
            return;
        }

        // "host:port", with the port optional - a player typing an address into
        // a box should not have to remember the default.
        auto          host = address;
        std::uint32_t port = 27015;

        if (const auto colon = address.rfind(':'); colon != std::string::npos) {
            host = address.substr(0, colon);

            try {
                port = static_cast<std::uint32_t>(std::stoul(address.substr(colon + 1)));
            }
            catch (const std::exception &) {
                // Left at the default rather than refused: the host half is
                // still usable, and a typo in the port is not worth a dead end.
            }
        }
        
        gApplication->RequestConnectTo(host, port, nickname);
    }
} // namespace VantaMP::Core
