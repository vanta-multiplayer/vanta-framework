#include "application.h"

#include <logging/logger.h>

#include "core/ui/ped_debug_ui.h"
#include "shared/constants.h"
#include "shared/version.h"

namespace VantaMP::Core {
    std::unique_ptr<Application> gApplication = nullptr;

    void Application::PostInit() {
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("VantaMP client initialized");
    }

    void Application::PostUpdate() {
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
        options.gameVersion = "1.0.1.33";
        options.modSlug     = "vantamp";
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
