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

        // BORDERLESS BY DEFAULT, and never exclusive fullscreen.
        //
        // Exclusive fullscreen on this engine regularly takes the display mode
        // and then never presents, which is indistinguishable from a hang. It
        // also costs a device reset on every alt-tab, and alt-tabbing between
        // two clients is most of what testing a multiplayer platform consists
        // of. A borderless window covering the monitor looks the same to a
        // player and has neither problem.
        //
        // Set false for a conventional window with a title bar, which is what
        // running two clients side by side wants.
        bool displayBorderless = true;

        // Where to connect, and as whom.
        //
        // A fixed address for now: the connect menu that would ask for one does
        // not exist yet, and hardcoding it in the client would mean a rebuild to
        // point at a different server.
        std::string serverHost = "127.0.0.1";
        std::uint32_t serverPort = 27015;
        std::string nickname = "Player";
    };

    // Loads the config, creating it if it is absent.
    //
    // Never fails in a way that stops the client: a missing, unreadable or
    // malformed file leaves every setting at its default and says so in the
    // log. A player who breaks their JSON gets a working game and a warning,
    // rather than a mod that will not start.
    const ClientConfig &LoadClientConfig(const std::string &projectPath);

    const ClientConfig &GetClientConfig();
} // namespace VantaMP::Core
