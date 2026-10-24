// Pass-through replacement for TG4AC\Binaries\Win64\dll\ArrowheadUSBIO.dll.
//
// Without the cabinet's USB I/O board, ArrowheadUSBIOUpdate reports 0 on the
// volume knob channel, so the game mutes the DGO1/DGO2 reprint routes.
// This proxy loads the original DLL (renamed ArrowheadUSBIO_orig.dll), forwards
// every call to it, and after each update writes a fixed volume into that channel.
//
// Volume is read once from ArrowheadUSBIO_volume.ini next to this DLL:
//   [Volume]
//   Percent=100

#include <windows.h>

// Offset of the volume knob word in the struct passed to ArrowheadUSBIOUpdate
// (the 8th analog channel). The game computes volume = value / 65535.
#define VOLUME_OFFSET 0x12

typedef int (*fn4)(void *, void *, void *, void *);

static HMODULE orig;
static WORD volume_value = 0xFFFF;

static fn4 p_GetDeviceVersion, p_Close, p_GetStatus, p_Open, p_Update;
static fn4 p_NCancelRead, p_NGetID, p_NGetResult, p_NGetStatus, p_NRead;

static void load_original(HMODULE self)
{
    WCHAR dir[MAX_PATH], path[MAX_PATH], ini[MAX_PATH];
    DWORD n = GetModuleFileNameW(self, dir, MAX_PATH);
    while (n > 0 && dir[n - 1] != L'\\')
        n--;
    dir[n] = 0;

    lstrcpyW(path, dir);
    lstrcatW(path, L"ArrowheadUSBIO_orig.dll");
    orig = LoadLibraryW(path);

    lstrcpyW(ini, dir);
    lstrcatW(ini, L"ArrowheadUSBIO_volume.ini");
    UINT percent = GetPrivateProfileIntW(L"Volume", L"Percent", 100, ini);
    if (percent > 100)
        percent = 100;
    volume_value = (WORD)((percent * 0xFFFFu) / 100u);

    if (!orig)
        return;
    p_GetDeviceVersion = (fn4)GetProcAddress(orig, "ArrowheadUSBGetDeviceVersion");
    p_Close = (fn4)GetProcAddress(orig, "ArrowheadUSBIOClose");
    p_GetStatus = (fn4)GetProcAddress(orig, "ArrowheadUSBIOGetStatus");
    p_Open = (fn4)GetProcAddress(orig, "ArrowheadUSBIOOpen");
    p_Update = (fn4)GetProcAddress(orig, "ArrowheadUSBIOUpdate");
    p_NCancelRead = (fn4)GetProcAddress(orig, "NESiCAReaderCancelRead");
    p_NGetID = (fn4)GetProcAddress(orig, "NESiCAReaderGetID");
    p_NGetResult = (fn4)GetProcAddress(orig, "NESiCAReaderGetResult");
    p_NGetStatus = (fn4)GetProcAddress(orig, "NESiCAReaderGetStatus");
    p_NRead = (fn4)GetProcAddress(orig, "NESiCAReaderRead");
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        load_original(inst);
    }
    return TRUE;
}

// Every export takes at most one pointer argument; passing four registers
// through is harmless under the x64 calling convention.
#define FORWARD(name, ptr, fallback)                                   \
    __declspec(dllexport) int name(void *a, void *b, void *c, void *d) \
    {                                                                  \
        return ptr ? ptr(a, b, c, d) : (fallback);                     \
    }

FORWARD(ArrowheadUSBGetDeviceVersion, p_GetDeviceVersion, -1)
FORWARD(ArrowheadUSBIOClose, p_Close, 1)
FORWARD(ArrowheadUSBIOGetStatus, p_GetStatus, 0)
FORWARD(ArrowheadUSBIOOpen, p_Open, 1)
FORWARD(NESiCAReaderCancelRead, p_NCancelRead, 0)
FORWARD(NESiCAReaderGetID, p_NGetID, 0)
FORWARD(NESiCAReaderGetResult, p_NGetResult, 0)
FORWARD(NESiCAReaderGetStatus, p_NGetStatus, 0)
FORWARD(NESiCAReaderRead, p_NRead, 0)

__declspec(dllexport) int ArrowheadUSBIOUpdate(void *io, void *b, void *c, void *d)
{
    int r = p_Update ? p_Update(io, b, c, d) : 1;
    if (io)
        *(WORD *)((BYTE *)io + VOLUME_OFFSET) = volume_value;
    return r;
}
