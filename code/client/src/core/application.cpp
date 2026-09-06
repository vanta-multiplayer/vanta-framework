#include "application.h"

#include <logging/logger.h>
#include <core_modules.h>
#include <networking/network_peer.h>
#include <networking/replication/entity_registry.h>

#include "shared/player_entity.h"
#include "shared/vehicle_entity.h"
#include "shared/rpc/local_player_spawn.h"
#include "shared/rpc/player_enter_vehicle.h"
#include "shared/rpc/player_exit_vehicle.h"
#include "shared/rpc/vehicle_damage_report.h"
#include "shared/rpc/player_reload.h"
#include "shared/rpc/player_shot.h"

#include "core/client_config.h"
#include "core/diagnostics/breadcrumbs.h"
#include "core/globals.h"
#include "core/nui/nui.h"
#include "core/ui/window_input.h"
#include "core/ui/ped_debug_ui.h"
#include "game/patches/boot.h"
#include "sdk/draw.h"

#include <graphics/backend/d3d9.h>
#include "game/sync/player_sync.h"
#include "game/sync/shots.h"
#include "game/sync/vehicle_sync.h"
#include "game/camera.h"
#include "game/entities/local_player.h"
#include "game/patches/script.h"
#include "shared/constants.h"
#include "shared/version.h"

namespace VantaMP::Core {
    std::unique_ptr<Application> gApplication = nullptr;

    void Application::PostInit() {
        // The same name the server registers, so the CRC32 that identifies this
        // type on the wire matches without either side shipping a number. A
        // mismatch here does not fail loudly - the entity simply never
        // constructs, and remote players are invisible for no stated reason.
        Framework::Networking::Replication::EntityRegistry::Get().Register<Shared::PlayerEntity>(Shared::PlayerEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<Shared::VehicleEntity>(Shared::VehicleEntity::kTypeName);

        // The server placing this player in the world. Until it arrives the
        // local ped is at a placeholder behind a black screen, because where a
        // player belongs is not the client's decision.
        if (auto *net = Framework::CoreModules::GetNetworkPeer()) {
            net->RegisterRPC<Shared::RPC::LocalPlayerSpawn>([](const Shared::RPC::LocalPlayerSpawn &spawn, MafiaNet::Packet *) {
                Game::ApplyLocalSpawn(spawn.x, spawn.y, spawn.z, spawn.heading, spawn.health, spawn.armour);
            });

            // Combat events, not snapshot fields. The server stamps the shooter
            // and fans the packet out; we only apply what arrives.
            net->RegisterRPC<Shared::RPC::PlayerShot>([](const Shared::RPC::PlayerShot &shot, MafiaNet::Packet *) {
                Game::ApplyRemoteShot(shot.networkId, shot.weaponType, shot.originX, shot.originY, shot.originZ, shot.targetX, shot.targetY, shot.targetZ, shot.order, shot.damagedNetworkId, shot.damage);
            });

            net->RegisterRPC<Shared::RPC::PlayerReload>([](const Shared::RPC::PlayerReload &reload, MafiaNet::Packet *) {
                Game::ApplyRemoteReload(reload.networkId);
            });

            net->RegisterRPC<Shared::RPC::PlayerEnterVehicle>([](const Shared::RPC::PlayerEnterVehicle &enter, MafiaNet::Packet *) {
                Game::ApplyRemoteEnterVehicle(enter.networkId, enter.vehicleId, enter.seat, enter.warp, enter.door);
            });

            net->RegisterRPC<Shared::RPC::PlayerExitVehicle>([](const Shared::RPC::PlayerExitVehicle &exit, MafiaNet::Packet *) {
                Game::ApplyRemoteExitVehicle(exit.networkId, exit.warp, exit.door);
            });

            net->RegisterRPC<Shared::RPC::VehicleDamageReport>([](const Shared::RPC::VehicleDamageReport &report, MafiaNet::Packet *) {
                Game::ApplyRemoteVehicleDamage(report.vehicleId, report.damageEngine, report.doorsPacked, report.wheelsPacked, report.panels, report.lights,
                                               report.doorRatiosPacked, report.applyRatios);
            });
        }

        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("VantaMP client initialized");
    }

    void Application::OnConnectionFinalized(float serverTickRate) {
        // AN INTERVAL IN SECONDS, NOT A FREQUENCY. Printing it as Hz read "0 Hz"
        // for a perfectly good 0.1s tick, which looked exactly like a server
        // that had never configured one.
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Connected; the server ticks every {:.0f}ms", serverTickRate * 1000.0f);

        // THE PAGE CLOSES ITS OWN MENU, and keeps its console and netgraph.
        // Hiding the view would take all three, which is not what a player who
        // just joined a server wants.
        Diagnostics::Breadcrumbs::SetStage(Diagnostics::Stage::InGame);

        Nui::ReportConnected(GetClientConfig().serverHost);
        Nui::ReportConnectionState("connected", "");

        // The screen is NOT revealed here. Being accepted is not being placed:
        // the server decides where this player belongs, and says so with a
        // spawn - which is what lifts the fade.
    }

    void Application::OnConnectionClosed() {
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Disconnected from the server");

        // Back to the menu, and the remote players go with the connection -
        // their peds belong to this client, while the entities were the
        // network's.
        Game::ClearRemotePlayers();
        Game::ClearRemoteVehicles();

        // Back behind the fade: the menu belongs over a black screen, not over
        // a world the player is no longer in.
        Game::FadeOut();

        Nui::ReportConnectionState("disconnected", "");
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
        auto host = address;
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

        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("{} asked to join {}:{}", nickname, host, port);

        gApplication->RequestConnectTo(host, port, nickname);
    }

    void Application::PostUpdate() {
        // The menu waits for the game's own startup to finish.
        //
        // The copyright notice is the one screen Vanta does not remove, so it
        // is also the one screen the interface must not cover. The frontend
        // being bypassed is the moment after it, and the earliest point at
        // which the menu is the only thing worth looking at.
        if (!_menuShown && Game::IsFrontendBypassed()) {
            _menuShown = true;
            Nui::SetInterfaceVisible(true);
        }

        Nui::TickNetgraph();

        // THE CURSOR BELONGS TO WHOEVER HAS FOCUS, EVERY FRAME.
        //
        // Turning it on once at startup leaves it drawn over the game for ever,
        // so a player in a world has a pointer they cannot use and no mouse
        // look. It follows the menu instead: up while the menu is, gone the
        // moment the page closes it.
        if (auto *imgui = GetImGUI(); imgui && imgui->IsInitialized()) {
            const bool wantsPointer = Nui::IsMenuOpen();

            if (wantsPointer != _cursorShown) {
                _cursorShown = wantsPointer;
                imgui->ShowCursor(wantsPointer);
            }
        }

        // THE VIEWS ARE DRAWN THROUGH IMGUI, NOT BY Manager::Render().
        //
        // Render() paints the page into a texture; SubmitImGuiDraws() is what
        // puts that texture on the screen, and it has to run inside an ImGui
        // frame. Calling only the first is why the interface initialised
        // cleanly, reported itself up, and showed nothing at all.
        //
        // Queued first so it lands under the debug panel rather than over it.
        if (GetImGUI() && GetImGUI()->IsInitialized()) {
            GetImGUI()->PushWidget([this]() {
                if (auto *webManager = GetWebManager(); webManager && webManager->IsInitialized()) {
                    webManager->SubmitImGuiDraws();
                }
            });
        }

        if (_disconnectRequested) {
            _disconnectRequested = false;
            _connectRequested    = false;
            _pendingHost.clear();
            _pendingNickname.clear();

            if (auto *engine = GetNetworkingEngine()) {
                if (auto *client = engine->GetNetworkClient()) {
                    (void)client->Disconnect();
                }
            }
        }

        // The menu asked to connect, on a CEF thread. Acted on here instead,
        // because connecting touches state that belongs to the game thread.
        if (_connectRequested && !_pendingHost.empty()) {
            _connectRequested = false;

            const auto host = _pendingHost;
            const auto port = _pendingPort;
            auto nickname   = _pendingNickname;

            _pendingHost.clear();
            _pendingNickname.clear();

            if (nickname.empty()) {
                nickname = GetClientConfig().nickname;
            }

            // Identity is read from this when assets finish, not from the
            // connect call. Leaving it empty is why every remote nametag
            // used to say "Player".
            Framework::Integrations::Client::CurrentState state;
            state.host     = host;
            state.port     = static_cast<std::int32_t>(port);
            state.nickname = nickname;
            SetCurrentState(state);
            SaveConnectSettings(host, port, nickname);

            Diagnostics::Breadcrumbs::SetStage(Diagnostics::Stage::Connecting);

            const auto result = ConnectToServer(host, static_cast<std::int32_t>(port), "");

            if (!result) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Could not reach {}:{} - {}", host, port, result.GetError().message);
                Nui::ReportConnectionState("failed", result.GetError().message);
            }
            else {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Connecting to {}:{}", host, port);
                Nui::ReportConnectionState("connecting", host + ":" + std::to_string(port));
            }
        }

        // Ped debug is in-world only. Drawing it on the connect screen puts an
        // ImGui window over the menu before there is a ped worth inspecting.
        if (Nui::IsInWorld() && GetImGUI() && GetImGUI()->IsInitialized()) {
            GetImGUI()->PushWidget([]() {
                UI::DrawPedDebugPanel();
            });
        }
    }

    void Application::PreShutdown() {
        Nui::ShutdownInterface();
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("VantaMP client shutting down");
    }

    void Application::ModuleRegister(Framework::Scripting::Engine *) {}

    void BringUpApplication(IDirect3DDevice9 *device, HWND window) {
        if (gApplication) {
            return;
        }

        gApplication = std::make_unique<Application>();

        Framework::Graphics::RendererConfiguration rendererOptions = {};
        rendererOptions.backend                                    = Framework::Graphics::RendererBackend::BACKEND_D3D_9;
        rendererOptions.platform                                   = Framework::Graphics::PlatformBackend::PLATFORM_WIN32;
        rendererOptions.windowHandle                               = window;
        rendererOptions.d3d9.device                                = device;

        Framework::Integrations::Client::InstanceOptions options;
        options.useRenderer     = true;
        options.usePresence     = false;
        options.useImGUI        = true;
        options.rendererOptions = rendererOptions;

        options.gameName    = Shared::kGameName;
        options.gameVersion = Shared::kGameVersion;
        options.modSlug     = Shared::kModSlug;
        options.modVersion  = Version::rel;

        // Crash reporting. The module path is where crashpad_handler.exe sits,
        // which is beside the client rather than beside the game - the process
        // is gta_sa.exe, so the working directory is no guide.
        options.sentryDSN         = Shared::kSentryDSN;
        options.sentryModulePath  = gProjectPath;
        options.sentryEnvironment = Shared::kSentryEnvironment;

        // The log is what turns a stack trace into an explanation: it carries
        // the pattern scan results and the startup sequence leading up to the
        // fault.
        options.sentryAttachments = {fmt::format("{}/logs/VantaMP.log", gProjectPath)};

        SDK::SetWorldOverlayDevice(device);

        if (const auto result = gApplication->Init(options); !result) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Failed to initialize the client: {}", result.GetError().message);
            gApplication.reset();
            return;
        }

        // CEF, and the interface on top of it.
        //
        // The framework constructs the web manager but does not start it - the
        // root directory and the viewport are the project's to decide, and it
        // has no way to guess either.
        RECT client {};
        GetClientRect(window, &client);

        const int width  = client.right - client.left;
        const int height = client.bottom - client.top;

        auto *webManager = gApplication->GetWebManager();

        if (webManager) {
            const auto cefRoot = gProjectPath;

            if (const auto result = webManager->Init(cefRoot, {width, height}, gApplication->GetRenderer(), false); !result) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("CEF did not start: {}", result.GetError().message);
            }
            else {
                Nui::BringUpInterface(width, height);

                // The interface is useless without a pointer and a keyboard.
                // CEF has no window of its own, so the messages have to be
                // handed to it by whoever owns the one on screen.
                VantaMP::UI::InstallWindowHook(window);


            }
        }
    }

    void TickApplication() {
        if (!gApplication || !gApplication->IsInitialized()) {
            return;
        }

        gApplication->Update();
        gApplication->Render();

        // CEF paints into a texture here; nothing is on screen yet.
        if (auto *webManager = gApplication->GetWebManager(); webManager && webManager->IsInitialized()) {
            webManager->Render();
        }

        // Instance::Render builds the frame; only the mod knows where in it to submit
        if (const auto imgui = gApplication->GetImGUI(); imgui && imgui->IsInitialized()) {
            imgui->Render();
        }
    }

    void OnDeviceLost() {
        SDK::OnWorldOverlayDeviceLost();

        if (gApplication && gApplication->IsInitialized()) {
            if (const auto imgui = gApplication->GetImGUI(); imgui && imgui->IsInitialized()) {
                imgui->OnDeviceLost();
            }
        }
    }

    void OnDeviceReset() {
        if (gApplication && gApplication->IsInitialized()) {
            if (const auto imgui = gApplication->GetImGUI(); imgui && imgui->IsInitialized()) {
                imgui->OnDeviceReset();
            }

            if (auto *renderer = gApplication->GetRenderer()) {
                if (auto *d3d = renderer->GetD3D9Backend()) {
                    SDK::OnWorldOverlayDeviceReset(d3d->GetDevice());
                }
            }
        }
    }
} // namespace VantaMP::Core
