#include "model_catalog.h"

#include "shared/vehicle/upgrades.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

namespace VantaMP::Shared {
    namespace {
        constexpr char kMagic[4] = {'V', 'N', 'T', 'C'};
        constexpr std::uint16_t kVersion = 1;

        std::vector<std::uint16_t> gVehicles;
        std::vector<std::uint16_t> gPeds;
        std::vector<std::uint16_t> gWeapons;
        std::vector<std::uint16_t> gUpgrades;

        void EnsureRetail() {
            if (gVehicles.empty() && gPeds.empty() && gWeapons.empty() && gUpgrades.empty()) {
                ModelCatalog::UseRetail();
            }
        }

        bool Contains(const std::vector<std::uint16_t> &table, std::uint32_t id) {
            if (id > 0xFFFFu) {
                return false;
            }

            return std::binary_search(table.begin(), table.end(), static_cast<std::uint16_t>(id));
        }

        void SortUnique(std::vector<std::uint16_t> &table) {
            std::sort(table.begin(), table.end());
            table.erase(std::unique(table.begin(), table.end()), table.end());
        }

        void UnionInto(std::vector<std::uint16_t> &into, const std::vector<std::uint16_t> &extra) {
            into.insert(into.end(), extra.begin(), extra.end());
            SortUnique(into);
        }

        bool ReadTable(std::ifstream &in, std::vector<std::uint16_t> &out) {
            std::uint16_t count = 0;

            if (!in.read(reinterpret_cast<char *>(&count), sizeof(count))) {
                return false;
            }

            out.resize(count);

            if (count == 0) {
                return true;
            }

            return static_cast<bool>(in.read(reinterpret_cast<char *>(out.data()), static_cast<std::streamsize>(count * sizeof(std::uint16_t))));
        }

        bool ReadFile(const std::string &path, std::vector<std::uint16_t> &vehicles, std::vector<std::uint16_t> &peds, std::vector<std::uint16_t> &weapons,
                      std::vector<std::uint16_t> &upgrades) {
            std::ifstream in(path, std::ios::binary);

            if (!in) {
                return false;
            }

            char magic[4] {};
            std::uint16_t version = 0;
            std::uint16_t flags   = 0;

            if (!in.read(magic, 4) || std::memcmp(magic, kMagic, 4) != 0) {
                return false;
            }

            if (!in.read(reinterpret_cast<char *>(&version), sizeof(version)) || version != kVersion) {
                return false;
            }

            if (!in.read(reinterpret_cast<char *>(&flags), sizeof(flags))) {
                return false;
            }

            return ReadTable(in, vehicles) && ReadTable(in, peds) && ReadTable(in, weapons) && ReadTable(in, upgrades);
        }
    } // namespace

    void ModelCatalog::UseRetail() {
        gVehicles.clear();
        gPeds.clear();
        gWeapons.clear();
        gUpgrades.clear();

        for (std::uint16_t id = 400; id <= 611; ++id) {
            gVehicles.push_back(id);
        }

        for (std::uint16_t id = 0; id <= 312; ++id) {
            if (id != 74 && id != 149 && id != 208) {
                gPeds.push_back(id);
            }
        }

        for (std::uint16_t id = 0; id <= 0x2E; ++id) {
            gWeapons.push_back(id);
        }

        for (std::uint16_t id = kFirstUpgrade; id <= kLastUpgrade; ++id) {
            gUpgrades.push_back(id);
        }
    }

    bool ModelCatalog::Load(const std::string &path) {
        std::vector<std::uint16_t> vehicles;
        std::vector<std::uint16_t> peds;
        std::vector<std::uint16_t> weapons;
        std::vector<std::uint16_t> upgrades;

        if (!ReadFile(path, vehicles, peds, weapons, upgrades)) {
            return false;
        }

        SortUnique(vehicles);
        SortUnique(peds);
        SortUnique(weapons);
        SortUnique(upgrades);

        gVehicles = std::move(vehicles);
        gPeds     = std::move(peds);
        gWeapons  = std::move(weapons);
        gUpgrades = std::move(upgrades);
        return true;
    }

    bool ModelCatalog::Merge(const std::string &path) {
        std::vector<std::uint16_t> vehicles;
        std::vector<std::uint16_t> peds;
        std::vector<std::uint16_t> weapons;
        std::vector<std::uint16_t> upgrades;

        if (!ReadFile(path, vehicles, peds, weapons, upgrades)) {
            return false;
        }

        UnionInto(gVehicles, vehicles);
        UnionInto(gPeds, peds);
        UnionInto(gWeapons, weapons);
        UnionInto(gUpgrades, upgrades);
        return true;
    }

    bool ModelCatalog::IsVehicle(std::uint32_t modelId) {
        EnsureRetail();
        return Contains(gVehicles, modelId);
    }

    bool ModelCatalog::IsPed(std::uint32_t modelId) {
        EnsureRetail();
        return Contains(gPeds, modelId);
    }

    bool ModelCatalog::IsWeapon(std::uint32_t weaponType) {
        EnsureRetail();
        return Contains(gWeapons, weaponType);
    }

    bool ModelCatalog::IsUpgrade(std::uint32_t modelId) {
        EnsureRetail();
        return Contains(gUpgrades, modelId) && UpgradeSlot(static_cast<std::uint16_t>(modelId)) >= 0;
    }

    std::uint32_t ModelCatalog::VehicleCount() {
        return static_cast<std::uint32_t>(gVehicles.size());
    }

    std::uint32_t ModelCatalog::PedCount() {
        return static_cast<std::uint32_t>(gPeds.size());
    }

    std::uint32_t ModelCatalog::WeaponCount() {
        return static_cast<std::uint32_t>(gWeapons.size());
    }

    std::uint32_t ModelCatalog::UpgradeCount() {
        return static_cast<std::uint32_t>(gUpgrades.size());
    }
} // namespace VantaMP::Shared
