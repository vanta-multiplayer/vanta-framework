#include "ped_debug_ui.h"

#include <imgui.h>

#include <cfloat>

#include "sdk/game.h"
#include "sdk/patterns.h"

namespace VantaMP::Core::UI {
    namespace {
        // Retail hashes its model names, so only the ones worth naming are spelled out
        const char *ModelName(int modelIndex) {
            switch (modelIndex) {
            case 0: return "CJ (player)";
            case 1: return "male01";
            case 2: return "bfori";
            case 7: return "wmycr";
            default: return nullptr;
            }
        }

        const char *PedTypeName(int pedType) {
            switch (pedType) {
            case 0: return "PLAYER1";
            case 1: return "PLAYER2";
            case 2: return "PLAYER_NETWORK";
            case 3: return "PLAYER_UNUSED";
            case 4: return "CIVMALE";
            case 5: return "CIVFEMALE";
            case 6: return "COP";
            default: return "other";
            }
        }
    } // namespace

    void DrawPedDebugPanel() {
        ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(320.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));

        if (!ImGui::Begin("VantaMP - Local Ped", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::End();
            return;
        }

        const auto state = SDK::QueryLocalPed();

        if (!state.valid) {
            ImGui::TextDisabled("No player ped yet");
            ImGui::TextDisabled("CWorld::Players %08X", static_cast<unsigned>(SDK::gPatterns.CWorld__Players));
            ImGui::End();
            return;
        }

        ImGui::Text("Position  %.2f  %.2f  %.2f", state.x, state.y, state.z);
        ImGui::Text("Heading   %.1f deg", state.heading * 57.2957795f);

        ImGui::Separator();

        if (const auto name = ModelName(state.modelIndex)) {
            ImGui::Text("Model     %d (%s)", state.modelIndex, name);
        }
        else {
            ImGui::Text("Model     %d", state.modelIndex);
        }
        ImGui::Text("Ped type  %d (%s)", state.pedType, PedTypeName(state.pedType));

        ImGui::Separator();

        const auto healthFraction = state.maxHealth > 0.0f ? state.health / state.maxHealth : 0.0f;
        ImGui::ProgressBar(healthFraction, ImVec2(-1.0f, 0.0f));
        ImGui::Text("Health    %.0f / %.0f", state.health, state.maxHealth);

        if (state.inVehicle) {
            ImGui::Separator();
            ImGui::Text("In vehicle, model %d", state.vehicleModelIndex);
        }

        ImGui::Separator();
        ImGui::TextDisabled("CWorld::Players %08X", static_cast<unsigned>(SDK::gPatterns.CWorld__Players));

        ImGui::End();
    }
} // namespace VantaMP::Core::UI
