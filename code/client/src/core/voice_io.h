#pragma once

#include <string>
#include <vector>

namespace VantaMP::Core {
    struct VoiceEndpoint {
        std::string id;
        std::string name;
        bool isDefault = false;
    };

    [[nodiscard]] std::vector<VoiceEndpoint> ListVoiceInputs();
    [[nodiscard]] std::vector<VoiceEndpoint> ListVoiceOutputs();

    // Hear-yourself loopback on the chosen endpoints. Independent of the
    // voice session, so it works from the menu before a connect.
    bool StartMicTest(const std::string &inputId, const std::string &outputId);
    void StopMicTest();
    [[nodiscard]] bool IsMicTestRunning();
    [[nodiscard]] float MicTestLevel();

    void SetTalkLatched(bool on);
    [[nodiscard]] bool IsTalkLatched();

    void ApplyVoiceSettings();
} // namespace VantaMP::Core
