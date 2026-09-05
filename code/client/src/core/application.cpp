#include "application.h"

#include <logging/logger.h>
#include <networking/replication/entity_registry.h>

#include "shared/player_entity.h"

#include "core/client_config.h"
#include "core/globals.h"
#include "core/ui/nui.h"
#include "core/ui/window_input.h"
#include "core/ui/ped_debug_ui.h"
#include "game/player_sync.h"
#include "game/world.h"
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

        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("VantaMP client initialized");
    }

    void Application::OnConnectionFinalized(float serverTickRate) {
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Connected; the server ticks at {:.0f} Hz", serverTickRate);

        // The world is the thing to look at now, not the menu.
        VantaMP::UI::SetMenuVisible(false);
        VantaMP::UI::ReportConnectionState("connected", "");
    }

    void Application::OnConnectionClosed() {
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Disconnected from the server");

        // Back to the menu, and the remote players go with the connection -
        // their peds belong to this client, while the entities were the
        // network's.
        Game::ClearRemotePlayers();

        VantaMP::UI::SetMenuVisible(true);
        VantaMP::UI::ReportConnectionState("disconnected", "");
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

        gApplication->RequestConnectTo(host, port);
    }

    void Application::PostUpdate() {
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

        // The menu asked to connect, on a CEF thread. Acted on here instead,
        // because connecting touches state that belongs to the game thread.
        if (_connectRequested && !_pendingHost.empty()) {
            _connectRequested = false;

            const auto host = _pendingHost;
            const auto port = _pendingPort;

            _pendingHost.clear();

            const auto result = ConnectToServer(host, static_cast<std::int32_t>(port), "");

            if (!result) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Could not reach {}:{} - {}", host, port, result.GetError().message);
                VantaMP::UI::ReportConnectionState("failed", result.GetError().message);
            }
            else {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Connecting to {}:{}", host, port);
                VantaMP::UI::ReportConnectionState("connecting", host + ":" + std::to_string(port));
            }
        }

        // Instance::Update drains the queue before this runs, so the panel lands a frame later
        if (GetImGUI() && GetImGUI()->IsInitialized()) {
            GetImGUI()->PushWidget([]() {
                UI::DrawPedDebugPanel();
            });
        }
    }

    void Application::PreShutdown() {
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
                VantaMP::UI::BringUpInterface(width, height);

                // The interface is useless without a pointer and a keyboard.
                // CEF has no window of its own, so the messages have to be
                // handed to it by whoever owns the one on screen.
                VantaMP::UI::InstallWindowHook(window);

                // AND A CURSOR TO POINT WITH.
                //
                // ImGui's, not the operating system's. The framework mirrors the
                // page's cursor shape onto ImGui and expects the host to be
                // drawing a software cursor; the OS one is hidden by the game
                // and would be the wrong pointer anyway - it does not know what
                // the page is hovering.
                if (auto *imgui = gApplication->GetImGUI(); imgui && imgui->IsInitialized()) {
                    imgui->ShowCursor(true);
                }
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
        }
    }
} // namespace VantaMP::Core
