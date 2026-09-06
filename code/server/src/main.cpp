#include "core/server.h"

#include "shared/constants.h"
#include "shared/sync_rates.h"
#include "shared/version.h"

#include <logging/logger.h>

int main(int argc, char **argv) {
    Framework::Integrations::Server::InstanceOptions opts;
    opts.bindHost      = "0.0.0.0";
    opts.bindPort      = 27015;
    opts.webBindHost   = "0.0.0.0";
    opts.webBindPort   = 27016;
    opts.maxPlayers    = 32;
    opts.modName       = "VantaMP";
    opts.modSlug       = VantaMP::Shared::kModSlug;
    opts.gameName      = VantaMP::Shared::kGameName;
    opts.gameVersion   = VantaMP::Shared::kGameVersion;
    opts.modVersion    = VantaMP::Version::rel;
    opts.bindPassword  = "";
    opts.enableSignals = true;

    // Crash reporting, tagged net.role=server by the framework so server faults
    // are separable from client ones in the same project.
    opts.sentryDSN         = VantaMP::Shared::kSentryDSN;
    opts.sentryEnvironment = VantaMP::Shared::kSentryEnvironment;

    // HOW OFTEN THE WORLD IS SENT, which is not how often it is drawn.
    //
    // The client interpolates between updates and the game animates from the
    // input that came with them, so sending a player's pose sixty times a
    // second costs bandwidth for movement nobody can see. MTA settled on 100ms
    // for players over twenty years of running real servers.
    //
    // It is also what the client sets its own auto-serialize interval from -
    // left at zero, entities never serialize at all and nothing ever moves.
    opts.worldConfig.tickInterval = static_cast<float>(VantaMP::Shared::SyncRate::kPlayerMs) / 1000.0f;

    // The interest grid's extent. San Andreas spans roughly -3000..3000, so the
    // default bounds are ample; the cell size is what decides how many
    // candidates a query walks.
    opts.worldConfig.streamCellSize = 100.0f;

    opts.argc = argc;
    opts.argv = argv;

    VantaMP::Server server;
    if (const auto result = server.Init(opts); !result) {
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Failed to start server: {}", result.GetError().message);
        return 1;
    }
    server.Run();
    server.Shutdown();
    return 0;
}
