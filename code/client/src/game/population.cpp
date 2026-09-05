#include "population.h"

#include "sdk/patterns.h"

#include <logging/logger.h>
#include <utils/hooking/hooking.h>

namespace VantaMP::Game {
    namespace {
        bool gDisabled = false;
    } // namespace

    void DisableAutonomousPopulation() {
        if (gDisabled) {
            return;
        }

        gDisabled = true;

        const auto &p = SDK::gPatterns;

        // --- Pedestrians -----------------------------------------------------

        // RETURNS FALSE, NOT MERELY RETURNS. CPopulation::Add hands back whether
        // it created a ped and its caller acts on that, so a bare `ret` leaves
        // whatever happened to be in eax and the caller intermittently believes
        // in a ped that was never made.
        hook::return_value_function(p.CPopulation__Add, 0);

        hook::return_function(p.CPopulation__ManagePed);

        // Stops the game deleting peds it did not create. Entity lifetime
        // belongs to the server, and GTA's own cleanup has no idea which peds
        // are ours.
        hook::return_function(p.CPopulation__RemovePed);

        // CPopulation::Update is cut short after ManagePopulation rather than
        // stubbed: the rest of the function is bookkeeping the game expects to
        // have run, and skipping it entirely upsets state elsewhere.
        //
        //   pop esi ; ret
        hook::put<std::uint8_t>(p.CPopulation__UpdateEarlyOut, 0x5E);
        hook::put<std::uint8_t>(p.CPopulation__UpdateEarlyOut + 1, 0xC3);

        // --- Traffic ---------------------------------------------------------

        hook::return_function(p.CCarCtrl__GenerateRandomCars);

        // Same reasoning as RemovePed: the server owns vehicle lifetime.
        hook::return_function(p.CCarCtrl__RemoveCarsIfPoolFull);

        // --- Ambient model streaming ------------------------------------------

        // Stops the streamer pulling in ped and vehicle models FOR POPULATION.
        // General streaming is untouched: the world still streams and anything
        // requested explicitly still loads.
        hook::return_function(p.CStreaming__StreamPedsAndCars);

        // jnz -> jmp, so the ambient-model branch is never taken.
        hook::put<std::uint8_t>(p.CStreaming__AmbientModelsJump, 0xEB);

        // --- Single-player systems that spawn peds of their own ---------------

        hook::return_function(p.CGangWars__Update);
        hook::return_function(p.CPedType__InterestingEvents);
        hook::return_function(p.CPed__PlayerConversations);

        Framework::Logging::GetLogger("Game")->info("Autonomous population disabled; streaming, paths and pools remain available");
    }

    bool IsAutonomousPopulationDisabled() {
        return gDisabled;
    }
} // namespace VantaMP::Game
