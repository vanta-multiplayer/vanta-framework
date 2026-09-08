#pragma once

#include <integrations/client/instance.h>

namespace VantaMP::Core {
    class Application;

    // Join / leave / download progress. These used to live on Application
    // because that is where the Framework callbacks land; the bodies do
    // not belong in the same file as CEF startup and the frame loop.
    void OnConnectionFinalized(Application &app, float serverTickRate);
    void OnConnectionClosed(Application &app);
    void OnAssetsDownloadProgress(Application &app, const Framework::Integrations::Client::AssetDownloadStatus &status);
    void OnConnectionPhaseChanged(Application &app, Framework::Integrations::Client::ConnectionPhase phase);
} // namespace VantaMP::Core
