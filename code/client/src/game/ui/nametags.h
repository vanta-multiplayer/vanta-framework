#pragma once

namespace VantaMP::Game {
    // Hooks CHud::Draw so nametags run in Render2dStuff, under the HUD.
    void InstallNametags();

    // Tahoma "name (id)" over remote players, with GTA-style health and armour
    // bars. Drawn in the world 2D pass, not through ImGui — the atlas path
    // was what turned the letters into white boxes.
    void DrawNametags();
} // namespace VantaMP::Game
