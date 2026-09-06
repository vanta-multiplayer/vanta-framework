#pragma once

struct IDirect3DDevice9;

namespace VantaMP::SDK {
    // Installs the world-overlay callback on CHud::Draw (plugin-sdk drawHudEvent).
    void InstallWorldOverlay(void (*draw)());

    // The D3D9 device nametags draw with. Set when the device is created or
    // reset; the Tahoma texture is dropped on lost so it is not used after Release.
    void SetWorldOverlayDevice(IDirect3DDevice9 *device);
    void OnWorldOverlayDeviceLost();
    void OnWorldOverlayDeviceReset(IDirect3DDevice9 *device);

    // One nametag: Tahoma "name (id)", then GTA-style armour and health bars.
    void DrawNametag(float x, float y, const char *label, float health, float armour, float alpha);
} // namespace VantaMP::SDK
