#pragma once

namespace Framework::Scripting {
    class Engine;
}

namespace VantaMP::Scripting {
    // Publishes client Player / Vehicle globals (readonly replica handles).
    void Register(Framework::Scripting::Engine *engine);
} // namespace VantaMP::Scripting
