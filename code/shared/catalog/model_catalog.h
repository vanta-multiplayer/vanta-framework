#pragma once

#include <cstdint>
#include <string>

namespace VantaMP::Shared {
    // Valid GTA model IDs, loaded from a baked binary so the server never
    // has to open the game exe and a script cannot invent an ID that would
    // crash a client.
    //
    // The file is the allow-list. Retail GTA:SA ships as data/catalog/gta_sa.models.
    // A later custom-models pass appends skins / cars to the same format
    // (or a second file that is merged). An ID that is not in the loaded
    // tables is refused — including 400–611 that happen to be unused, and
    // any weapon the owner reports that is not a held type.
    class ModelCatalog final {
      public:
        enum class Kind : std::uint8_t {
            Vehicle = 1,
            Ped     = 2,
            Weapon  = 3,
            Upgrade = 4,
        };

        // Built-in retail lists. Used when no file is present so a forgotten
        // copy does not brick create(); the file still wins when it loads,
        // which is how custom models will replace or extend the set.
        static void UseRetail();

        // Replaces the tables. Returns false on a bad header or a truncated
        // file; the previous tables stay. Merge adds IDs without dropping
        // the ones already loaded.
        static bool Load(const std::string &path);
        static bool Merge(const std::string &path);

        [[nodiscard]] static bool IsVehicle(std::uint32_t modelId);
        [[nodiscard]] static bool IsPed(std::uint32_t modelId);
        [[nodiscard]] static bool IsWeapon(std::uint32_t weaponType);
        [[nodiscard]] static bool IsUpgrade(std::uint32_t modelId);

        [[nodiscard]] static std::uint32_t VehicleCount();
        [[nodiscard]] static std::uint32_t PedCount();
        [[nodiscard]] static std::uint32_t WeaponCount();
        [[nodiscard]] static std::uint32_t UpgradeCount();
    };
} // namespace VantaMP::Shared
