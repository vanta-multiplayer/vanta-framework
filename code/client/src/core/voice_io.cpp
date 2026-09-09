#include "core/voice_io.h"

#include "core/application.h"
#include "core/client_config.h"
#include "game/patches/audio_output.h"
#include "game/sync/player_sync.h"
#include "sdk/ped/remote_ped.h"
#include "shared/entity/player_entity.h"
#include "shared/world/world_space.h"

#include <utils/safe_win32.h>

#include <MinHook.h>
#include <core_modules.h>
#include <glm/vec3.hpp>
#include <logging/logger.h>
#include <miniaudio.h>
#include <networking/replication/replication_manager.h>
#include <utils/hooking/hook_function.h>
#include <voice/client/voice_client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace VantaMP::Core {
    namespace {
        ma_device *gTestDevice = nullptr;
        std::atomic<float> gLevel {0.0f};
        std::atomic<bool> gTalkLatched {false};
        bool gPausedVoiceForTest = false;

        void PauseVoiceForTest() {
            if (!gApplication) {
                return;
            }

            auto &voice = gApplication->GetVoiceClient();
            if (!voice.IsEnabled()) {
                return;
            }

            voice.SetEnabled(false);
            gPausedVoiceForTest = true;
        }

        void ResumeVoiceAfterTest() {
            if (!gPausedVoiceForTest) {
                return;
            }

            gPausedVoiceForTest = false;

            if (!gApplication) {
                return;
            }

            auto &voice = gApplication->GetVoiceClient();
            if (!voice.IsEnabled()) {
                voice.SetEnabled(true);
            }
        }

        using MaDeviceInitFn = ma_result (*)(ma_context *, const ma_device_config *, ma_device *);
        MaDeviceInitFn gMaDeviceInit = nullptr;

        ma_device_id gCaptureId {};
        ma_device_id gPlaybackId {};
        bool gHaveCapture  = false;
        bool gHavePlayback = false;
        std::string gAppliedInput  = "default";
        std::string gAppliedOutput = "default";
        bool gHooked = false;

        using SetSpeakerPositionFn = void *;
        SetSpeakerPositionFn gSetSpeakerPosition = nullptr;
        std::uint64_t gCachedSpeaker = 0;
        glm::vec3 gCachedLive {};
        bool gCachedLiveOk = false;

        bool LiveSpeakerPosition(std::uint64_t guid, glm::vec3 &out) {
            if (guid == 0) {
                return false;
            }

            if (guid == gCachedSpeaker) {
                if (gCachedLiveOk) {
                    out = gCachedLive;
                }

                return gCachedLiveOk;
            }

            gCachedSpeaker = guid;
            gCachedLiveOk  = false;

            auto *replication = Framework::CoreModules::GetReplication();
            if (!replication) {
                return false;
            }

            Shared::PlayerEntity *player = nullptr;
            replication->ForEachEntity([&](Framework::Networking::Replication::NetworkEntity *entity) {
                if (player || static_cast<std::uint64_t>(entity->ownerGUID) != guid) {
                    return;
                }

                player = dynamic_cast<Shared::PlayerEntity *>(entity);
            });

            if (!player || player->IsOwner()) {
                return false;
            }

            const auto localWorld = Game::QueryLocalWorldSpace();

            if (!Shared::SameWorld(localWorld.interior, localWorld.dimension, player->interior, player->dimension)) {
                // Proximity still hears the last packet position if we
                // refuse. A kilometre away is silence at 40 m.
                gCachedLive   = {10000.0f, 10000.0f, 10000.0f};
                gCachedLiveOk = true;
                out           = gCachedLive;
                return true;
            }

            const int slot = Game::SlotOfNetworkId(static_cast<std::uint64_t>(player->GetNetworkID()));
            if (slot < 0) {
                return false;
            }

            const auto head = SDK::GetRemotePedBonePosition(slot, SDK::kBoneHead);
            if (head.x != 0.0f || head.y != 0.0f || head.z != 0.0f) {
                gCachedLive   = {head.x, head.y, head.z};
                gCachedLiveOk = true;
                out           = gCachedLive;
                return true;
            }

            const auto body = SDK::GetRemotePedPosition(slot);
            if (body.x != 0.0f || body.y != 0.0f || body.z != 0.0f) {
                gCachedLive   = {body.x, body.y, body.z};
                gCachedLiveOk = true;
                out           = gCachedLive;
                return true;
            }

            return false;
        }

        class SpeakerPositionDetour {
          public:
            void Thunk(std::uint64_t speaker, const glm::vec3 &position);
        };

        void SpeakerPositionDetour::Thunk(std::uint64_t speaker, const glm::vec3 &position) {
            auto *self = reinterpret_cast<Framework::Voice::VoiceClient *>(this);
            glm::vec3 live = position;
            const glm::vec3 &use = LiveSpeakerPosition(speaker, live) ? live : position;

            using Original = void (Framework::Voice::VoiceClient::*)(std::uint64_t, const glm::vec3 &);
            Original original {};
            std::memcpy(&original, &gSetSpeakerPosition, sizeof(original));
            (self->*original)(speaker, use);
        }

        void *SpeakerPositionAddress() {
            using Member = void (Framework::Voice::VoiceClient::*)(std::uint64_t, const glm::vec3 &);
            const Member member = &Framework::Voice::VoiceClient::SetSpeakerPosition;
            void *address       = nullptr;
            std::memcpy(&address, &member, sizeof(address));
            return address;
        }

        void *SpeakerHookAddress() {
            using Member = void (SpeakerPositionDetour::*)(std::uint64_t, const glm::vec3 &);
            const Member member = &SpeakerPositionDetour::Thunk;
            void *address       = nullptr;
            std::memcpy(&address, &member, sizeof(address));
            return address;
        }

        bool FillWasapiId(const std::string &text, ma_device_id &out) {
            if (text.empty() || text == "default") {
                return false;
            }

            std::memset(&out, 0, sizeof(out));
            return MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.wasapi, 64) > 0;
        }

        std::string WideToUtf8(const wchar_t *text) {
            if (text == nullptr || text[0] == L'\0') {
                return {};
            }

            const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
            if (bytes <= 1) {
                return {};
            }

            std::string out(static_cast<std::size_t>(bytes - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), bytes, nullptr, nullptr);
            return out;
        }

        ma_result MaDeviceInit_Hook(ma_context *context, const ma_device_config *config, ma_device *device) {
            ma_device_config copy = *config;

            if (copy.capture.pDeviceID == nullptr && gHaveCapture && (copy.deviceType == ma_device_type_capture || copy.deviceType == ma_device_type_duplex)) {
                copy.capture.pDeviceID = &gCaptureId;
            }

            if (copy.playback.pDeviceID == nullptr && gHavePlayback && (copy.deviceType == ma_device_type_playback || copy.deviceType == ma_device_type_duplex)) {
                copy.playback.pDeviceID = &gPlaybackId;
            }

            return gMaDeviceInit(context, &copy, device);
        }

        void InstallDeviceHook() {
            if (gHooked) {
                return;
            }

            auto logger = Framework::Logging::GetLogger("Voice");
            auto *target = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(&ma_device_init));
            const MH_STATUS created = MH_CreateHook(target, reinterpret_cast<void *>(&MaDeviceInit_Hook), reinterpret_cast<void **>(&gMaDeviceInit));
            if (created != MH_OK) {
                logger->error("Voice device hook failed to install ({})", static_cast<int>(created));
                return;
            }

            MH_EnableHook(target);

            auto *speaker = SpeakerPositionAddress();
            const MH_STATUS speakers = MH_CreateHook(speaker, SpeakerHookAddress(), &gSetSpeakerPosition);
            if (speakers != MH_OK) {
                logger->error("Voice speaker hook failed to install ({})", static_cast<int>(speakers));
            }
            else {
                MH_EnableHook(speaker);
            }

            gHooked = true;
        }

        void WantDevice(bool capture, const std::string &id) {
            ma_device_id parsed {};
            const bool have = FillWasapiId(id, parsed);

            if (!have && !id.empty() && id != "default") {
                Framework::Logging::GetLogger("Voice")->warn("Voice {} id is not a WASAPI endpoint: {}", capture ? "input" : "output", id);
            }

            if (capture) {
                gHaveCapture = have;
                if (have) {
                    gCaptureId = parsed;
                }
            }
            else {
                gHavePlayback = have;
                if (have) {
                    gPlaybackId = parsed;
                }
            }
        }

        std::vector<VoiceEndpoint> ListEndpoints(bool capture) {
            ma_context context {};
            if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
                return {};
            }

            ma_device_info *playback     = nullptr;
            ma_uint32 playbackCount      = 0;
            ma_device_info *captureInfos = nullptr;
            ma_uint32 captureCount       = 0;

            if (ma_context_get_devices(&context, &playback, &playbackCount, &captureInfos, &captureCount) != MA_SUCCESS) {
                ma_context_uninit(&context);
                return {};
            }

            const ma_device_info *list = capture ? captureInfos : playback;
            const ma_uint32 count      = capture ? captureCount : playbackCount;

            std::vector<VoiceEndpoint> endpoints;
            endpoints.reserve(count);

            for (ma_uint32 i = 0; i < count; ++i) {
                VoiceEndpoint endpoint;
                endpoint.id        = WideToUtf8(list[i].id.wasapi);
                endpoint.name      = list[i].name;
                endpoint.isDefault = list[i].isDefault != 0;
                if (!endpoint.id.empty()) {
                    endpoints.push_back(std::move(endpoint));
                }
            }

            ma_context_uninit(&context);
            return endpoints;
        }

        void OnDuplex(ma_device *, void *output, const void *input, std::uint32_t frames) {
            auto *out = static_cast<float *>(output);
            const auto *in = static_cast<const float *>(input);

            float acc = 0.0f;

            for (std::uint32_t i = 0; i < frames; ++i) {
                const float sample = in ? in[i] : 0.0f;
                acc += sample * sample;
                out[i * 2]     = sample;
                out[i * 2 + 1] = sample;
            }

            if (frames > 0) {
                const float rms = std::sqrt(acc / static_cast<float>(frames));
                gLevel.store(std::clamp(rms * 400.0f, 0.0f, 100.0f), std::memory_order_relaxed);
            }
        }
    } // namespace

    std::vector<VoiceEndpoint> ListVoiceInputs() {
        return ListEndpoints(true);
    }

    std::vector<VoiceEndpoint> ListVoiceOutputs() {
        return ListEndpoints(false);
    }

    bool StartMicTest(const std::string &inputId, const std::string &outputId) {
        StopMicTest();
        PauseVoiceForTest();

        ma_device_config config  = ma_device_config_init(ma_device_type_duplex);
        config.capture.format    = ma_format_f32;
        config.capture.channels  = 1;
        config.playback.format   = ma_format_f32;
        config.playback.channels = 2;
        config.sampleRate        = 48000;
        config.dataCallback      = &OnDuplex;

        ma_device_id captureId {};
        ma_device_id playbackId {};

        if (FillWasapiId(inputId, captureId)) {
            config.capture.pDeviceID = &captureId;
        }

        if (FillWasapiId(outputId, playbackId)) {
            config.playback.pDeviceID = &playbackId;
        }

        auto *device = new (std::nothrow) ma_device {};
        if (!device) {
            ResumeVoiceAfterTest();
            return false;
        }

        if (ma_device_init(nullptr, &config, device) != MA_SUCCESS || ma_device_start(device) != MA_SUCCESS) {
            ma_device_uninit(device);
            delete device;
            ResumeVoiceAfterTest();
            Framework::Logging::GetLogger("Voice")->warn("Mic test could not open the selected devices");
            return false;
        }

        gTestDevice = device;
        gLevel.store(0.0f, std::memory_order_relaxed);
        return true;
    }

    void StopMicTest() {
        if (gTestDevice) {
            ma_device_uninit(gTestDevice);
            delete gTestDevice;
            gTestDevice = nullptr;
            gLevel.store(0.0f, std::memory_order_relaxed);
        }

        ResumeVoiceAfterTest();
    }

    bool IsMicTestRunning() {
        return gTestDevice != nullptr;
    }

    float MicTestLevel() {
        return gLevel.load(std::memory_order_relaxed);
    }

    void SetTalkLatched(bool on) {
        if (on) {
            StopMicTest();
        }

        gTalkLatched.store(on, std::memory_order_relaxed);
    }

    bool IsTalkLatched() {
        return gTalkLatched.load(std::memory_order_relaxed);
    }

    void InvalidateVoiceSpeakerCache() {
        gCachedSpeaker = 0;
        gCachedLiveOk  = false;
    }

    void ApplyVoiceSettings() {
        if (!gApplication) {
            return;
        }

        InstallDeviceHook();

        const auto &config = GetClientConfig();
        auto &voice        = gApplication->GetVoiceClient();

        const bool devicesChanged = gAppliedInput != config.voiceInputDevice || gAppliedOutput != config.voiceOutputDevice;

        WantDevice(true, config.voiceInputDevice);
        WantDevice(false, config.voiceOutputDevice);
        gAppliedInput  = config.voiceInputDevice;
        gAppliedOutput = config.voiceOutputDevice;

        voice.SetMasterVolume(config.voiceVolume);
        voice.SetPushToTalkKey(config.voicePttKey);
        Game::SetGameOutputDevice(config.voiceOutputDevice);

        if (devicesChanged && voice.IsEnabled()) {
            voice.SetEnabled(false);
            voice.SetEnabled(true);
        }

    }

    void InstallVoiceDeviceHook() {
        InstallDeviceHook();
    }
} // namespace VantaMP::Core

static InitFunction init(
    []() {
        VantaMP::Core::InstallVoiceDeviceHook();
    },
    "VoiceDevices");
