#pragma once

#include <utils/safe_win32.h>

#include <integrations/client/instance.h>

#include <memory>

struct IDirect3DDevice9;

namespace VantaMP::Core {
    class Application: public Framework::Integrations::Client::Instance {
      public:
        void PostInit() override;
        void PostUpdate() override;
        void PreShutdown() override;

        void ModuleRegister(Framework::Scripting::Engine *engine) override;
    };

    extern std::unique_ptr<Application> gApplication;

    // Driven by the D3D9 device hooks
    void BringUpApplication(IDirect3DDevice9 *device, HWND window);
    void TickApplication();
    void OnDeviceLost();
    void OnDeviceReset();
} // namespace VantaMP::Core
