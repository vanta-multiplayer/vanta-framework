#include "application.h"

#include <logging/logger.h>
#include <networking/replication/entity_registry.h>

#include "shared/player_entity.h"

#include "core/client_config.h"
#include "core/ui/ped_debug_ui.h"
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
    }

    void Application::OnConnectionClosed() {
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Disconnected from the server");
    }

    void Application::PostUpdate() {
        // CONNECT ONCE THE PLAYER EXISTS, not at startup.
        //
        // The server's first act is to give this connection an entity, and
        // anything it then tells us about the world assumes there is somewhere
        // to put it. Connecting before the local player is created means the
        // first replicated state arrives with nothing to apply it to.
        //
        // Attempted once. A failed connect is reported and left alone rather
        // than retried every frame, which would bury the reason in a thousand
        // identical log lines.
        if (!_connectAttempted && Game::IsPlayerSpawned()) {
            _connectAttempted = true;

            const auto &config = GetClientConfig();

            const auto result = ConnectToServer(config.serverHost, static_cast<std::int32_t>(config.serverPort), "");

            if (!result) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Could not reach {}:{} - {}", config.serverHost, config.serverPort, result.GetError().message);
            }
            else {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Connecting to {}:{} as {}", config.serverHost, config.serverPort, config.nickname);
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
        }
    }

    void TickApplication() {
        if (!gApplication || !gApplication->IsInitialized()) {
            return;
        }

        gApplication->Update();
        gApplication->Render();

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
