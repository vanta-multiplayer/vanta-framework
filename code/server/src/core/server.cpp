#include "server.h"

#include "eac/eac.h"
#include "net.h"
#include "scripting/bindings.h"
#include "shared/catalog/model_catalog.h"
#include "shared/entity/player_entity.h"
#include "shared/world/spawn_points.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <voice/server/voice_server.h>

namespace VantaMP {
    void Server::PostInit() {
        if (Shared::ModelCatalog::Load("catalog/gta_sa.models")) {
            Shared::ModelCatalog::Merge("catalog/custom.models");
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)
                ->info("Model catalog: {} vehicles, {} peds, {} weapons, {} upgrades", Shared::ModelCatalog::VehicleCount(),
                       Shared::ModelCatalog::PedCount(), Shared::ModelCatalog::WeaponCount(), Shared::ModelCatalog::UpgradeCount());
        }
        else {
            Shared::ModelCatalog::UseRetail();
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("catalog/gta_sa.models missing: using the built-in retail list");
        }

        if (auto *voice = Framework::CoreModules::GetVoiceServer()) {
            voice->SetProximityRange(40.0f);
        }

        Net::Register(*this);

        Eac::Init();

        // Start the wall clock at noon so GetTime advances before any script
        // call. Broadcast is a no-op with zero peers.
        _world.SetTime(12, 0);

        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Server ready");
    }

    void Server::PostUpdate() {
        Eac::Tick();
    }

    void Server::PreShutdown() {
        Eac::Shutdown();
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Server shutting down");
    }

    void Server::ModuleRegister(Framework::Scripting::Engine *engine) {
        Scripting::Register(engine, _players, _vehicles, _world);
    }

    void Server::OnPlayerConnect(const Framework::Integrations::Server::PlayerConnectionData &data) {
        _players.Add(data.guid, data.nickname);
        Eac::OnPlayerConnected(data.guid);
    }

    void Server::OnPlayerReady(MafiaNet::PeerGuid guid) {
        // CONNECTING IS NOT SPAWNING. Until this the player has an entity but no
        // place in the world. A resource listening for playerReady decides
        // whether they appear, and where. The C++ spawn below is only the
        // fallback for a server with no resource loaded.
        _world.SendTo(guid);

        auto *player = _players.Find(guid);

        if (player && Scripting::EmitPlayerReady(static_cast<std::uint64_t>(player->GetNetworkID()))) {
            return;
        }

        _players.Spawn(guid, Shared::Spawn::kDefaultX, Shared::Spawn::kDefaultY, Shared::Spawn::kDefaultZ);
    }

    void Server::OnPlayerDisconnect(MafiaNet::PeerGuid guid) {
        Eac::OnPlayerDisconnect(guid);
        _vehicles.HandleDisconnect(guid);
        _players.Remove(guid);
    }
} // namespace VantaMP
