#pragma once

namespace VantaMP {
    class Server;

    namespace Net {
        // Entity types and the RPCs that are not the replica. PostInit
        // calls this once; new packets go here, not into server.cpp.
        void Register(Server &server);
    } // namespace Net
} // namespace VantaMP
