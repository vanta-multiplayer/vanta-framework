#include "application.h"

#include "connection.h"
#include "net.h"

#include <logging/logger.h>

#include "core/client_config.h"
#include "core/diagnostics/breadcrumbs.h"
#include "core/globals.h"
#include "core/nui/nui.h"
#include "core/voice_io.h"
#include "core/ui/window_input.h"
#include "core/ui/ped_debug_ui.h"
#include "game/patches/boot.h"
#include "scripting/bindings.h"
#include "sdk/world/draw.h"

#include <graphics/backend/d3d9.h>
#include "game/patches/display.h"
#include "game/patches/script.h"
#include "sdk/world/camera.h"
#include "sdk/ped/local_ped.h"
#include "sdk/vehicle/occupancy.h"
#include "shared/constants.h"
#include "shared/version.h"

#include <atomic>

namespace VantaMP::Core {
    std::unique_ptr<Application> gApplication = nullptr;

    void Application::PostInit() {
        Net::Register();

        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("VantaMP client initialized");
        ApplyVoiceSettings();
    }

    void Application::OnConnectionFinalized(float serverTickRate) {
        // AN INTERVAL IN SECONDS, NOT A FREQUENCY. Printing it as Hz read "0 Hz"
        // for a perfectly good 0.1s tick, which looked exactly like a server
        // that had never configured one.
        //
        // The screen is NOT revealed here. Being accepted is not being placed:
        // the server decides where this player belongs, and says so with a
        // spawn - which is what lifts the fade.
        Core::OnConnectionFinalized(*this, serverTickRate);
    }

    void Application::OnConnectionClosed() {
        Core::OnConnectionClosed(*this);
    }

    void Application::OnAssetsDownloadProgress(const Framework::Integrations::Client::AssetDownloadStatus &status) {
        Core::OnAssetsDownloadProgress(*this, status);
    }

    void Application::OnConnectionPhaseChanged(Framework::Integrations::Client::ConnectionPhase phase) {
        Core::OnConnectionPhaseChanged(*this, phase);
    }

    void Application::PublishVoice() {
        auto &voice = GetVoiceClient();
        InvalidateVoiceSpeakerCache();

        DWORD foregroundPid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
        const bool focused   = foregroundPid == GetCurrentProcessId();
        const bool capturing = Nui::IsCapturingInput();
        const bool latched   = IsTalkLatched();
        const bool held      = (GetAsyncKeyState(voice.GetPushToTalkKey()) & 0x8000) != 0;
        const bool talking   = focused && (latched || held);

        if (!capturing) {
            Nui::ReleaseVoiceFocus();
        }

        voice.SetPushToTalk(talking);
        voice.SetTransmitBlocked(!talking && (!focused || capturing));

        if (!Nui::IsInWorld()) {
            return;
        }

        const auto camera = SDK::ReadListenerCamera();
        if (!camera.valid) {
            return;
        }

        Framework::Voice::ListenerTransform listener;
        listener.position = {camera.x, camera.y, camera.z};
        listener.forward  = {camera.forwardX, camera.forwardY, camera.forwardZ};
        listener.up       = {camera.upX, camera.upY, camera.upZ};

        const auto ped = SDK::QueryLocalPed();
        if (ped.valid) {
            listener.attenuationPosition = {ped.x, ped.y, ped.z};

            // Chase-cam sits metres behind the cabin and springs. Pan is
            // measured from listener.position, so that motion chops the mix
            // every frame. Distance still uses the ped.
            if (SDK::QueryPedOccupancy(0).seated) {
                listener.position = listener.attenuationPosition;
            }
        }

        voice.SetListenerTransform(listener);
    }

    void Application::Update() {
        PublishVoice();
        Framework::Integrations::Client::Instance::Update();
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
        Nui::TickMicTest();

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
        StopMicTest();
        Nui::ReportMicTest(false);
        SetTalkLatched(false);
        Nui::ShutdownInterface();
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("VantaMP client shutting down");
    }

    void Application::ModuleRegister(Framework::Scripting::Engine *engine) {
        Scripting::Register(engine);
    }

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

    namespace {
        std::atomic_bool gDisplayApply {false};
    }

    void RequestDisplayApply() {
        gDisplayApply.store(true, std::memory_order_release);
    }

    void FlushDisplayApply(IDirect3DDevice9 *device, HWND window) {
        if (!gDisplayApply.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        if (!Game::ApplyLiveDisplay(device, window)) {
            return;
        }

        RECT client {};
        GetClientRect(window, &client);
        Nui::ResizeInterface(client.right - client.left, client.bottom - client.top);
        Nui::RefreshSettings();
    }
} // namespace VantaMP::Core
