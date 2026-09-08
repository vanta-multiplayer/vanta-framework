#pragma once

#include <mafianet/BitStream.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace VantaMP::Shared::RPC {
    // Opaque Easy Anti-Cheat / EOS bytes. Max 512 per EOS docs.
    inline constexpr std::uint16_t kEacOpaqueMax = 512;

    // Client → server registration after Connect login (PUID + JWT).
    struct EacRegister {
        static constexpr const char *kIdentifier = "VantaMP::EacRegister";

        std::string   productUserId;
        std::string   connectJwt;
        std::uint32_t clientPlatform = 1; // EOS_ACCCP_Windows
        // 1 = EOS_ACCCT_ProtectedClient (client called BeginSession).
        // 0 = EOS_ACCCT_UnprotectedClient.
        std::uint32_t clientType = 0;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, productUserId);
            bs->Serialize(write, connectJwt);
            bs->Serialize(write, clientPlatform);
            bs->Serialize(write, clientType);
        }
    };

    // Bidirectional opaque AC traffic (client↔server).
    struct EacOpaque {
        static constexpr const char *kIdentifier = "VantaMP::EacOpaque";

        std::uint16_t length = 0;
        std::uint8_t  data[kEacOpaqueMax] {};

        void Set(const void *bytes, std::uint32_t size) {
            // Truncating would yield EOS_ACCCAR_InvalidMessage on the peer.
            if (!bytes || size == 0 || size > kEacOpaqueMax) {
                length = 0;
                return;
            }
            length = static_cast<std::uint16_t>(size);
            std::memcpy(data, bytes, length);
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            // RakNet's aligned-safe blob: compressed length + byte-aligned payload.
            // Do not use per-byte Serialize — mid-byte packing corrupts AC binaries.
            if (write) {
                const unsigned int len = length;
                bs->WriteAlignedBytesSafe(reinterpret_cast<const char *>(data), len, kEacOpaqueMax);
            }
            else {
                unsigned int len = 0;
                if (!bs->ReadAlignedBytesSafe(reinterpret_cast<char *>(data), len, kEacOpaqueMax)) {
                    length = 0;
                    return;
                }
                length = static_cast<std::uint16_t>(len);
            }
        }
    };

    // Server → client: EOS asked to remove the player.
    struct EacAction {
        static constexpr const char *kIdentifier = "VantaMP::EacAction";

        std::int32_t  action     = 0;
        std::int32_t  reasonCode = 0;
        std::string   details;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, action);
            bs->Serialize(write, reasonCode);
            bs->Serialize(write, details);
        }
    };
} // namespace VantaMP::Shared::RPC
