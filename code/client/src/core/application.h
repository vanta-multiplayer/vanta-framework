#pragma once

#include <utils/safe_win32.h>

#include <integrations/client/instance.h>

#include <cstdint>
#include <memory>
#include <string>

struct IDirect3DDevice9;

namespace VantaMP::Core {
    class Application: public Framework::Integrations::Client::Instance {
      public:
        void PostInit() override;
        void Update() override;
        void PostUpdate() override;
        void PreShutdown() override;

        void ModuleRegister(Framework::Scripting::Engine *engine) override;

        void OnConnectionFinalized(float serverTickRate) override;
        void OnConnectionClosed() override;
        void OnAssetsDownloadProgress(const Framework::Integrations::Client::AssetDownloadStatus &status) override;
        void OnConnectionPhaseChanged(Framework::Integrations::Client::ConnectionPhase phase) override;

        // Queued rather than acted on: this is called from a CEF thread, and
        // connecting touches state the game thread owns.
        void RequestConnectTo(const std::string &host, std::uint32_t port, const std::string &nickname) {
            _pendingHost      = host;
            _pendingPort      = port;
            _pendingNickname  = nickname;
            _connectRequested = true;
        }

        void RequestDisconnect() {
            _disconnectRequested = true;
        }

      private:
        void PublishVoice();

        // Where the player asked to go, once they have asked. Empty until then:
        // the client no longer connects on its own.
        std::string _pendingHost;
        std::uint32_t _pendingPort = 0;
        std::string _pendingNickname;
        bool _connectRequested     = false;
        bool _disconnectRequested  = false;

        // The menu is held back until the game's own startup screens are done.
        bool _menuShown = false;

        // Mirrors the ImGui cursor state, so it is only toggled on a change.
        bool _cursorShown = false;
    };

    // Asked for by the menu. Takes effect on the next frame rather than
    // immediately, because this arrives on a CEF thread and connecting touches
    // state the game thread owns.
    void RequestConnect(const std::string &address, const std::string &nickname);
    void RequestDisconnect();

    extern std::unique_ptr<Application> gApplication;

    // Driven by the D3D9 device hooks
    void BringUpApplication(IDirect3DDevice9 *device, HWND window);
    void TickApplication();
    void OnDeviceLost();
    void OnDeviceReset();

    // Queued from the page thread. Applied from CGame::Process, outside
    // every D3D method. Reset from BeginScene or Present is INVALIDCALL.
    void RequestDisplayApply();
    void FlushDisplayApply(IDirect3DDevice9 *device, HWND window);
} // namespace VantaMP::Core
