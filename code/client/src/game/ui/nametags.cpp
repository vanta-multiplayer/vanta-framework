#include "nametags.h"

#include "game/sync/player_sync.h"
#include "sdk/world/draw.h"
#include "sdk/game.h"
#include "shared/entity/player_entity.h"
#include "shared/world/world_space.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>
#include <voice/client/voice_client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace VantaMP::Game {
    void InstallNametags() {
        SDK::InstallWorldOverlay(DrawNametags);
    }

    void DrawNametags() {
        auto *replication = Framework::CoreModules::GetReplication();

        if (!replication) {
            return;
        }

        const auto local = SDK::QueryLocalPed();

        if (!local.valid) {
            return;
        }

        replication->ForEachEntity([&local](Framework::Networking::Replication::NetworkEntity *entity) {
            auto *player = dynamic_cast<Shared::PlayerEntity *>(entity);

            if (!player || player->IsOwner()) {
                return;
            }

            const auto localWorld = QueryLocalWorldSpace();

            if (!Shared::SameWorld(localWorld.interior, localWorld.dimension, player->interior, player->dimension)) {
                return;
            }

            const auto networkId = static_cast<std::uint64_t>(player->GetNetworkID());
            const int slot       = SlotOfNetworkId(networkId);

            if (slot < 0) {
                return;
            }

            const auto head   = SDK::GetRemotePedBonePosition(slot, SDK::kBoneHead);
            const auto screen = SDK::WorldToScreen(head.x, head.y, head.z + 0.40f);

            if (!screen.visible) {
                return;
            }

            const float dx = head.x - local.x;
            const float dy = head.y - local.y;
            const float dz = head.z - local.z;
            const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

            if (distance < 1.0f || distance > 50.0f) {
                return;
            }

            const char *name = player->nickname.empty() ? "Player" : player->nickname.c_str();
            bool talking     = false;

            if (auto *voice = Framework::CoreModules::GetVoiceClient()) {
                const auto guid = static_cast<std::uint64_t>(player->ownerGUID);
                talking         = guid != 0 && voice->GetSpeakerLevel(guid) > 0.02f;
            }

            char label[96];
            if (talking) {
                std::snprintf(label, sizeof(label), "%s  (voice)", name);
            }
            else {
                std::snprintf(label, sizeof(label), "%s (%llu)", name, static_cast<unsigned long long>(networkId));
            }

            const float fadeStart = 35.0f;
            const float alpha     = distance <= fadeStart ? 1.0f : std::clamp(1.0f - (distance - fadeStart) / (50.0f - fadeStart), 0.0f, 1.0f);

            SDK::DrawNametag(screen.x, screen.y, label, player->health, player->armour, alpha, talking);
        });
    }
} // namespace VantaMP::Game
