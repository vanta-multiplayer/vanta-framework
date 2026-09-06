#pragma once

namespace Framework::GUI {
    class Manager;
}

namespace VantaMP::Nui {
    void RegisterEmbeddedInterface(Framework::GUI::Manager &manager);

    inline constexpr const char *kEmbeddedUiUrl = "http://vanta/ui.html";
} // namespace VantaMP::Nui
