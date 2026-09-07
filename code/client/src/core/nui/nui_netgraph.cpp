#include "internal.h"

#include "core/application.h"
#include "core/nui/events.h"

#include <utils/safe_win32.h>

#include <integrations/client/networking/engine.h>
#include <mafianet/peerinterface.h>
#include <mafianet/statistics.h>

namespace VantaMP::Nui {
    void TickNetgraph() {
        using Internal::gFps;
        using Internal::gInWorld;
        using Internal::gNetgraphOn;
        using Internal::gVisible;

        static LARGE_INTEGER frequency {};
        static LARGE_INTEGER last {};
        static double        emitWait   = 0.0;
        static bool          clockReady = false;

        if (!clockReady) {
            QueryPerformanceFrequency(&frequency);
            QueryPerformanceCounter(&last);
            clockReady = true;
        }

        LARGE_INTEGER now {};
        QueryPerformanceCounter(&now);

        const double dt = frequency.QuadPart > 0 ? static_cast<double>(now.QuadPart - last.QuadPart) / static_cast<double>(frequency.QuadPart) : 0.0;
        last            = now;

        if (dt > 0.0 && dt < 1.0) {
            const auto instant = static_cast<float>(1.0 / dt);
            gFps += (instant - gFps) * 0.35f;
        }

        if (!gNetgraphOn || !gVisible || !gInWorld) {
            emitWait = 0.0;
            return;
        }

        // FPS is sampled every frame. The page only needs a reading
        // once a second - tx/rx are already last-second totals, and
        // pushing a sample every PostUpdate is what made the corner
        // flicker.
        emitWait += dt;

        if (emitWait < 1.0) {
            return;
        }

        emitWait = 0.0;

        int rtt   = 0;
        double tx = 0.0;
        double rx = 0.0;

        if (Core::gApplication) {
            if (auto *engine = Core::gApplication->GetNetworkingEngine()) {
                if (auto *client = engine->GetNetworkClient()) {
                    rtt = client->GetPing();

                    if (auto *peer = client->GetPeer()) {
                        if (auto *stats = peer->GetStatistics(peer->GetSystemAddressFromIndex(0))) {
                            tx = static_cast<double>(stats->valueOverLastSecond[MafiaNet::ACTUAL_BYTES_SENT]);
                            rx = static_cast<double>(stats->valueOverLastSecond[MafiaNet::ACTUAL_BYTES_RECEIVED]);
                        }
                    }
                }
            }
        }

        nlohmann::json sample;
        sample["fps"]       = static_cast<int>(gFps + 0.5f);
        sample["rtt"]       = rtt;
        sample["tx"]        = tx;
        sample["rx"]        = rx;
        sample["connected"] = gInWorld;
        Emit(Event::kNetgraphUpdate, nlohmann::json::array({sample}));
    }
}
