#pragma once

#include <cstdint>
#include <string>

namespace VantaMP::Core {
    // Settings a player can change without a rebuild.
    //
    // Read once at startup from VantaMP.json beside the client, and written
    // back with whatever was missing filled in - so a first run produces a file
    // showing every option and its default rather than an empty one.
    //
    // Deliberately small. This is for things a player legitimately chooses;
    // anything the client needs in order to work correctly belongs in code
    // where it cannot be set to a value that breaks the game.
    struct ClientConfig {
        // 0 means "the size of the monitor the game opened on". A borderless
        // window ignores these entirely and always covers its monitor.
        std::uint32_t displayWidth  = 0;
        std::uint32_t displayHeight = 0;

        // BORDERLESS BY DEFAULT. Exclusive fullscreen is opt-in: it takes
        // the display mode and costs a device Reset on every alt-tab, which
        // two clients on one machine do constantly. A borderless window
        // covering the monitor looks the same and has neither problem.
        //
        // Set false for a conventional window with a title bar, which is what
        // running two clients side by side wants.
        bool displayBorderless = true;

        // Exclusive fullscreen. Takes the selected resolution as the display
        // mode. Applied at the next launch; a live Reset on this exe crashes.
        bool displayExclusive = false;

        // D3D adapter the Device Selection dialog last confirmed. 0 is the
        // default GPU; a machine with an iGPU and a dGPU needs this remembered
        // or the next launch silently goes back to whichever RW enumerates
        // first.
        std::uint32_t displayAdapter = 0;

        // False until the player has confirmed a display, either through
        // GTA's Device Selection dialog or by saving in Settings.
        //
        // A missing VantaMP.json is written with defaults on the first
        // launch, so "the file exists" is not first-run. This flag is.
        // An older file that never had the key is treated as already
        // set up, otherwise existing installs would see the dialog again.
        bool displaySetupDone = false;

        // 0-1. Master scales the two GTA channels before they are written
        // as 0-64. Changing them is live; the file is only so the next
        // launch opens at the same mix.
        float volumeMaster = 1.0f;
        float volumeSfx    = 1.0f;
        float volumeRadio  = 1.0f;

        // Voice endpoints. "default" is whatever the OS currently prefers.
        std::string voiceInputDevice  = "default";
        std::string voiceOutputDevice = "default";

        // Playback gain for other players. 1 is unity; 2 is the slider's top.
        float voiceVolume = 1.0f;

        // Win32 virtual-key code. V is the framework default.
        int voicePttKey = 0x56;

        // Frames per second, or 0 for no cap.
        //
        // Capped by default. GTA's physics are frame-time dependent, so an
        // uncapped client on a modern machine runs the game in territory it was
        // never tested in - and in multiplayer that means two players doing the
        // same thing getting different results.
        std::uint32_t frameLimit = 60;

        // Last server the player joined, and the name they announced as.
        // Written on connect so the next start and `reconnect` use the same
        // identity instead of the compile-time default.
        std::string serverHost = "127.0.0.1";
        std::uint32_t serverPort = 27015;
        std::string nickname = "Player";

        // Corner FPS / ping / throughput. Off until a player asks for it.
        bool netgraph = false;
    };

    // Loads the config, creating it if it is absent.
    //
    // Never fails in a way that stops the client: a missing, unreadable or
    // malformed file leaves every setting at its default and says so in the
    // log. A player who breaks their JSON gets a working game and a warning,
    // rather than a mod that will not start.
    const ClientConfig &LoadClientConfig(const std::string &projectPath);

    const ClientConfig &GetClientConfig();

    // Changes a setting and writes it back.
    //
    // The player picked it in the menu, so it has to outlive the session -
    // otherwise choosing a resolution appears to work and is forgotten on the
    // next start.
    // UINT32_MAX keeps the adapter already in the file. Anything else is
    // the GPU the Device Selection dialog just confirmed.
    void SaveDisplaySettings(std::uint32_t width, std::uint32_t height, bool borderless, bool exclusive, std::uint32_t adapter = 0xFFFFFFFFu);

    [[nodiscard]] bool IsDisplaySetupDone();

    // Last server and the name this client will announce as. Written when the
    // player asks to connect, so the next start opens on the same identity
    // rather than the compile-time default.
    void SaveConnectSettings(const std::string &host, std::uint32_t port, const std::string &nickname);

    void SaveVolumeSettings(float master, float sfx, float radio);
    void SaveVoiceSettings(const std::string &inputDevice, const std::string &outputDevice, float volume, int pttKey);
    void SaveNetgraphSetting(bool enabled);
} // namespace VantaMP::Core
