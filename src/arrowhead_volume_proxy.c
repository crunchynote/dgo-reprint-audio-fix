// Pass-through replacement for TG4AC\Binaries\Win64\dll\ArrowheadUSBIO.dll.
//
// Without the cabinet's USB I/O board, ArrowheadUSBIOUpdate reports 0 on the
// volume knob channel, so the game mutes the DGO1/DGO2 reprint routes.
// This proxy loads the original DLL (renamed ArrowheadUSBIO_orig.dll), forwards
// every call to it, and after each update writes a fixed volume into that channel.
//
// It can also mute the virtual arcade's ambience around the routes (see below).
// Settings are read once from ArrowheadUSBIO_volume.ini next to this DLL:
//   [Volume]
//   Percent=100
//   [Arcade]
//   MuteAmbience=0

#include <windows.h>

// Offset of the volume knob word in the struct passed to ArrowheadUSBIOUpdate
// (the 8th analog channel). The game computes volume = value / 65535.
#define VOLUME_OFFSET 0x12

typedef int (*fn4)(void *, void *, void *, void *);

static HMODULE orig;
static WORD volume_value = 0xFFFF;

static fn4 p_GetDeviceVersion, p_Close, p_GetStatus, p_Open, p_Update;
static fn4 p_NCancelRead, p_NGetID, p_NGetResult, p_NGetStatus, p_NRead;

static void load_original(const WCHAR *dir, const WCHAR *ini)
{
    WCHAR path[MAX_PATH];
    lstrcpyW(path, dir);
    lstrcatW(path, L"ArrowheadUSBIO_orig.dll");
    orig = LoadLibraryW(path);

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

// ---------------------------------------------------------------------------
// Cue filter (TG4AC-Win64-Shipping.exe 5.80.02 only).
//
// Every sound the game starts goes through CRI's criAtomExPlayer_SetCueId(player,
// acb, id). We hook it so chosen cues can be muted per sound bank, or every cue
// logged. Besides [Arcade] MuteAmbience (see install_movie_mute), these advanced
// options in ArrowheadUSBIO_volume.ini are available:
//   [Mute]
//   legend=D3          ; hex cue IDs to mute in legend.acb (D3 = arcade room loop)
//   legend2=           ; legend2.acb (DGO2 reprint)
//   [Debug]
//   LogSounds=1        ; writes ArrowheadUSBIO_sounds.log next to this DLL
// ---------------------------------------------------------------------------

// criAtomExPlayer_SetCueId, checked against SET_CUE_ID_BYTES before patching.
#define RVA_SET_CUE_ID 0x1e3a44c
#define PROLOGUE_LEN 15

// mov [rsp+8],rbx / mov [rsp+10h],rsi / push rdi / sub rsp,20h / mov edi,r8d
static const BYTE SET_CUE_ID_BYTES[18] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10,
    0x57, 0x48, 0x83, 0xEC, 0x20, 0x41, 0x8B, 0xF8};

// A cue ID no bank uses: SetCueId fails and clears the player, so Start plays nothing.
#define SILENT_CUE_ID 0x7FFF0000

// DGOA_BACKG in legend.acb: the arcade room's background loop, muted by MuteAmbience.
#define ARCADE_ROOM_CUE 0xD3

#define MAX_BANKS 16
#define MAX_MUTED 64
#define MAX_HANDLES 32

typedef void (*set_cue_id_fn)(void *player, void *acb, int id);

struct bank {
    char name[32];
    WORD muted[MAX_MUTED];
    int count;
};

static struct bank banks[MAX_BANKS];
static int bank_count;
static struct { void *acb; char name[32]; } handles[MAX_HANDLES];
static int handle_count;
static set_cue_id_fn original_set_cue_id;
static BYTE *exe_base;
static HANDLE sound_log = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION lock;

static int readable(const void *p, SIZE_T len)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!p || !VirtualQuery(p, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT)
        return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
        return 0;
    return (const BYTE *)p + len <= (const BYTE *)mbi.BaseAddress + mbi.RegionSize;
}

static DWORD be32(const BYTE *p) { return (DWORD)p[0] << 24 | (DWORD)p[1] << 16 | (DWORD)p[2] << 8 | p[3]; }
static WORD be16(const BYTE *p) { return (WORD)(p[0] << 8 | p[1]); }

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

// Reads the "Name" column of row 0 from an in-memory ACB (@UTF table).
static int acb_name(const BYTE *utf, char *out)
{
    if (!readable(utf, 32) || utf[0] != '@' || utf[1] != 'U' || utf[2] != 'T' || utf[3] != 'F')
        return 0;
    DWORD size = be32(utf + 4);
    if (size < 24 || !readable(utf, size + 8))
        return 0;
    const BYTE *b = utf + 8;
    const BYTE *rows = b + be16(b + 2), *strings = b + be32(b + 4);
    int cols = be16(b + 16);
    static const int sizes[16] = {1, 1, 2, 2, 4, 4, 8, 8, 4, 0, 4, 8, 0, 0, 0, 0};
    const BYTE *c = b + 24;
    int pos = 0;
    for (int i = 0; i < cols; i++) {
        int flags = *c++;
        const char *cname = (const char *)strings + be32(c);
        c += 4;
        int storage = flags & 0xF0, type = flags & 0x0F;
        const BYTE *value = NULL;
        if (storage == 0x30) {
            value = c;
            c += sizes[type];
        } else if (storage == 0x50) {
            value = rows + pos;
            pos += sizes[type];
        }
        if (value && type == 0x0A && str_eq(cname, "Name")) {
            const char *s = (const char *)strings + be32(value);
            int n = 0;
            while (s[n] && n < 31) out[n] = s[n], n++;
            out[n] = 0;
            return n > 0;
        }
    }
    return 0;
}

// Finds the ACB's name by looking for its @UTF data up to two pointers away from the handle.
static void find_acb_name(void *acb, char *out)
{
    out[0] = 0;
    if (!readable(acb, 0x100))
        return;
    void **p = (void **)acb;
    for (int i = 0; i < 0x100 / 8; i++) {
        if (acb_name((const BYTE *)p[i], out))
            return;
        if (readable(p[i], 0x40)) {
            void **q = (void **)p[i];
            for (int j = 0; j < 0x40 / 8; j++)
                if (acb_name((const BYTE *)q[j], out))
                    return;
        }
    }
}

static const char *name_of(void *acb)
{
    for (int i = 0; i < handle_count; i++)
        if (handles[i].acb == acb)
            return handles[i].name;
    int slot = handle_count < MAX_HANDLES ? handle_count++ : MAX_HANDLES - 1;
    handles[slot].acb = acb;
    find_acb_name(acb, handles[slot].name);
    return handles[slot].name;
}

static int is_muted(const char *bank, int id)
{
    for (int i = 0; i < bank_count; i++)
        if (str_eq(banks[i].name, bank))
            for (int j = 0; j < banks[i].count; j++)
                if (banks[i].muted[j] == id)
                    return 1;
    return 0;
}

static char *put_hex(char *p, ULONG_PTR v, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4)
        *p++ = hex[(v >> shift) & 15];
    return p;
}

static char *put_str(char *p, const char *s)
{
    while (*s) *p++ = *s++;
    return p;
}

static char *put_dec(char *p, unsigned v)
{
    char tmp[12];
    int n = 0;
    do tmp[n++] = (char)('0' + v % 10); while (v /= 10);
    while (n) *p++ = tmp[--n];
    return p;
}

static void log_cue(const char *bank, int id, void *caller, int skipped)
{
    char line[128], *p = line;
    DWORD written;
    p = put_dec(p, GetTickCount());
    *p++ = ' ';
    p = put_str(p, bank[0] ? bank : "?");
    *p++ = ' ';
    p = put_hex(p, (ULONG_PTR)id, 4);
    p = put_str(p, " from ");
    p = put_hex(p, (ULONG_PTR)((BYTE *)caller - exe_base), 8);
    if (skipped)
        p = put_str(p, " muted");
    *p++ = '\r';
    *p++ = '\n';
    WriteFile(sound_log, line, (DWORD)(p - line), &written, NULL);
}

static void set_cue_id_hook(void *player, void *acb, int id)
{
    void *caller = __builtin_return_address(0);
    EnterCriticalSection(&lock);
    const char *bank = acb ? name_of(acb) : "";
    int skip = is_muted(bank, id);
    if (sound_log != INVALID_HANDLE_VALUE)
        log_cue(bank, id, caller, skip);
    LeaveCriticalSection(&lock);
    original_set_cue_id(player, acb, skip ? SILENT_CUE_ID : id);
}

// Parses "name=hex,hex;comment" lines from the [Mute] section.
static void load_mutes(const WCHAR *ini)
{
    static WCHAR section[2048];
    DWORD len = GetPrivateProfileSectionW(L"Mute", section, 2048, ini);
    for (const WCHAR *s = section; len && *s; s += lstrlenW(s) + 1) {
        if (bank_count == MAX_BANKS)
            break;
        struct bank *b = &banks[bank_count];
        int n = 0;
        while (*s && *s != L'=' && n < 31)
            b->name[n++] = (char)*s++;
        b->name[n] = 0;
        if (*s != L'=' || !n)
            continue;
        unsigned v = 0;
        int digits = 0;
        for (s++;; s++) {
            WCHAR ch = *s;
            int d = ch >= L'0' && ch <= L'9' ? ch - L'0'
                  : ch >= L'a' && ch <= L'f' ? ch - L'a' + 10
                  : ch >= L'A' && ch <= L'F' ? ch - L'A' + 10 : -1;
            if (d >= 0) {
                v = v * 16 + (unsigned)d;
                digits++;
                continue;
            }
            if (digits && b->count < MAX_MUTED)
                b->muted[b->count++] = (WORD)v;
            v = 0;
            digits = 0;
            if (ch == 0 || ch == L';')
                break;
        }
        s += lstrlenW(s); // the outer loop steps past this line's terminator
        if (b->count)
            bank_count++;
    }
}

// ---------------------------------------------------------------------------
// Movie audio mute (TG4AC-Win64-Shipping.exe 5.80.02 only).
//
// The virtual arcade around the reprint routes is a set of CRI Mana videos
// (Content\Movies\AC1_Left*.usm, AC1_Right*.usm) with the other machines' sounds
// recorded into their audio tracks. Mana sets that audio's volume in exactly two
// places, both calling criAtomPlayer_SetVolume. We point those two calls at a stub
// that forces the volume to 0. Together with the room loop cue this is:
//   [Arcade]
//   MuteAmbience=1
// ---------------------------------------------------------------------------

#define RVA_ATOM_PLAYER_SET_VOLUME 0x1e3ef9c
static const DWORD MANA_SET_VOLUME_CALLS[] = {0x1ebbc14, 0x1ebc1b5};

// Executable memory within +-2 GB of the exe, so rel32 calls can reach it.
static BYTE *alloc_near(BYTE *base)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    ULONG_PTR gran = si.dwAllocationGranularity;
    for (ULONG_PTR dist = 0x10000000; dist < 0x70000000; dist += gran) {
        BYTE *want = (BYTE *)(((ULONG_PTR)base - dist) & ~(gran - 1));
        BYTE *got = VirtualAlloc(want, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (got)
            return got;
    }
    return NULL;
}

static void install_movie_mute(BYTE *base)
{
    BYTE *target = base + RVA_ATOM_PLAYER_SET_VOLUME;
    for (int i = 0; i < 2; i++) {
        BYTE *site = base + MANA_SET_VOLUME_CALLS[i];
        if (site[0] != 0xE8 || site + 5 + *(INT32 *)(site + 1) != target)
            return; // different game version: leave movies alone
    }
    BYTE *stub = alloc_near(base);
    if (!stub)
        return;
    // xorps xmm1,xmm1 ; jmp [rip+0] ; dq criAtomPlayer_SetVolume
    BYTE code[17] = {0x0F, 0x57, 0xC9, 0xFF, 0x25, 0, 0, 0, 0};
    for (int i = 0; i < 8; i++)
        code[9 + i] = (BYTE)((ULONG_PTR)target >> (8 * i));
    for (int i = 0; i < (int)sizeof code; i++)
        stub[i] = code[i];
    for (int i = 0; i < 2; i++) {
        BYTE *site = base + MANA_SET_VOLUME_CALLS[i];
        INT32 rel = (INT32)(stub - (site + 5));
        DWORD old;
        if (!VirtualProtect(site + 1, 4, PAGE_EXECUTE_READWRITE, &old))
            continue;
        *(INT32 *)(site + 1) = rel;
        VirtualProtect(site + 1, 4, old, &old);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
}

static void add_mute(const char *name, WORD id)
{
    struct bank *b = NULL;
    for (int i = 0; i < bank_count; i++)
        if (str_eq(banks[i].name, name))
            b = &banks[i];
    if (!b) {
        if (bank_count == MAX_BANKS)
            return;
        b = &banks[bank_count++];
        int n = 0;
        while (name[n] && n < 31) b->name[n] = name[n], n++;
        b->name[n] = 0;
        b->count = 0;
    }
    if (b->count < MAX_MUTED)
        b->muted[b->count++] = id;
}

static void install_cue_filter(const WCHAR *dir, const WCHAR *ini, int mute_ambience)
{
    load_mutes(ini);
    if (mute_ambience)
        add_mute("legend", ARCADE_ROOM_CUE);
    int logging = GetPrivateProfileIntW(L"Debug", L"LogSounds", 0, ini);
    if (!logging && !bank_count)
        return;

    exe_base = (BYTE *)GetModuleHandleW(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(exe_base + ((IMAGE_DOS_HEADER *)exe_base)->e_lfanew);
    if (nt->OptionalHeader.SizeOfImage < RVA_SET_CUE_ID + sizeof SET_CUE_ID_BYTES)
        return; // not the game exe
    BYTE *target = exe_base + RVA_SET_CUE_ID;
    for (int i = 0; i < (int)sizeof SET_CUE_ID_BYTES; i++)
        if (target[i] != SET_CUE_ID_BYTES[i])
            return; // different game version: leave sounds alone

    // Trampoline: the original first instructions, then a jump back past them.
    BYTE *tramp = VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp)
        return;
    for (int i = 0; i < PROLOGUE_LEN; i++)
        tramp[i] = target[i];
    BYTE *back = tramp + PROLOGUE_LEN;
    ULONG_PTR resume = (ULONG_PTR)(target + PROLOGUE_LEN);
    back[0] = 0xFF; back[1] = 0x25; back[2] = back[3] = back[4] = back[5] = 0; // jmp [rip+0]
    for (int i = 0; i < 8; i++)
        back[6 + i] = (BYTE)(resume >> (8 * i));
    original_set_cue_id = (set_cue_id_fn)tramp;

    InitializeCriticalSection(&lock);
    if (logging) {
        WCHAR path[MAX_PATH];
        lstrcpyW(path, dir);
        lstrcatW(path, L"ArrowheadUSBIO_sounds.log");
        sound_log = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, NULL);
    }

    BYTE patch[14] = {0xFF, 0x25, 0, 0, 0, 0};
    ULONG_PTR hook = (ULONG_PTR)&set_cue_id_hook;
    for (int i = 0; i < 8; i++)
        patch[6 + i] = (BYTE)(hook >> (8 * i));
    DWORD old;
    if (!VirtualProtect(target, sizeof patch, PAGE_EXECUTE_READWRITE, &old))
        return;
    for (int i = 0; i < (int)sizeof patch; i++)
        target[i] = patch[i];
    VirtualProtect(target, sizeof patch, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, sizeof patch);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        WCHAR dir[MAX_PATH], ini[MAX_PATH];
        DWORD n = GetModuleFileNameW(inst, dir, MAX_PATH);
        while (n > 0 && dir[n - 1] != L'\\')
            n--;
        dir[n] = 0;
        lstrcpyW(ini, dir);
        lstrcatW(ini, L"ArrowheadUSBIO_volume.ini");
        load_original(dir, ini);
        int mute_ambience = GetPrivateProfileIntW(L"Arcade", L"MuteAmbience", 0, ini);
        install_cue_filter(dir, ini, mute_ambience);
        if (mute_ambience) {
            BYTE *base = (BYTE *)GetModuleHandleW(NULL);
            IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
            if (nt->OptionalHeader.SizeOfImage > MANA_SET_VOLUME_CALLS[1] + 5)
                install_movie_mute(base);
        }
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
