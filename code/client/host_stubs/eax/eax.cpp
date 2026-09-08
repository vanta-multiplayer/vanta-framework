// Vanta-owned EAX.DLL stand-in: forward Create APIs to dsound.dll.
// gta_sa.exe hard-imports EAX.DLL ordinal 6; shipping Rockstar's copy under the
// client root (or loading it from the GTA install) trips EAC Untrusted. This
// stub is our binary, hashed in base.bin, and satisfies the IAT without packing
// game files.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <mmsystem.h>
#include <dsound.h>

namespace {
    using DirectSoundCreateFn  = HRESULT(WINAPI *)(LPCGUID, LPDIRECTSOUND *, LPUNKNOWN);
    using DirectSoundCreate8Fn = HRESULT(WINAPI *)(LPCGUID, LPDIRECTSOUND8 *, LPUNKNOWN);

    HMODULE Dsound() {
        static HMODULE module = LoadLibraryW(L"dsound.dll");
        return module;
    }

    template <typename Fn>
    Fn Proc(const char *name) {
        HMODULE module = Dsound();
        if (!module) {
            return nullptr;
        }
        return reinterpret_cast<Fn>(GetProcAddress(module, name));
    }
} // namespace

extern "C" {

HRESULT WINAPI EAXDirectSoundCreate(LPCGUID pcGuidDevice, LPDIRECTSOUND *ppDS, LPUNKNOWN pUnkOuter) {
    if (auto *fn = Proc<DirectSoundCreateFn>("DirectSoundCreate")) {
        return fn(pcGuidDevice, ppDS, pUnkOuter);
    }
    return DSERR_NODRIVER;
}

HRESULT WINAPI EAXDirectSoundCreate8(LPCGUID pcGuidDevice, LPDIRECTSOUND8 *ppDS8, LPUNKNOWN pUnkOuter) {
    if (auto *fn = Proc<DirectSoundCreate8Fn>("DirectSoundCreate8")) {
        return fn(pcGuidDevice, ppDS8, pUnkOuter);
    }
    return DSERR_NODRIVER;
}

HRESULT WINAPI DllCanUnloadNow() {
    return S_FALSE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID, REFIID, LPVOID *ppv) {
    if (ppv) {
        *ppv = nullptr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllRegisterServer() {
    return S_OK;
}

HRESULT WINAPI DllUnregisterServer() {
    return S_OK;
}

DWORD WINAPI GetCurrentVersion() {
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) {
    return TRUE;
}

} // extern "C"
