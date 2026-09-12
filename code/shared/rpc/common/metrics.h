#pragma once

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <string>

namespace VantaMP::Shared::RPC::Metrics {
    // Per-packet-type protocol counters, for every hot packet in the
    // project - vehicle and player alike.
    //
    // MEASURED, NOT ESTIMATED. The point of this file is that no claim about
    // bandwidth in this protocol should come from reading a struct
    // definition: a struct says nothing about how often it is sent, how many
    // recipients it has, or what the transport adds. Every number reported
    // for this refactor comes from here.
    //
    // Bytes recorded are PAYLOAD bytes as handed to the transport, times the
    // number of recipients. RakNet's own datagram headers are not included
    // and are noted separately where they matter - the RPC4 identifier cost
    // IS included, because it is written by our own send path.
    enum class PacketType : std::uint8_t {
        DriverSync = 0,
        UnoccupiedSync,
        Resync,
        Damage,
        SyncOwner,
        PlayerOnFootSync,
        EnterVehicle,
        ExitVehicle,
        OccupancyAbort,
        Count,
    };

    inline constexpr std::size_t kPacketTypeCount = static_cast<std::size_t>(PacketType::Count);

    [[nodiscard]] inline const char *PacketTypeName(PacketType type) {
        switch (type) {
        case PacketType::DriverSync: return "VehicleDriverSync";
        case PacketType::UnoccupiedSync: return "VehicleUnoccupiedSync";
        case PacketType::Resync: return "VehicleResync";
        case PacketType::Damage: return "VehicleDamageReport";
        case PacketType::SyncOwner: return "VehicleSyncOwner";
        case PacketType::PlayerOnFootSync: return "PlayerOnFootSync";
        case PacketType::EnterVehicle: return "PlayerEnterVehicle";
        case PacketType::ExitVehicle: return "PlayerExitVehicle";
        case PacketType::OccupancyAbort: return "PlayerOccupancyAbort";
        default: return "Unknown";
        }
    }

    struct PacketCounter {
        std::uint64_t packets    = 0;
        std::uint64_t bytes      = 0;
        std::uint64_t recipients = 0;
        std::uint32_t maxBytes   = 0;

        void Record(std::size_t payloadBytes, std::uint32_t recipientCount) {
            packets += 1;
            recipients += recipientCount;
            bytes += static_cast<std::uint64_t>(payloadBytes) * recipientCount;

            if (payloadBytes > maxBytes) {
                maxBytes = static_cast<std::uint32_t>(payloadBytes);
            }
        }

        [[nodiscard]] double AverageBytes() const {
            return packets == 0 ? 0.0 : static_cast<double>(bytes) / static_cast<double>(packets);
        }
    };

    // Process-wide counters. Deliberately not thread-safe: every vehicle
    // send happens on the game thread (client) or the tick thread (server),
    // and adding a lock to a counter on the hot path would cost more than
    // the thing it measures.
    class Counters final {
      public:
        static Counters &Get() {
            static Counters instance;

            return instance;
        }

        void Record(PacketType type, std::size_t payloadBytes, std::uint32_t recipients = 1) {
            _counters[static_cast<std::size_t>(type)].Record(payloadBytes, recipients);
        }

        [[nodiscard]] const PacketCounter &Counter(PacketType type) const {
            return _counters[static_cast<std::size_t>(type)];
        }

        void Reset() {
            _counters = {};
            _sinceMs  = 0;
        }

        void SetWindowStart(std::uint64_t nowMs) {
            _sinceMs = nowMs;
        }

        [[nodiscard]] std::uint64_t WindowStart() const {
            return _sinceMs;
        }

        [[nodiscard]] std::uint64_t TotalBytes() const {
            std::uint64_t total = 0;

            for (const auto &counter : _counters) {
                total += counter.bytes;
            }

            return total;
        }

        // Human-readable table: packets, bytes, bytes/sec, average and peak
        // payload per type. This is what gets pasted into a before/after
        // comparison.
        [[nodiscard]] std::string Report(std::uint64_t nowMs, std::uint32_t players) const {
            const double seconds = (nowMs > _sinceMs) ? static_cast<double>(nowMs - _sinceMs) / 1000.0 : 0.0;

            std::string out;

            out += "packet                     packets      bytes    B/s   avgB   maxB  recips\n";
            out += "-----------------------------------------------------------------------\n";

            for (std::size_t i = 0; i < kPacketTypeCount; ++i) {
                const auto &counter = _counters[i];

                if (counter.packets == 0) {
                    continue;
                }

                char line[192];

                std::snprintf(line, sizeof(line), "%-24s %8llu %10llu %6.0f %6.1f %6u %7llu\n", PacketTypeName(static_cast<PacketType>(i)),
                              static_cast<unsigned long long>(counter.packets), static_cast<unsigned long long>(counter.bytes),
                              seconds > 0.0 ? static_cast<double>(counter.bytes) / seconds : 0.0, counter.AverageBytes(), counter.maxBytes,
                              static_cast<unsigned long long>(counter.recipients));
                out += line;
            }

            char footer[192];
            const auto total = TotalBytes();

            std::snprintf(footer, sizeof(footer), "TOTAL %llu bytes over %.1fs = %.0f B/s", static_cast<unsigned long long>(total), seconds,
                          seconds > 0.0 ? static_cast<double>(total) / seconds : 0.0);
            out += footer;

            if (players > 0 && seconds > 0.0) {
                std::snprintf(footer, sizeof(footer), " = %.0f B/s per player (%u players)\n", static_cast<double>(total) / seconds / players, players);
                out += footer;
            }
            else {
                out += "\n";
            }

            return out;
        }

      private:
        std::array<PacketCounter, kPacketTypeCount> _counters {};
        std::uint64_t _sinceMs = 0;
    };
} // namespace VantaMP::Shared::RPC::Metrics
