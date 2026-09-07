#include "nui.h"

#include "core/application.h"
#include "core/client_config.h"
#include "core/nui/embedded_ui.h"
#include "core/nui/events.h"
#include "internal.h"
#include "core/nui/page_events.h"
#include "core/voice_io.h"

#include <core_modules.h>
#include <gui/manager.h>
#include <gui/view.h>
#include <logging/logger.h>

#include <nlohmann/json.hpp>

namespace VantaMP::Nui {
    using Internal::ApplyViewFocus;
    using Internal::BindCoreHandlers;
    using Internal::CurrentSettings;
    using Internal::FindView;
    using Internal::gConsoleOpen;
    using Internal::gInWorld;
    using Internal::gMenuOpen;
    using Internal::gReconnectPending;
    using Internal::gReconnectTo;
    using Internal::gViewId;
    using Internal::gVisible;
    using Internal::SetMenuOpen;

    void Emit(const char *event, const nlohmann::json &detail) {
        const auto args = detail.is_array() ? detail : detail.is_null() ? nlohmann::json::array() : nlohmann::json::array({detail});
        PushToPage(event, args.dump());
    }

    void BringUpInterface(int width, int height) {
        if (gViewId >= 0) {
            return;
        }

        auto *manager = Framework::CoreModules::GetWebManager();

        if (!manager || !manager->IsInitialized()) {
            Framework::Logging::GetLogger("NUI")->error("The web manager is not up; there will be no interface");
            return;
        }

        RegisterEmbeddedInterface(*manager);
        BindCoreHandlers();

        // 0x0 = fill the viewport and follow WM_SIZE. A fixed size here
        // is why the page stayed at the launch resolution after a live
        // apply: Manager::Resize only moves auto-resize views.
        gViewId = manager->CreateView(kEmbeddedUiUrl, 0, 0);

        if (gViewId < 0) {
            Framework::Logging::GetLogger("NUI")->error("The interface view could not be created");
            return;
        }

        auto *view = FindView();

        if (!view) {
            return;
        }

        view->SetAutoResize(true);
        view->Display(false);

        Framework::Logging::GetLogger("NUI")->info("Interface up at {}x{} ({})", width, height, kEmbeddedUiUrl);
    }

    void ResizeInterface(int width, int height) {
        if (width <= 0 || height <= 0) {
            return;
        }

        auto *manager = Framework::CoreModules::GetWebManager();

        if (!manager || !manager->IsInitialized()) {
            return;
        }

        manager->SetViewportConfiguration({width, height});

        if (auto *view = FindView()) {
            view->Resize(width, height);
        }
    }

    void SetInterfaceVisible(bool visible) {
        gVisible = visible;

        if (auto *view = FindView()) {
            view->Display(visible);
            view->Focus(visible && (gMenuOpen || gConsoleOpen));
        }

        if (visible) {
            Emit(Event::kUiReady);
        }
    }

    bool IsInterfaceVisible() {
        return gVisible;
    }

    bool IsMenuOpen() {
        return gVisible && gMenuOpen;
    }

    bool IsInWorld() {
        return gInWorld;
    }

    bool IsCapturingInput() {
        return gVisible && (gMenuOpen || gConsoleOpen);
    }

    void ReleaseVoiceFocus() {
        if (auto *target = FindView()) {
            target->Focus(false);
        }
    }

    void EnsureViewFocus() {
        ApplyViewFocus();
    }

    void ToggleMenu() {
        if (!gVisible) {
            return;
        }

        if (!gInWorld) {
            SetMenuOpen(true);
            return;
        }

        SetMenuOpen(!gMenuOpen);
    }

    void HandleEscape() {
        if (!gVisible) {
            return;
        }

        if (gConsoleOpen) {
            gConsoleOpen = false;
            ApplyViewFocus();
            Emit(Event::kConsoleOpen, nlohmann::json::array({false}));
            return;
        }

        if (!gInWorld) {
            SetMenuOpen(true);
            Emit(Event::kHotkeyEscape);
            return;
        }

        if (!gMenuOpen) {
            SetMenuOpen(true);
            return;
        }

        Emit(Event::kHotkeyEscape);
    }

    void ToggleConsole() {
        if (!gVisible) {
            return;
        }

        gConsoleOpen = !gConsoleOpen;
        ApplyViewFocus();
        Emit(Event::kConsoleOpen, nlohmann::json::array({gConsoleOpen}));
    }

    void RefreshSettings() {
        Emit(Event::kSettingsUpdate, nlohmann::json::array({CurrentSettings()}));
    }

    void ReportMicTest(bool running) {
        Emit(Event::kSettingsMicRunning, nlohmann::json::array({running}));

        if (!running) {
            Emit(Event::kSettingsMicLevel, nlohmann::json::array({0}));
        }
    }

    void TickMicTest() {
        if (Core::IsMicTestRunning()) {
            Emit(Event::kSettingsMicLevel, nlohmann::json::array({static_cast<int>(Core::MicTestLevel() + 0.5f)}));
            return;
        }

        if (!Core::gApplication) {
            return;
        }

        const auto level = Core::gApplication->GetVoiceClient().GetLocalLevel();
        Emit(Event::kSettingsMicLevel, nlohmann::json::array({static_cast<int>(level * 100.0f + 0.5f)}));
    }

    void ReportConnected(const std::string &serverName) {
        gInWorld  = true;
        gMenuOpen = false;
        ApplyViewFocus();

        Emit(Event::kConnectionSetServer, nlohmann::json::array({serverName}));
        Emit(Event::kConnectionConnected, nlohmann::json::array({serverName}));
        Emit(Event::kUiToggle, nlohmann::json::array({false}));
    }

    void ReportConnectionState(const std::string &state, const std::string &detail) {
        if (state == "connecting") {
            Emit(Event::kConnectionConnecting, detail.empty() ? nlohmann::json::array() : nlohmann::json::array({detail}));
            return;
        }

        if (state == "failed") {
            gInWorld = false;
            SetMenuOpen(true);
            Emit(Event::kConnectionFailed, nlohmann::json::array({detail.empty() ? "Connection failed" : detail, true}));
            return;
        }

        if (state == "disconnected") {
            gInWorld  = false;
            gMenuOpen = true;
            Core::SetTalkLatched(false);
            ApplyViewFocus();
            Emit(Event::kConnectionDisconnected, nlohmann::json::array({detail.empty() ? "Disconnected" : detail}));
            Emit(Event::kUiToggle, nlohmann::json::array({true}));

            if (gReconnectPending) {
                const auto target = gReconnectTo;
                gReconnectPending = false;
                gReconnectTo.clear();
                Core::RequestConnect(target, Core::GetClientConfig().nickname.empty() ? "Player" : Core::GetClientConfig().nickname);
            }

            return;
        }

        if (state == "connected") {
            return;
        }
    }

    void ReportConnectionProgress(const ConnectionProgress &progress) {
        nlohmann::json payload = {
            {"phase", progress.phase},
            {"downloading", progress.downloading},
            {"progress", progress.progress},
            {"filesDownloaded", progress.filesDownloaded},
            {"filesTotal", progress.filesTotal},
            {"bytesDownloaded", progress.bytesDownloaded},
            {"bytesTotal", progress.bytesTotal},
            {"currentFile", progress.currentFile},
        };

        Emit(Event::kConnectionProgress, nlohmann::json::array({payload}));
    }

    void ShutdownInterface() {
        if (gViewId < 0) {
            return;
        }

        if (auto *manager = Framework::CoreModules::GetWebManager()) {
            manager->DestroyView(gViewId);
        }

        ClearHandlers();
        gViewId = -1;
    }
} // namespace VantaMP::Nui
