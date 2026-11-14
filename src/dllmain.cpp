// re5_char_tweaks.asi — per-costume (class) tweaks for RE5 (PC).
//
// Config:
//   <dir>\re5_char_tweaks.ini              master config, one section per class
//   <dir>\re5_char_overrides\<class>.ini   per-mod overrides, e.g.
//                                          uPl00ChrisCos3.ini with plain keys
// where <dir> is the folder of this .asi (scripts\), falling back to the game
// folder.
//
// Sections are the exact class names (costumes), plus [default]:
//   [uPl00ChrisCos3]
//   back=0
//   custom_bone=1
//   bone=4
// A class section overrides [default] key by key.
//
// Keys:
//   back        -1 = vanilla, 0 = never mount, 1 = force mount
//   custom_bone  0/1
//   bone         0..127 (anim joint No)
//   chn          0 off, 1 .chn, 2 .ccl, 3 both
//
// The decision site is located at runtime by signature (the retail exe is
// protected by Steam + Enigma and the code is shifted vs. the 2018 build):
//   mov eax,[eax+0x20] / and eax,0xFFFF00 / cmp eax,0x500 / setne al / pop esi / ret
// The 5-byte tail is replaced by a jump into a stub that resolves the config
// for the current class and returns the desired AL.
//
// Build (TDM-GCC, 32-bit):
//   g++ -m32 -shared -O2 -static-libgcc -static-libstdc++ -o re5_char_tweaks.asi dllmain.cpp

#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------- config
struct Section {
    char name[64];
    int back, custom, bone, chn;
    unsigned char has_back, has_custom, has_bone, has_chn;
};

struct Cfg { int back, custom, bone, chn; };

static Section g_sections[128];
static int     g_section_count = 0;

static Section* find_section(const char* name)
{
    for (int i = 0; i < g_section_count; ++i)
        if (_stricmp(g_sections[i].name, name) == 0) return &g_sections[i];
    return nullptr;
}

static Section* ensure_section(const char* name)
{
    Section* s = find_section(name);
    if (s) return s;
    if (g_section_count >= (int)(sizeof(g_sections) / sizeof(g_sections[0]))) return nullptr;
    s = &g_sections[g_section_count++];
    memset(s, 0, sizeof(*s));
    strncpy_s(s->name, sizeof(s->name), name, _TRUNCATE);
    return s;
}

static void trim(char* s)
{
    char* p = s;
    while (*p == ' ' || *p == '\t') ++p;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = 0;
}

// default_section: used for keys that appear before the first [section] line.
static void parse_ini(const char* path, const char* default_section)
{
    FILE* f = nullptr;
    if (fopen_s(&f, path, "r") != 0 || !f) return;

    char current[64];
    strncpy_s(current, sizeof(current), default_section, _TRUNCATE);
    ensure_section(current);

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (!line[0] || line[0] == ';' || line[0] == '#') continue;

        if (line[0] == '[') {
            char* end = strchr(line, ']');
            if (!end) continue;
            *end = 0;
            strncpy_s(current, sizeof(current), line + 1, _TRUNCATE);
            trim(current);
            ensure_section(current);
            continue;
        }

        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char* key = line;
        char* val = eq + 1;
        trim(key);
        trim(val);

        Section* s = find_section(current);
        if (!s) continue;

        if (_stricmp(key, "back") == 0) { s->back = atoi(val); s->has_back = 1; }
        else if (_stricmp(key, "custom_bone") == 0) { s->custom = atoi(val); s->has_custom = 1; }
        else if (_stricmp(key, "bone") == 0) { s->bone = atoi(val); s->has_bone = 1; }
        else if (_stricmp(key, "chn") == 0) { s->chn = atoi(val); s->has_chn = 1; }
    }
    fclose(f);
}

static Cfg resolve_cfg(const char* cls)
{
    Section* a = (cls && *cls) ? find_section(cls) : nullptr;
    Section* d = find_section("default");

    Cfg r;
    r.back   = -1;
    r.custom = 0;
    r.bone   = 0;
    r.chn    = 0;

    if (a && a->has_back) r.back = a->back; else if (d && d->has_back) r.back = d->back;
    if (a && a->has_custom) r.custom = a->custom; else if (d && d->has_custom) r.custom = d->custom;
    if (a && a->has_bone) r.bone = a->bone; else if (d && d->has_bone) r.bone = d->bone;
    if (a && a->has_chn) r.chn = a->chn; else if (d && d->has_chn) r.chn = d->chn;

    if (r.back < -1) r.back = -1;
    if (r.bone < 0 || r.bone > 127) r.bone = 0;
    return r;
}

// ---------------------------------------------------------------- logging
static void log_line(const char* fmt, ...)
{
    char path[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("TEMP", path, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    strcat_s(path, MAX_PATH, "\\re5_char_tweaks.log");

    FILE* f = nullptr;
    if (fopen_s(&f, path, "a") != 0 || !f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

// ---------------------------------------------------------------- globals
static BYTE* g_code = nullptr;
static BYTE* g_stub = nullptr;
static BYTE  g_result = 0;
static BYTE  g_bone_female = 0x02;
static BYTE  g_bone_male = 0x75;

static const DWORD VA_BONE_FEM  = 0xC4D8B6;
static const DWORD VA_BONE_MALE = 0xC4D887;
static const DWORD VA_CODE_PAGE = 0xC4D000;

// Class resource tables (vtable slot +0xA0) with the class (costume) name.
struct ClassTable { DWORD va; const char* cls; const char* label; };
static const ClassTable kClassTables[] = {
    { 0x1019C30, "uPl00ChrisNormal", "Chris BSAA (no vest)" },
    { 0x1019CF0, "uPl00ChrisArmorA", "Chris BSAA + melee vest" },
    { 0x1019DB0, "uPl00ChrisArmorB", "Chris BSAA + bullet vest" },
    { 0x1019E70, "uPl00ChrisArmorF", "Chris BSAA + melee + bullet vests" },
    { 0x1019F30, "uPl00ChrisPlain", "Chris jacket (start of the game)" },
    { 0x1019FF0, "uPl00ChrisCos1", "Chris Safari" },
    { 0x101A0B0, "uPl00ChrisCos2", "Chris S.T.A.R.S." },
    { 0x101A170, "uPl00ChrisCos3", "Chris Warrior" },
    { 0x101A308, "uPl00ChrisCos4", "Chris Heavy Metal" },
    { 0x101A248, "uPl00ChrisEp1", "Chris Lost in Nightmares" },
    { 0x101A3E0, "uPl01ShebaNormal", "Sheva BSAA (no vest)" },
    { 0x101A4A0, "uPl01ShebaArmorA", "Sheva BSAA + melee vest" },
    { 0x101A560, "uPl01ShebaArmorB", "Sheva BSAA + bullet vest" },
    { 0x101A620, "uPl01ShebaArmorF", "Sheva BSAA + melee + bullet vests" },
    { 0x101A6E0, "uPl01ShebaPlain", "Sheva basic gear (start of the game)" },
    { 0x101A7A0, "uPl01ShebaCos1", "Sheva Tribal" },
    { 0x101A860, "uPl01ShebaCos2", "Sheva Clubbin'" },
    { 0x101A920, "uPl01ShebaCos3", "Sheva Business" },
    { 0x101A9F8, "uPl01ShebaCos4", "Sheva Fairy Tale" },
    { 0x101AAD0, "uPl02JillCos1", "Jill BSAA" },
    { 0x101AB90, "uPl02JillCos2", "Jill Battle Suit" },
    { 0x101AC50, "uPl02JillCos3", "Jill Desperate Escape" },
    { 0x101AD10, "uPl02JillCos4", "Jill Lost in Nightmares" },
    { 0x101ADD0, "uPl03WeskerCos1", "Wesker Midnight" },
    { 0x101AE90, "uPl03WeskerCos2", "Wesker S.T.A.R.S." },
    { 0x101B010, "uPl04JoshCos1", "Josh Mercenaries Reunion (unused Chris LiN)" },
    { 0x101B0D0, "uPl04JoshCos2", "Josh Desperate Escape" },
    { 0x101B190, "uPl05ExcellaNormal", "Excella Mercenaries Reunion" },
    { 0x101B268, "uPl06BarryNormal", "Barry Mercenaries Reunion" },
    { 0x101B328, "uPl07RebeccaNormal", "Rebecca Mercenaries Reunion" },
};


// ---------------------------------------------------------------- safe code writes
// Patching live code has to be atomic w.r.t. other threads: suspend them while
// the bytes are written, otherwise a thread may execute a half-written jump.
struct SuspendedThreads { HANDLE h[256]; int n; };

static void suspend_other_threads(SuspendedThreads& st)
{
    st.n = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;

    THREADENTRY32 te;
    te.dwSize = sizeof(te);
    const DWORD pid = GetCurrentProcessId();
    const DWORD self = GetCurrentThreadId();

    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
            if (st.n >= (int)(sizeof(st.h) / sizeof(st.h[0]))) break;
            HANDLE h = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
            if (!h) continue;
            if (SuspendThread(h) != (DWORD)-1) st.h[st.n++] = h;
            else CloseHandle(h);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
}

static void resume_threads(SuspendedThreads& st)
{
    for (int i = 0; i < st.n; ++i) {
        ResumeThread(st.h[i]);
        CloseHandle(st.h[i]);
    }
    st.n = 0;
}

// ---------------------------------------------------------------- chain slots
static bool looks_like_model_path(DWORD ptr)
{
    if (ptr < 0x400000 || ptr > 0x20000000) return false;
    const char* s = (const char*)ptr;
    return s[0] == 'p' && s[1] == 'a' && s[2] == 'w' && s[3] == 'n';
}

static int apply_chain_slots()
{
    if (!g_code) return 0;
    int written = 0;

    for (const ClassTable& ct : kClassTables) {
        Cfg cfg = resolve_cfg(ct.cls);
        if (cfg.chn <= 0) continue;

        DWORD* table = (DWORD*)(g_code + (ct.va - 0x400000));
        DWORD model = table[0];
        if (!looks_like_model_path(model)) continue;

        DWORD* chn = table + (0x88 / 4);
        DWORD* ccl = table + (0x90 / 4);

        bool want_chn = (cfg.chn & 1) && (*chn == 0);
        bool want_ccl = (cfg.chn & 2) && (*ccl == 0);
        if (!want_chn && !want_ccl) continue;

        DWORD old = 0;
        if (!VirtualProtect(chn, 8, PAGE_READWRITE, &old)) continue;
        if (want_chn) *chn = model;
        if (want_ccl) *ccl = model;
        VirtualProtect(chn, 8, old, &old);

        log_line("chain slots: %s table=%p model=%p chn=%s ccl=%s",
                 ct.cls, (void*)table, (void*)model,
                 want_chn ? "on" : "skip", want_ccl ? "on" : "skip");
        written += (want_chn ? 1 : 0) + (want_ccl ? 1 : 0);
    }
    return written;
}

// ---------------------------------------------------------------- class of player
static const char* class_of_player(void* player)
{
    if (!player || (uintptr_t)player < 0x10000) return "";

    void** vt = *(void***)player;
    if (!vt || (uintptr_t)vt < 0x400000 || (uintptr_t)vt > 0x20000000) return "";

    typedef void* (__attribute__((thiscall)) *GetTable)(void*);
    GetTable get = (GetTable)vt[0xA0 / 4];
    if (get && (uintptr_t)get > 0x400000) {
        DWORD* table = (DWORD*)get(player);
        if (table) {
            for (const ClassTable& ct : kClassTables)
                if (table == (DWORD*)(g_code + (ct.va - 0x400000)))
                    return ct.cls;
        }
    }

    // Fallback: class name from the DTI (vtable slot 4).
    typedef void* (__attribute__((thiscall)) *GetDTI)(void*);
    GetDTI get_dti = (GetDTI)vt[4];
    if (get_dti && (uintptr_t)get_dti > 0x400000) {
        void* dti = get_dti(player);
        if (dti && (uintptr_t)dti > 0x400000 && (uintptr_t)dti < 0x20000000) {
            const char* nm = *(const char**)((char*)dti + 4);
            if (nm && (uintptr_t)nm > 0x400000 && nm[0] == 'u' && nm[1] == 'P')
                return nm;
        }
    }
    return "";
}

// ---------------------------------------------------------------- bone bytes
static void write_bone_bytes(int bone)
{
    BYTE b = (BYTE)bone;
    if (g_bone_female == b && g_bone_male == b) return;

    DWORD old = 0;
    if (!VirtualProtect(g_code + (VA_CODE_PAGE - 0x400000), 0x1000,
                        PAGE_EXECUTE_READWRITE, &old))
        return;

    SuspendedThreads st;
    suspend_other_threads(st);
    if (g_bone_female != b) { *(BYTE*)(g_code + (VA_BONE_FEM - 0x400000)) = b; g_bone_female = b; }
    if (g_bone_male != b)   { *(BYTE*)(g_code + (VA_BONE_MALE - 0x400000)) = b; g_bone_male = b; }
    FlushInstructionCache(GetCurrentProcess(), g_code + (VA_CODE_PAGE - 0x400000), 0x1000);
    resume_threads(st);

    VirtualProtect(g_code + (VA_CODE_PAGE - 0x400000), 0x1000, old, &old);
}

static void reset_bone_bytes()
{
    if (g_bone_female == 0x02 && g_bone_male == 0x75) return;

    DWORD old = 0;
    if (!VirtualProtect(g_code + (VA_CODE_PAGE - 0x400000), 0x1000,
                        PAGE_EXECUTE_READWRITE, &old))
        return;

    SuspendedThreads st;
    suspend_other_threads(st);
    if (g_bone_female != 0x02) { *(BYTE*)(g_code + (VA_BONE_FEM - 0x400000)) = 0x02; g_bone_female = 0x02; }
    if (g_bone_male != 0x75)   { *(BYTE*)(g_code + (VA_BONE_MALE - 0x400000)) = 0x75; g_bone_male = 0x75; }
    FlushInstructionCache(GetCurrentProcess(), g_code + (VA_CODE_PAGE - 0x400000), 0x1000);
    resume_threads(st);

    VirtualProtect(g_code + (VA_CODE_PAGE - 0x400000), 0x1000, old, &old);
}

// ---------------------------------------------------------------- decision
extern "C" unsigned char decide_c(void* player, unsigned type)
{
    const char* cls = class_of_player(player);
    Cfg cfg = resolve_cfg(cls);

    if (cfg.custom && cfg.bone >= 0 && cfg.bone <= 127) write_bone_bytes(cfg.bone);
    else reset_bone_bytes();

    static char last[64] = { 0 };
    if (strcmp(last, cls) != 0) {
        strncpy_s(last, sizeof(last), cls[0] ? cls : "?", _TRUNCATE);
        log_line("decide: class=%s type=%05x back=%d custom=%d bone=%d chn=%d",
                 cls[0] ? cls : "?", type, cfg.back, cfg.custom, cfg.bone, cfg.chn);
    }

    if (cfg.back >= 0) return (unsigned char)(cfg.back ? 1 : 0);
    return (unsigned char)(type != 0x500);
}

static BYTE* build_stub()
{
    BYTE* s = (BYTE*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!s) return nullptr;
    int i = 0;
    s[i++] = 0x60;                                   // pushad
    s[i++] = 0x50;                                   // push eax (type)
    s[i++] = 0x56;                                   // push esi (player)
    s[i++] = 0xE8;                                   // call decide_c
    *(DWORD*)(s + i) = (DWORD)&decide_c - ((DWORD)s + i + 4); i += 4;
    s[i++] = 0x83; s[i++] = 0xC4; s[i++] = 0x08;     // add esp, 8
    s[i++] = 0xA2; *(DWORD*)(s + i) = (DWORD)&g_result; i += 4;
    s[i++] = 0x61;                                   // popad
    s[i++] = 0xA0; *(DWORD*)(s + i) = (DWORD)&g_result; i += 4;
    s[i++] = 0x5E;                                   // pop esi
    s[i++] = 0xC3;                                   // ret
    return s;
}

// ---------------------------------------------------------------- scan
static const BYTE kSig[18] = {
    0x8B, 0x40, 0x20,
    0x25, 0x00, 0xFF, 0xFF, 0x00,
    0x3D, 0x00, 0x05, 0x00, 0x00,
    0x0F, 0x95, 0xC0,
    0x5E, 0xC3
};

static BYTE* scan_for_site()
{
    HANDLE self = GetCurrentProcess();
    BYTE* addr = (BYTE*)0x10000;

    while ((uintptr_t)addr < 0x7FFF0000) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(addr, &mbi, sizeof(mbi)) != sizeof(mbi)) break;
        BYTE* base = (BYTE*)mbi.BaseAddress;
        SIZE_T size = mbi.RegionSize;

        bool exec = (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                                    PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
        bool guard = (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0;

        if (mbi.State == MEM_COMMIT && exec && !guard) {
            const SIZE_T CHUNK = 64 * 1024;
            for (SIZE_T off = 0; off < size; off += CHUNK) {
                SIZE_T to_read = (size - off < CHUNK) ? (size - off) : CHUNK;
                PBYTE buf = (PBYTE)malloc(to_read);
                if (!buf) break;
                SIZE_T got = 0;
                if (ReadProcessMemory(self, base + off, buf, to_read, &got) &&
                    got >= sizeof(kSig)) {
                    for (SIZE_T i = 0; i + sizeof(kSig) <= got; ++i) {
                        if (memcmp(buf + i, kSig, sizeof(kSig)) == 0) {
                            free(buf);
                            return base + off + i;
                        }
                    }
                }
                free(buf);
            }
        }
        addr = base + size;
    }
    return nullptr;
}

static bool patch_site(BYTE* site)
{
    BYTE* tail = site + 13;
    if (memcmp(tail, kSig + 13, 5) != 0) return false;

    DWORD old = 0;
    if (!VirtualProtect(tail, 5, PAGE_EXECUTE_READWRITE, &old)) return false;

    BYTE code[5];
    code[0] = 0xE9;
    *(DWORD*)(code + 1) = (DWORD)g_stub - ((DWORD)tail + 5);

    SuspendedThreads st;
    suspend_other_threads(st);
    memcpy(tail, code, 5);
    FlushInstructionCache(GetCurrentProcess(), tail, 5);
    resume_threads(st);

    VirtualProtect(tail, 5, old, &old);
    return true;
}

// ---------------------------------------------------------------- paths / files
static void dir_of_module(HMODULE mod, char* out, size_t out_size)
{
    char path[MAX_PATH] = { 0 };
    if (mod && GetModuleFileNameA(mod, path, MAX_PATH)) {
        strncpy_s(out, out_size, path, _TRUNCATE);
        char* slash = strrchr(out, '\\');
        if (slash) *(slash + 1) = 0;
    } else {
        out[0] = 0;
    }
}

static int cmp_names(const void* a, const void* b)
{
    return _stricmp((const char*)a, (const char*)b);
}

static void load_override_folder(const char* dir)
{
    char folder[MAX_PATH];
    strncpy_s(folder, sizeof(folder), dir, _TRUNCATE);
    strcat_s(folder, sizeof(folder), "re5_char_overrides");
    CreateDirectoryA(folder, nullptr);

    char mask[MAX_PATH];
    strncpy_s(mask, sizeof(mask), folder, _TRUNCATE);
    strcat_s(mask, sizeof(mask), "\\*.ini");

    static char names[64][MAX_PATH];
    int n = 0;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(mask, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (n >= 64) break;
            strncpy_s(names[n], MAX_PATH, folder, _TRUNCATE);
            strcat_s(names[n], MAX_PATH, "\\");
            strcat_s(names[n], MAX_PATH, fd.cFileName);
            ++n;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    if (n > 1) qsort(names, n, sizeof(names[0]), cmp_names);
    for (int i = 0; i < n; ++i) {
        // File name (without .ini) is the class name: uPl00ChrisCos3.ini
        char base[64] = { 0 };
        const char* fn = strrchr(names[i], '\\');
        fn = fn ? fn + 1 : names[i];
        strncpy_s(base, sizeof(base), fn, _TRUNCATE);
        char* dot = strrchr(base, '.');
        if (dot) *dot = 0;

        parse_ini(names[i], base);
        log_line("override: %s -> [%s]", names[i], base);
    }
}

static void write_default_ini(const char* dir)
{
    char path[MAX_PATH];
    strncpy_s(path, sizeof(path), dir, _TRUNCATE);
    strcat_s(path, sizeof(path), "re5_char_tweaks.ini");
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) return;

    FILE* f = nullptr;
    if (fopen_s(&f, path, "w") != 0 || !f) return;
    fputs(
        "; RE5 per-costume tweaks\n"
        "; Sections are exact class names; keys not set here fall back to [default].\n"
        "; Overrides: re5_char_overrides\\<class>.ini (e.g. uPl00ChrisCos3.ini)\n"
        ";\n"
        "; back: -1 vanilla / 0 never / 1 force\n"
        "; custom_bone: 0/1, bone: 0..127\n"
        "; chn: 0 off / 1 .chn / 2 .ccl / 3 both\n",
        f);
    fputs("\n[default]\nback=-1\ncustom_bone=0\nbone=0\nchn=0\n\n", f);
    for (const ClassTable& ct : kClassTables) {
        int back = (strcmp(ct.cls, "uPl05ExcellaNormal") == 0) ? 1 : -1;
        fprintf(f, "; %s\n[%s]\nback=%d\ncustom_bone=0\nbone=0\nchn=0\n\n",
                ct.label, ct.cls, back);
    }
    fclose(f);
}

static DWORD WINAPI worker(LPVOID)
{
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&worker, &self);

    char dir[MAX_PATH] = { 0 };
    dir_of_module(self, dir, sizeof(dir));
    char exe_dir[MAX_PATH] = { 0 };
    dir_of_module(nullptr, exe_dir, sizeof(exe_dir));
    if (!dir[0]) strncpy_s(dir, sizeof(dir), exe_dir, _TRUNCATE);

    g_code = (BYTE*)GetModuleHandleA(nullptr);
    if (!g_code) return 0;

    char master[MAX_PATH];
    strncpy_s(master, sizeof(master), dir, _TRUNCATE);
    strcat_s(master, sizeof(master), "re5_char_tweaks.ini");
    if (GetFileAttributesA(master) == INVALID_FILE_ATTRIBUTES && exe_dir[0]) {
        char alt[MAX_PATH];
        strncpy_s(alt, sizeof(alt), exe_dir, _TRUNCATE);
        strcat_s(alt, sizeof(alt), "re5_char_tweaks.ini");
        if (GetFileAttributesA(alt) != INVALID_FILE_ATTRIBUTES)
            strncpy_s(master, sizeof(master), alt, _TRUNCATE);
    }
    if (GetFileAttributesA(master) == INVALID_FILE_ATTRIBUTES)
        write_default_ini(dir);

    parse_ini(master, "default");
    log_line("start: module=%p ini=%s", (void*)g_code, master);

    load_override_folder(dir);
    if (exe_dir[0] && _stricmp(exe_dir, dir) != 0)
        load_override_folder(exe_dir);

    g_stub = build_stub();
    if (!g_stub) { log_line("no stub memory"); return 0; }

    bool patched = false;
    for (int i = 0; i < 1800; ++i) {
        if (!patched) {
            BYTE* site = scan_for_site();
            if (site) {
                log_line("site found at %p", (void*)site);
                if (patch_site(site)) {
                    patched = true;
                    log_line("site patched at %p", (void*)(site + 13));
                } else {
                    log_line("tail mismatch at %p", (void*)site);
                }
            }
        }
        apply_chain_slots();
        if (patched && i > 30) break;
        Sleep(1000);
    }
    log_line("done (patched=%d)", patched ? 1 : 0);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);
        HANDLE h = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
    }
    return TRUE;
}
