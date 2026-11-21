// re5_char_tweaks.asi — per-costume (class) tweaks for RE5 (PC).
//
// Config files, all in the folder of this .asi (scripts\), falling back to the
// game folder. Names are lowercase snake_case, one prefix per mod:
//   re5_ct_settings.ini     global switches (file overrides, delays, dumps)
//   re5_ct_characters.ini   one section per class (costume)
//   re5_ct_weapons.ini      one section per weapon (mount offsets)
//   re5_char_overrides\     per-mod class overrides, <class>.ini with plain keys
//   re5_weapon_overrides\   per-mod weapon overrides, <WPxx>.ini
//   re5_file_overrides\     loose file overrides, <archive-internal path>.<ext>
//
// Sections in re5_ct_characters.ini are the exact class names (costumes), plus
// [default]:
//   [uPl00ChrisCos3]
//   back=0
//   custom_bone=1
//   bone=4
// A class section overrides [default] key by key.
//
// A [WPxx] section inside a class section overrides re5_ct_weapons.ini for that
// costume only; keys not set fall back to a [WPxx] under [default], then to the
// value already in the game's per-weapon table:
//   [uPl00ChrisCos3]
//   [WP0F]
//   pos2=-5,7,-13
//
// Keys:
//   back        -1 = vanilla, 0 = never mount, 1 = force mount
//   custom_bone  0/1
//   bone         0..255 (anim joint No)
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
#include <stdint.h>
#include <ctype.h>

static bool looks_like_model_path(DWORD ptr);
static BYTE* scan_for_bytes(const BYTE* sig, size_t len);
static int  weapon_id_from_name(const char* s);
static void set_char_weapon_key(const char* cls, int id, const char* key, const char* val);
static void add_extra_exts(const char* list);

// ---------------------------------------------------------------- config
// Weapon mount offsets: position/rotation for the joint-117 branch and for the
// spine branch, the same four vec3s as re5_ct_weapons.ini.
struct WeaponCfg {
    float pos117[3], rot117[3], pos2[3], rot2[3];
    unsigned char has_pos117, has_rot117, has_pos2, has_rot2;
};

struct Section {
    char name[64];
    int back, custom, bone, chn;
    unsigned char has_back, has_custom, has_bone, has_chn;
};

struct Cfg { int back, custom, bone, chn; };

static int g_dump_weapons = 0;
static int g_file_overrides_on = 1;
static int g_override_whitelist = 1;   // 1 = index only resource types the game loads
static int g_delay_ms = 10000;

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

// ---------------------------------------------------------------- config files
// All lowercase snake_case, one prefix per mod.
static const char kCfgSettings[] = "re5_ct_settings.ini";
static const char kCfgCharacters[] = "re5_ct_characters.ini";
static const char kCfgWeapons[] = "re5_ct_weapons.ini";

static bool file_exists(const char* path)
{
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

static void path_join(char* dst, size_t dst_size, const char* dir, const char* name)
{
    strncpy_s(dst, dst_size, dir, _TRUNCATE);
    strcat_s(dst, dst_size, name);
}

// Looks for <dir>\<name>, then the same under exe_dir.
static bool find_config(char* out, size_t out_size, const char* dir,
                        const char* exe_dir, const char* name)
{
    const char* dirs[2] = { dir, exe_dir };

    for (int d = 0; d < 2; ++d) {
        if (!dirs[d] || !*dirs[d]) continue;
        if (d == 1 && _stricmp(dirs[0], dirs[1]) == 0) continue;
        char p[MAX_PATH];
        path_join(p, sizeof(p), dirs[d], name);
        if (file_exists(p)) {
            strncpy_s(out, out_size, p, _TRUNCATE);
            return true;
        }
    }
    return false;
}

// default_section: used for keys that appear before the first [section] line.
//
// A [WPxx] section inside this file belongs to the class section before it (or
// to default_section at the top of the file), so a costume can carry its own
// weapon mount offsets next to its back/bone keys:
//
//   [uPl00ChrisCos3]
//   back=0
//   [WP0F]
//   pos2=-5,7,-13
static void parse_ini(const char* path, const char* default_section)
{
    FILE* f = nullptr;
    if (fopen_s(&f, path, "r") != 0 || !f) return;

    char current[64];
    char owner[64];          // class that owns subsequent [WPxx] sections
    strncpy_s(current, sizeof(current), default_section, _TRUNCATE);
    strncpy_s(owner, sizeof(owner), default_section, _TRUNCATE);
    ensure_section(current);

    int wp_id = -1;          // >= 0 while inside a [WPxx] subsection

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
            int id = weapon_id_from_name(current);
            if (id >= 0) {
                wp_id = id;              // stays inside the current class
            } else {
                wp_id = -1;
                strncpy_s(owner, sizeof(owner), current, _TRUNCATE);
                ensure_section(current);
            }
            continue;
        }

        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char* key = line;
        char* val = eq + 1;
        trim(key);
        trim(val);

        if (wp_id >= 0) {
            set_char_weapon_key(owner, wp_id, key, val);
            continue;
        }

        Section* s = find_section(current);
        if (!s) continue;

        if (_stricmp(key, "dump_weapons") == 0) { g_dump_weapons = atoi(val); }
        else if (_stricmp(key, "file_overrides") == 0) { g_file_overrides_on = atoi(val); }
        else if (_stricmp(key, "file_override_whitelist") == 0) { g_override_whitelist = atoi(val); }
        else if (_stricmp(key, "file_override_exts") == 0) { add_extra_exts(val); }
        else if (_stricmp(key, "delay_ms") == 0) { g_delay_ms = atoi(val); }
        else if (_stricmp(key, "back") == 0) { s->back = atoi(val); s->has_back = 1; }
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
    if (r.bone < 0 || r.bone > 255) r.bone = 0;
    return r;
}

// ---------------------------------------------------------------- weapon offsets
static WeaponCfg g_weapons[256];

static int weapon_id_from_name(const char* s)
{
    while (*s == ' ' || *s == '\t') ++s;
    if ((s[0] == 'W' || s[0] == 'w') && (s[1] == 'P' || s[1] == 'p')) s += 2;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    char* end = nullptr;
    long v = strtol(s, &end, 16);
    if (end == s || v < 0 || v > 255) return -1;
    return (int)v;
}

static int parse_vec3(const char* v, float out[3])
{
    float a, b, c;
    if (sscanf(v, " %f , %f , %f", &a, &b, &c) == 3 ||
        sscanf(v, " %f %f %f", &a, &b, &c) == 3) {
        out[0] = a; out[1] = b; out[2] = c;
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------- per-class weapon offsets
// [WPxx] subsections inside a class section. Values are keyed by
// (class, weapon) and override re5_ct_weapons.ini for that costume only.
// A class section only ever matches its exact class name; no character-wide
// section is supported on purpose, so tuning one costume never changes another.
// Fallback for one mount, key by key:
// class -> [default] -> the value already in the game's table.
struct CharWeapon {
    char cls[64];
    int  id;
    WeaponCfg w;
};

static CharWeapon g_char_weapons[192];
static int g_char_weapon_count = 0;

static CharWeapon* find_char_weapon(const char* cls, int id, bool create)
{
    for (int i = 0; i < g_char_weapon_count; ++i)
        if (g_char_weapons[i].id == id && _stricmp(g_char_weapons[i].cls, cls) == 0)
            return &g_char_weapons[i];
    if (!create) return nullptr;
    if (g_char_weapon_count >= (int)(sizeof(g_char_weapons) / sizeof(g_char_weapons[0])))
        return nullptr;
    CharWeapon* cw = &g_char_weapons[g_char_weapon_count++];
    memset(cw, 0, sizeof(*cw));
    strncpy_s(cw->cls, sizeof(cw->cls), cls, _TRUNCATE);
    cw->id = id;
    return cw;
}

static void set_char_weapon_key(const char* cls, int id, const char* key, const char* val)
{
    CharWeapon* cw = find_char_weapon(cls, id, true);
    if (!cw) return;
    if (_stricmp(key, "pos117") == 0) cw->w.has_pos117 = (unsigned char)parse_vec3(val, cw->w.pos117);
    else if (_stricmp(key, "rot117") == 0) cw->w.has_rot117 = (unsigned char)parse_vec3(val, cw->w.rot117);
    else if (_stricmp(key, "pos2") == 0) cw->w.has_pos2 = (unsigned char)parse_vec3(val, cw->w.pos2);
    else if (_stricmp(key, "rot2") == 0) cw->w.has_rot2 = (unsigned char)parse_vec3(val, cw->w.rot2);
}

// Resolution for one mount is key by key: class -> [default] -> the value
// already in the game's table.
static bool resolve_char_weapon(const char* cls, int id, WeaponCfg* out)
{
    memset(out, 0, sizeof(*out));

    const char* names[2] = { cls, "default" };
    bool any = false;

    for (int k = 0; k < 2; ++k) {
        const char* n = names[k];
        if (!n || !*n) continue;
        if (k == 1 && _stricmp(names[0], n) == 0) continue;
        CharWeapon* cw = find_char_weapon(n, id, false);
        if (!cw) continue;
        if (cw->w.has_pos117) { memcpy(out->pos117, cw->w.pos117, 12); out->has_pos117 = 1; any = true; }
        if (cw->w.has_rot117) { memcpy(out->rot117, cw->w.rot117, 12); out->has_rot117 = 1; any = true; }
        if (cw->w.has_pos2)   { memcpy(out->pos2,   cw->w.pos2,   12); out->has_pos2 = 1;   any = true; }
        if (cw->w.has_rot2)   { memcpy(out->rot2,   cw->w.rot2,   12); out->has_rot2 = 1;   any = true; }
    }
    return any;
}

// default_section: used for plain key files (weapon overrides), e.g. "WP0F".
static void parse_weapon_ini(const char* path, const char* default_section)
{
    FILE* f = nullptr;
    if (fopen_s(&f, path, "r") != 0 || !f) return;

    char current[64];
    strncpy_s(current, sizeof(current), default_section, _TRUNCATE);

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
            continue;
        }

        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char* key = line;
        char* val = eq + 1;
        trim(key);
        trim(val);

        int id = weapon_id_from_name(current);
        if (id < 0) continue;
        WeaponCfg& w = g_weapons[id];

        if (_stricmp(key, "pos117") == 0) w.has_pos117 = (unsigned char)parse_vec3(val, w.pos117);
        else if (_stricmp(key, "rot117") == 0) w.has_rot117 = (unsigned char)parse_vec3(val, w.rot117);
        else if (_stricmp(key, "pos2") == 0) w.has_pos2 = (unsigned char)parse_vec3(val, w.pos2);
        else if (_stricmp(key, "rot2") == 0) w.has_rot2 = (unsigned char)parse_vec3(val, w.rot2);
    }
    fclose(f);
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

// Last class the decide stub resolved (fallback for the mount hook when the
// player cannot be derived from the mount call itself).
static char g_decide_class[64] = { 0 };

// The mounting code is located at runtime by signature: the Enigma-packed
// retail build shifts the function, so fixed addresses must not be used.
//   male:   6A 75 50 FF D2 8D 87 D0 00 00 00   (push 117; push eax; call; lea 0xD0)
//   female: 6A 02 50 FF D2 8D 87 F0 00 00 00   (push 2;   push eax; call; lea 0xF0)
// The two sites are 0x2F bytes apart. g_bone_* point at the immediate byte.
static DWORD g_weapon_base    = 0;
static DWORD g_bone_fem_addr  = 0;
static DWORD g_bone_male_addr = 0;

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

struct ClassModel { const char* cls; const char* model; };
static const ClassModel kClassModels[] = {
    { "uPl00ChrisNormal",   "pawn\\pl\\pl00\\model\\pl0000" },
    { "uPl00ChrisArmorA",   "pawn\\pl\\pl00\\model\\pl0500" },
    { "uPl00ChrisArmorB",   "pawn\\pl\\pl00\\model\\pl0900" },
    { "uPl00ChrisArmorF",   "pawn\\pl\\pl00\\model\\pl0a00" },
    { "uPl00ChrisPlain",    "pawn\\pl\\pl03\\model\\pl0300" },
    { "uPl00ChrisCos1",     "pawn\\pl\\pl10\\model\\pl0000" },
    { "uPl00ChrisCos2",     "pawn\\pl\\pl11\\model\\pl1100" },
    { "uPl00ChrisCos3",     "pawn\\pl\\pl20\\model\\pl0000" },
    { "uPl00ChrisCos4",     "pawn\\pl\\pl22\\model\\pl0000" },
    { "uPl00ChrisEp1",      "pawn\\pl\\pl04\\model\\pl0400" },
    { "uPl01ShebaNormal",   "pawn\\pl\\pl01\\model\\pl0100" },
    { "uPl01ShebaArmorA",   "pawn\\pl\\pl01\\model\\pl0600" },
    { "uPl01ShebaArmorB",   "pawn\\pl\\pl01\\model\\pl0b00" },
    { "uPl01ShebaArmorF",   "pawn\\pl\\pl01\\model\\pl0c00" },
    { "uPl01ShebaPlain",    "pawn\\pl\\pl0d\\model\\pl0d00" },
    { "uPl01ShebaCos1",     "pawn\\pl\\pl12\\model\\pl1200" },
    { "uPl01ShebaCos2",     "pawn\\pl\\pl13\\model\\pl0100" },
    { "uPl01ShebaCos3",     "pawn\\pl\\pl21\\model\\pl2100" },
    { "uPl01ShebaCos4",     "pawn\\pl\\pl23\\model\\pl2300" },
    { "uPl02JillCos1",      "pawn\\pl\\pl02\\model\\Pl0200" },
    { "uPl02JillCos2",      "pawn\\pl\\pl14\\model\\pl1400" },
    { "uPl02JillCos3",      "pawn\\pl\\pl17\\model\\pl1700" },
    { "uPl02JillCos4",      "pawn\\pl\\pl02\\model\\pl0200" },
    { "uPl03WeskerCos1",    "pawn\\pl\\pl15\\model\\pl1500" },
    { "uPl03WeskerCos2",    "pawn\\pl\\pl16\\model\\pl1600" },
    { "uPl04JoshCos1",      "pawn\\em\\em88\\model\\em8800" },
    { "uPl04JoshCos2",      "pawn\\pl\\pl18\\model\\pl1800" },
    { "uPl05ExcellaNormal", "pawn\\pl\\pl1e\\model\\pl1e00" },
    { "uPl06BarryNormal",   "pawn\\pl\\pl1c\\model\\pl1c00" },
    { "uPl07RebeccaNormal", "pawn\\pl\\pl1d\\model\\pl1d00" },
};

static void* g_loader_tramp = nullptr;

// Patch slots 17/18 (rChain/rChainCol) right before the resource loader walks
// the class resource table. Identifies the class by the model path in slot 0.
static void patch_chain_table(DWORD* table, unsigned count)
{
    if (!table || count < 19) return;
    const char* model = (const char*)table[0];
    if (!looks_like_model_path((DWORD)(uintptr_t)model)) return;

    const ClassModel* cm = nullptr;
    for (const ClassModel& m : kClassModels)
        if (_stricmp(m.model, model) == 0) { cm = &m; break; }
    if (!cm) return;

    Cfg cfg = resolve_cfg(cm->cls);
    if (cfg.chn <= 0) return;

    DWORD* chn = table + (0x88 / 4);
    DWORD* ccl = table + (0x90 / 4);
    bool want_chn = (cfg.chn & 1) && (*chn == 0);
    bool want_ccl = (cfg.chn & 2) && (*ccl == 0);
    if (!want_chn && !want_ccl) return;

    DWORD old = 0;
    if (!VirtualProtect(chn, 8, PAGE_READWRITE, &old)) return;
    if (want_chn) *chn = (DWORD)(uintptr_t)model;
    if (want_ccl) *ccl = (DWORD)(uintptr_t)model;
    VirtualProtect(chn, 8, old, &old);
    log_line("chain slots(load): %s chn=%d ccl=%d", cm->cls, want_chn, want_ccl);
}

static void __attribute__((thiscall)) loader_hook(void* self, void* table, unsigned count)
{
    typedef void (__attribute__((thiscall)) *Fn)(void*, void*, unsigned);
    patch_chain_table((DWORD*)table, count);
    Fn orig = (Fn)g_loader_tramp;
    if (orig) orig(self, table, count);
}

static BYTE* find_resource_loader()
{
    static const BYTE sig[] = {
        0x53, 0x55, 0x57, 0x8B, 0x7C, 0x24, 0x10, 0x33, 0xDB, 0x3B, 0xFB, 0x8B, 0xE9
    };
    return scan_for_bytes(sig, sizeof(sig));
}

static bool install_loader_hook()
{
    static bool reported = false;
    BYTE* fn = find_resource_loader();
    if (!fn) {
        if (!reported) { reported = true; log_line("chain slots: resource loader not found (will retry)"); }
        return false;
    }
    BYTE* t = (BYTE*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!t) return false;
    memcpy(t, fn, 7);
    t[7] = 0xE9;
    *(DWORD*)(t + 8) = (DWORD)(fn + 7) - ((DWORD)(t + 7) + 5);
    g_loader_tramp = t;

    DWORD old = 0;
    if (!VirtualProtect(fn, 7, PAGE_EXECUTE_READWRITE, &old)) return false;
    fn[0] = 0xE9;
    *(DWORD*)(fn + 1) = (DWORD)&loader_hook - ((DWORD)fn + 5);
    fn[5] = 0x90; fn[6] = 0x90;
    VirtualProtect(fn, 7, old, &old);
    FlushInstructionCache(GetCurrentProcess(), fn, 7);
    log_line("chain slots: resource loader at %p", (void*)fn);
    return true;
}

// ---------------------------------------------------------------- chain slots
static bool looks_like_model_path(DWORD ptr)
{
    if (ptr < 0x400000 || ptr > 0x20000000) return false;
    const char* s = (const char*)ptr;
    return s[0] == 'p' && s[1] == 'a' && s[2] == 'w' && s[3] == 'n';
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
static void write_code_byte(DWORD addr, BYTE value)
{
    DWORD old = 0;
    if (!VirtualProtect((void*)addr, 1, PAGE_EXECUTE_READWRITE, &old)) return;

    SuspendedThreads st;
    suspend_other_threads(st);
    *(BYTE*)addr = value;
    FlushInstructionCache(GetCurrentProcess(), (void*)addr, 1);
    resume_threads(st);

    VirtualProtect((void*)addr, 1, old, &old);
}

// Single-byte patch for the mount joints: a byte store is atomic, so the
// suspend-every-thread dance is unnecessary and much more expensive. These
// bytes are rewritten whenever the costume changes, and (with a trainer
// running) possibly on every frame - keep it cheap.
static void write_code_byte_fast(DWORD addr, BYTE value)
{
    DWORD old = 0;
    if (!VirtualProtect((void*)addr, 1, PAGE_EXECUTE_READWRITE, &old)) return;
    *(BYTE*)addr = value;
    FlushInstructionCache(GetCurrentProcess(), (void*)addr, 1);
    VirtualProtect((void*)addr, 1, old, &old);
}

// ---------------------------------------------------------------- write guards
//
// decide_c() runs for every player, every frame. Two things used to happen in
// that hot path and are now rate-limited, because a second tool in the process
// (the Ultimate Trainer, or just co-op with two different costumes) can make
// them permanent:
//
//  * site discovery - a full address-space scan whenever the mount signature
//    was not found yet. If another tool patched those bytes, the signature
//    never matched and the scan ran 60x/s -> stutter.
//  * joint byte writes - if something else restores the byte between calls we
//    end up rewriting it forever, each write with all threads suspended.
static DWORD g_bone_scan_next = 0;
static int   g_bone_scan_tries = 0;
static bool  g_bone_scan_gave_up = false;

static DWORD g_bone_write_tick = 0;
static int   g_bone_write_burst = 0;
static bool  g_bone_write_locked = false;

// One discovery attempt per 2 s, at most 20 attempts, then stop for good.
static bool bone_scan_due()
{
    if (g_bone_fem_addr || g_bone_scan_gave_up) return false;

    DWORD now = GetTickCount();
    if (now < g_bone_scan_next) return false;
    g_bone_scan_next = now + 2000;

    if (++g_bone_scan_tries > 20) {
        g_bone_scan_gave_up = true;
        log_line("bone: mount sites not found after %d tries - custom bone disabled "
                 "(another mod/trainer patched them?)", g_bone_scan_tries - 1);
        return false;
    }
    return true;
}

// Allow at most 8 writes per 250 ms window. Hitting the limit means the byte is
// being changed back by someone else, so keep the vanilla value and stop.
static bool bone_write_allowed()
{
    if (g_bone_write_locked) return false;

    DWORD now = GetTickCount();
    if (g_bone_write_tick && now - g_bone_write_tick < 250) ++g_bone_write_burst;
    else g_bone_write_burst = 1;
    g_bone_write_tick = now;

    if (g_bone_write_burst > 8) {
        g_bone_write_locked = true;
        log_line("bone: another writer keeps changing the mount site - "
                 "custom bone disabled for this session");
        return false;
    }
    return true;
}

static void set_bone_encoding(int bone);

// Any costume asking for a joint id that does not fit a sign-extended byte?
static bool any_wide_bone()
{
    for (int i = 0; i < g_section_count; ++i)
        if (g_sections[i].has_bone && g_sections[i].bone > 127) return true;
    return false;
}

static void write_bone_bytes(int bone)
{
    BYTE b = (BYTE)bone;
    if (!g_bone_fem_addr && !g_bone_male_addr) {
        static bool logged = false;
        if (!logged) { logged = true; log_line("bone: mount sites not found, skipping"); }
        return;
    }

    // 0..255 are valid anim-joint ids. The site holds a sign-extended byte, so
    // the value goes in as-is and the joint-getter thunk translates it back
    // (set_bone_encoding); ids 0..127 need no translation.
    set_bone_encoding(bone);

    bool fem_diff = g_bone_fem_addr && *(BYTE*)g_bone_fem_addr != b;
    bool male_diff = g_bone_male_addr && *(BYTE*)g_bone_male_addr != b;
    if (!fem_diff && !male_diff) return;
    if (!bone_write_allowed()) return;

    if (fem_diff) {
        log_line("bone: female %p %02x -> %02x", (void*)g_bone_fem_addr,
                 *(BYTE*)g_bone_fem_addr, b);
        write_code_byte_fast(g_bone_fem_addr, b);
    }
    if (male_diff) {
        log_line("bone: male %p %02x -> %02x", (void*)g_bone_male_addr,
                 *(BYTE*)g_bone_male_addr, b);
        write_code_byte_fast(g_bone_male_addr, b);
    }
}

static void reset_bone_bytes()
{
    set_bone_encoding(-1);

    bool fem_diff = g_bone_fem_addr && *(BYTE*)g_bone_fem_addr != 0x02;
    bool male_diff = g_bone_male_addr && *(BYTE*)g_bone_male_addr != 0x75;
    if (!fem_diff && !male_diff) return;
    if (!bone_write_allowed()) return;

    if (fem_diff) {
        log_line("bone: female reset %p %02x -> 02", (void*)g_bone_fem_addr,
                 *(BYTE*)g_bone_fem_addr);
        write_code_byte_fast(g_bone_fem_addr, 0x02);
    }
    if (male_diff) {
        log_line("bone: male reset %p %02x -> 75", (void*)g_bone_male_addr,
                 *(BYTE*)g_bone_male_addr);
        write_code_byte_fast(g_bone_male_addr, 0x75);
    }
}


// ---------------------------------------------------------------- wide bone ids
//
// 0..255 are valid anim-joint ids, but the vanilla site is `push imm8`, which
// sign-extends: an id above 127 arrives at the engine as a negative number and
// the joint lookup misses (the weapon is drawn at the world origin).
//
// Instead of owning those 5 bytes (another tool in the process may rewrite
// them, and a torn instruction crashes the game), the id is delivered through
// the joint getter: the mount call is `[actor]->vtbl[0x34](x, joint)`, so that
// one vtable slot is redirected to a thunk that translates the encoded negative
// byte into the real id. The mount code itself stays vanilla; if anything else
// overwrites our byte - or the slot - the thunk simply does not match and the
// game behaves as if the feature were off.
static void* g_bone_orig_joint = nullptr;   // original vtbl[0x34]
static void** g_bone_slot = nullptr;        // &vtbl[0x34]
static int   g_bone_enc = 0x7FFFFFFF;       // encoded (sign-extended) byte
static int   g_bone_val = -1;               // id to deliver instead
static BYTE* g_bone_thunk = nullptr;        // generated translator

// Generated machine code (no inline asm, so no symbol/linkage surprises):
//   mov  edx, [esp+8]        ; joint argument
//   cmp  edx, [g_bone_enc]
//   jne  pass
//   mov  edx, [g_bone_val]
//   mov  [esp+8], edx
// pass:
//   jmp  dword ptr [g_bone_orig_joint]
static BYTE* build_bone_thunk()
{
    BYTE* s = (BYTE*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE,
                                  PAGE_EXECUTE_READWRITE);
    if (!s) return nullptr;
    size_t i = 0;
    s[i++] = 0x8B; s[i++] = 0x54; s[i++] = 0x24; s[i++] = 0x08;     // mov edx,[esp+8]
    s[i++] = 0x3B; s[i++] = 0x15;                                   // cmp edx,[imm32]
    *(DWORD*)(s + i) = (DWORD)&g_bone_enc; i += 4;
    s[i++] = 0x75;                                                  // jne rel8
    size_t jne_at = i;
    s[i++] = 0x00;
    s[i++] = 0x8B; s[i++] = 0x15;                                   // mov edx,[imm32]
    *(DWORD*)(s + i) = (DWORD)&g_bone_val; i += 4;
    s[i++] = 0x89; s[i++] = 0x54; s[i++] = 0x24; s[i++] = 0x08;     // mov [esp+8],edx
    s[jne_at] = (BYTE)(i - (jne_at + 1));
    s[i++] = 0xFF; s[i++] = 0x25;                                   // jmp [imm32]
    *(DWORD*)(s + i) = (DWORD)&g_bone_orig_joint; i += 4;
    return s;
}

static void set_bone_encoding(int bone)
{
    // 128..254 arrive as a sign-extended byte, which is why the thunk maps them
    // back. 255 is excluded on purpose: its byte is 0xFF, i.e. -1 - the same
    // value vanilla code passes for "no joint" - so translating it would hijack
    // every "none" call instead of adding anything.
    if (bone > 127 && bone < 255) {
        g_bone_val = bone;
        g_bone_enc = (int)(signed char)(BYTE)bone;
    } else {
        g_bone_val = -1;
        g_bone_enc = 0x7FFFFFFF;            // cannot match any real argument
    }
}

// Called from the mount hook (ecx = item) before the joint push happens.
// Validates every pointer: a wrong vtable slot would corrupt unrelated calls.
static void install_bone_slot_hook(void* item)
{
    if (g_bone_slot || !item) return;

    BYTE* p = (BYTE*)item;
    void* actor = *(void**)(p + 0x2C);
    if (!actor || (uintptr_t)actor < 0x400000 || (uintptr_t)actor > 0x20000000) return;

    void** vt = *(void***)actor;
    if (!vt || (uintptr_t)vt < 0x400000 || (uintptr_t)vt > 0x20000000) return;

    void** slot = &vt[0x34 / 4];
    void* orig = *slot;
    if (!orig || (uintptr_t)orig < 0x400000 || (uintptr_t)orig > 0x20000000) return;

    if (!g_bone_thunk) g_bone_thunk = build_bone_thunk();
    if (!g_bone_thunk) return;

    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
        log_line("bone: cannot hook the joint getter (slot %p)", (void*)slot);
        return;
    }
    g_bone_orig_joint = orig;
    *slot = (void*)g_bone_thunk;
    VirtualProtect(slot, sizeof(void*), old, &old);

    g_bone_slot = slot;
    log_line("bone: joint getter hooked at slot %p (was %p)", (void*)slot, orig);
}
static DWORD find_weapon_table_base(BYTE* fem_site);
static bool table_looks_valid(DWORD base);
static DWORD find_table_near(BYTE* fem_site);

// The table base is an immediate in the mount function (`add $imm,%edi` or a
// `lea`), but the exact opcode differs between builds. Scan the function bytes
// for any dword that points at a valid weapon table.
static DWORD find_table_near(BYTE* fem_site)
{
    BYTE* begin = fem_site - 0x400;
    for (BYTE* p = begin; p + 4 < fem_site; ++p) {
        DWORD v = *(DWORD*)p;
        if (v >= 0x400000 && v < 0x2000000 && table_looks_valid(v))
            return v;
    }
    return 0;
}

// Locate the two push-imm sites by signature. Male = female - 0x2F.
// Scans [lo, hi) of the address space; stops at the first hit.
static bool scan_bone_sites(BYTE* lo, BYTE* hi)
{
    static const BYTE kFem[11] = { 0x6A,0x02,0x50,0xFF,0xD2,0x8D,0x87,0xF0,0x00,0x00,0x00 };
    static const BYTE kMale[11] = { 0x6A,0x75,0x50,0xFF,0xD2,0x8D,0x87,0xD0,0x00,0x00,0x00 };

    HANDLE self = GetCurrentProcess();
    BYTE* addr = lo;

    while ((uintptr_t)addr < (uintptr_t)hi) {
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
                    got >= sizeof(kFem)) {
                    for (SIZE_T i = 0; i + sizeof(kFem) <= got; ++i) {
                        if (memcmp(buf + i, kFem, sizeof(kFem)) != 0) continue;
                        BYTE* fem = base + off + i;
                        BYTE* male = fem - 0x2F;
                        bool male_ok = memcmp(male, kMale, sizeof(kMale)) == 0;
                        log_line("bone site: female=%p male=%p (male sig %s)",
                                 (void*)fem, (void*)male, male_ok ? "ok" : "MISSING");
                        g_bone_fem_addr = (DWORD)(fem + 1);
                        if (male_ok) g_bone_male_addr = (DWORD)(male + 1);
                        g_weapon_base = find_weapon_table_base(fem);
                        if (!g_weapon_base) g_weapon_base = find_table_near(fem);
                        log_line("weapon table base: %p", (void*)g_weapon_base);
                        free(buf);
                        return male_ok;
                    }
                }
                free(buf);
            }
        }
        addr = base + size;
    }
    return false;
}

// The mount code lives in re5dx9.exe, so try its image first: that is a few MB
// instead of every executable region of the process. The full-space scan stays
// as a fallback (the retail build is Enigma-packed, so the code can also show
// up in a section that is mapped later).
static bool find_bone_sites()
{
    HMODULE mod = GetModuleHandleA(nullptr);
    if (mod) {
        IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)mod;
        if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
            IMAGE_NT_HEADERS32* nt = (IMAGE_NT_HEADERS32*)((BYTE*)mod + dos->e_lfanew);
            if (nt->Signature == IMAGE_NT_SIGNATURE &&
                scan_bone_sites((BYTE*)mod, (BYTE*)mod + nt->OptionalHeader.SizeOfImage))
                return true;
        }
    }
    return scan_bone_sites((BYTE*)0x10000, (BYTE*)0x7FFF0000);
}

// ---------------------------------------------------------------- decision
extern "C" unsigned char decide_c(void* player, unsigned type)
{
    const char* cls = class_of_player(player);
    if (cls && *cls) strncpy_s(g_decide_class, sizeof(g_decide_class), cls, _TRUNCATE);
    Cfg cfg = resolve_cfg(cls);

    // The code may be decrypted only when the mount function is first used.
    // Rate-limited: this runs in the per-frame decision path.
    if (cfg.custom && bone_scan_due()) find_bone_sites();

    if (cfg.custom && cfg.bone >= 0 && cfg.bone <= 255) write_bone_bytes(cfg.bone);
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
    if (GetModuleFileNameA(mod, path, MAX_PATH)) {
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

static void load_weapon_file(const char* path)
{
    if (!path || !*path) return;
    parse_weapon_ini(path, "");
    log_line("weapon ini: %s", path);
}

static void load_weapon_folder(const char* dir)
{
    char folder[MAX_PATH];
    strncpy_s(folder, sizeof(folder), dir, _TRUNCATE);
    strcat_s(folder, sizeof(folder), "re5_weapon_overrides");
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
        char base[64] = { 0 };
        const char* fn = strrchr(names[i], '\\');
        fn = fn ? fn + 1 : names[i];
        strncpy_s(base, sizeof(base), fn, _TRUNCATE);
        char* dot = strrchr(base, '.');
        if (dot) *dot = 0;
        parse_weapon_ini(names[i], base);
        log_line("weapon override: %s -> [%s]", names[i], base);
    }
}

static void write_default_settings(const char* dir)
{
    char path[MAX_PATH];
    path_join(path, sizeof(path), dir, kCfgSettings);
    if (file_exists(path)) return;

    FILE* f = nullptr;
    if (fopen_s(&f, path, "w") != 0 || !f) return;
    fputs(
        "; re5_char_tweaks - global settings\n"
        ";\n"
        "; All keys are snake_case; a missing key keeps its default value.\n"
        "; Character (costume) table:  re5_ct_characters.ini\n"
        "; Weapon table:              re5_ct_weapons.ini\n"
        "; Per-mod overrides:         re5_char_overrides\\, re5_weapon_overrides\\\n"
        "; Loose file overrides:      re5_file_overrides\\<archive path>.<ext>\n"
        "\n"
        "[settings]\n"
        "; 1 = files from re5_file_overrides\\ override the matching .arc entries,\n"
        ";     0 = disable the file-override hooks\n"
        "file_overrides=1\n"
        "\n"
        "; 1 = only resource types the game can load are indexed (mod, mtg, tex,\n"
        ";     lmt, chn, ccl, efl, ...); previews, backups and source files are\n"
        ";     skipped and listed in the log. 0 = index every file.\n"
        "file_override_whitelist=1\n"
        "\n"
        "; extra extensions to accept, comma separated (e.g. \"abc,def\")\n"
        "file_override_exts=\n"
        "\n"
        "; how long to wait before patching code (the retail exe is Enigma-protected;\n"
        "; patching earlier makes the game exit with \"Program integrity check failed\")\n"
        "delay_ms=10000\n"
        "\n"        "\n"
        "; 1 = write %TEMP%\\re5_weapon_offsets.txt with the runtime weapon table (0 = off)\n"
        "dump_weapons=0\n",
        f);
    fclose(f);
}

static void write_default_characters(const char* dir)
{
    char path[MAX_PATH];
    path_join(path, sizeof(path), dir, kCfgCharacters);
    if (file_exists(path)) return;

    FILE* f = nullptr;
    if (fopen_s(&f, path, "w") != 0 || !f) return;
    fputs(
        "; re5_char_tweaks - character (costume) table\n"
        ";\n"
        "; Sections are exact class names (costumes), not characters. Every key that is\n"
        "; not set in a class section falls back to [default].\n"
        "; Global switches live in re5_ct_settings.ini.\n"
        ";\n"
        "; Overrides for mods: put <class>.ini files into re5_char_overrides\\\n"
        "; (next to the .asi). Example: re5_char_overrides\\uPl00ChrisCos3.ini\n"
        "; with plain keys (no section header needed):\n"
        ";     back=0\n"
        ";     custom_bone=1\n"
        ";     bone=4\n"
        ";\n"
        "; back:         -1 = vanilla, 0 = never mount, 1 = force mount\n"
        "; custom_bone:  0/1 - use the joint below for the mount\n"
        "; bone:         0..255 - anim joint No\n"
        "; chn:          0 = off, 1 = .chn, 2 = .ccl, 3 = both\n"
        ";\n"
        "; Per-costume weapon offsets: a [WPxx] section inside a class section overrides\n"
        "; re5_ct_weapons.ini for that costume only, e.g.\n"
        ";     [uPl00ChrisCos3]\n"
        ";     back=0\n"
        ";     [WP0F]\n"
        ";     pos2=-5,7,-13\n"
        "; Keys: pos117 / rot117 / pos2 / rot2 (same as re5_ct_weapons.ini).\n"
        "; A [WPxx] block is owned by the exact class section above it; there is no\n"
        "; character-wide section, so one costume's tuning never affects another.\n"
        ";\n"
        "\n",
        f);
    fputs("[default]\nback=-1\ncustom_bone=0\nbone=0\nchn=0\n\n", f);
    // Every costume starts at vanilla behaviour: a shipped framework must not
    // change the game until a key is edited.
    for (const ClassTable& ct : kClassTables) {
        fprintf(f, "; %s\n[%s]\nback=-1\ncustom_bone=0\nbone=0\nchn=0\n\n",
                ct.label, ct.cls);
    }
    fclose(f);
}

// Weapon mount offsets live in a global per-weapon table:
//   0x11A53F0 + (weaponId & 0xFF) * 0x140
//   +0xD0 pos vec3 / +0xE0 rot vec3   (joint 117 branch)
//   +0xF0 pos vec3 / +0x100 rot vec3  (spine branch)
// Optional dump: [default] dump_weapons=1
// The mount code computes `table + id*0x140` with `add $imm32,%edi` right
// before the branch, so the table base can be recovered from the code.
static DWORD find_weapon_table_base(BYTE* fem_site)
{
    BYTE* limit = fem_site - 0x400;
    for (BYTE* p = fem_site - 6; p >= limit; --p) {
        if (p[0] == 0x81 && p[1] == 0xC7) { // add edi, imm32
            DWORD imm = *(DWORD*)(p + 2);
            if (imm > 0x0F000000 && imm < 0x20000000)
                return imm;
        }
    }
    return 0;
}

static bool safe_read(DWORD addr, void* buf, SIZE_T size)
{
    if (addr < 0x10000 || addr > 0x7FFF0000) return false;
    if (size > 0x1000 || addr + size < addr) return false;
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)addr, buf, size, &got)
           && got == size;
}

static bool table_looks_valid(DWORD base)
{
    DWORD nameptr = 0;
    if (!safe_read(base + 4, &nameptr, sizeof(nameptr))) return false;
    char s[2] = { 0, 0 };
    if (!safe_read(nameptr, s, 2)) return false;
    return s[0] == 'W' && s[1] == 'P';
}

static bool weapon_table_base(DWORD* out)
{
    if (g_weapon_base && table_looks_valid(g_weapon_base)) { *out = g_weapon_base; return true; }
    if (table_looks_valid(0x11A53F0)) { *out = 0x11A53F0; return true; }
    return false;
}

static int apply_weapon_offsets()
{
    DWORD base = 0;
    if (!weapon_table_base(&base)) return 0;

    int applied = 0;
    for (int id = 0; id < 256; ++id) {
        WeaponCfg& w = g_weapons[id];
        if (!(w.has_pos117 || w.has_rot117 || w.has_pos2 || w.has_rot2)) continue;

        DWORD addr = base + (DWORD)id * 0x140;
        DWORD old = 0;
        if (!VirtualProtect((void*)addr, 0x140, PAGE_READWRITE, &old)) continue;

        float* p = (float*)addr;
        if (w.has_pos117) memcpy(&p[0xD0/4], w.pos117, sizeof(w.pos117));
        if (w.has_rot117) memcpy(&p[0xE0/4], w.rot117, sizeof(w.rot117));
        if (w.has_pos2)   memcpy(&p[0xF0/4], w.pos2,   sizeof(w.pos2));
        if (w.has_rot2)   memcpy(&p[0x100/4], w.rot2,  sizeof(w.rot2));

        VirtualProtect((void*)addr, 0x140, old, &old);
        ++applied;
    }
    if (applied) log_line("weapon overrides applied: %d", applied);
    return applied;
}

static bool dump_weapon_offsets()
{
    if (!g_code) return false;

    DWORD tbl = 0;
    if (g_weapon_base && table_looks_valid(g_weapon_base)) tbl = g_weapon_base;
    else if (table_looks_valid(0x11A53F0)) tbl = 0x11A53F0;
    if (!tbl) {
        static bool logged = false;
        if (!logged) { logged = true; log_line("weapon dump: no valid table (candidate %p)", (void*)g_weapon_base); }
        return false;
    }

    char path[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("TEMP", path, MAX_PATH);
    if (!n || n >= MAX_PATH) return false;
    strcat_s(path, MAX_PATH, "\\re5_weapon_offsets.txt");

    FILE* f = nullptr;
    if (fopen_s(&f, path, "w") != 0 || !f) return false;

    fprintf(f, "# weapon id, name, pos117, rot117, pos_spine, rot_spine (3 floats each)\n");
    fprintf(f, "# table base %08X\n", tbl);

    int count = 0;
    BYTE entry[0x110];
    for (int id = 0; id < 0x100; ++id) {
        DWORD addr = tbl + (DWORD)id * 0x140;
        if (!safe_read(addr, entry, sizeof(entry))) continue;

        DWORD nameptr = *(DWORD*)(entry + 4);
        char name[64] = { 0 };
        if (!safe_read(nameptr, name, 48)) continue;
        if (name[0] != 'W' || name[1] != 'P') continue;

        float* p = (float*)entry;
        fprintf(f, "%02X %-28.28s  pos117 %8.3f %8.3f %8.3f  rot117 %8.3f %8.3f %8.3f  pos2 %8.3f %8.3f %8.3f  rot2 %8.3f %8.3f %8.3f\n",
                id, name,
                p[0xD0/4], p[0xD4/4], p[0xD8/4],
                p[0xE0/4], p[0xE4/4], p[0xE8/4],
                p[0xF0/4], p[0xF4/4], p[0xF8/4],
                p[0x100/4], p[0x104/4], p[0x108/4]);
        ++count;
    }
    fclose(f);
    log_line("weapon dump: %d entries (base %08X) -> %s", count, tbl, path);
    return count > 0;
}

// ---------------------------------------------------------------- per-class mount hook
//
// The mount function reads the per-weapon offset block only when it actually
// mounts the holstered weapon (2018 build 0xC4D700..0xC4D93x):
//
//   c4d7f6: cmp  byte [ebp+0xc],1        ; mount enabled?
//   c4d7fa: jne  0xc4d931
//   c4d800: mov  eax,[ecx+4]             ; item id      <- hook site
//   c4d803: mov  edx,eax
//   c4d805: and  edx,0xff00
//   c4d80b: cmp  edx,0x100               ; weapon?
//   ...
//   c4d81c: lea  edi,[eax+eax*4] ; shl edi,6 ; add edi,0x11a53f0
//   c4d88b: lea  eax,[edi+0xd0] ; push eax ; call setPosition
//   c4d899: add  edi,0xe0       ; push edi ; call setRotation
//
// At c4d800 `ecx` is the item and `edi` is the item manager of the player
// (`uPlayer + 0x21A0`, the caller passes exactly that). The 11 bytes of that
// instruction pair are replaced by a jump to a stub that rewrites the 16 floats
// at table[id] + 0xD0 for this costume (class -> [default] -> table value),
// then re-executes the original bytes.
//
// The player is resolved the same way the mount function itself does it a few
// instructions later:
//
//   c4d83b: mov  ecx,[esi+0x18]          ; esi = [item+0x2c], coord owner
//   c4d846: push 0x11F34F8               ; key (a data address)
//   c4d84b: call 0x61D980                ; -> uPlayer*, 0 if not found
//
// The key and the call target are read from the code at install time, so the
// retail build's shifted data address does not matter. If that lookup fails,
// the item manager (`edi - 0x21A0`) and finally the class the decide stub saw
// are used.
//
// The write is per mount call, right before the read, so two costumes with
// different offsets never see each other's values. The first write for a
// weapon snapshots the block, so a costume without an override gets the
// previous values (vanilla, or a global re5_ct_weapons.ini override) back.
static DWORD g_mount_site = 0;
static BYTE* g_mount_stub = nullptr;

static unsigned char g_wp_touched[256];      // block snapshotted
static float g_wp_base[256][16];             // +0xD0 .. +0x10F
static char  g_wp_last[256][64];             // costume last written

static void* g_lookup_fn = nullptr;          // mount function's player lookup
static DWORD g_lookup_key = 0;

// __thiscall: ecx = owner, one stack argument (the callee pops it).
static void* lookup_player(void* owner)
{
    if (!g_lookup_fn || !owner) return nullptr;
    typedef void* (__attribute__((thiscall)) *Fn)(void*, void*);
    return ((Fn)g_lookup_fn)(owner, (void*)g_lookup_key);
}

static DWORD find_mount_site(BYTE* fem_site)
{
    // 8B 41 04 8B D0 81 E2 00 FF 00 00
    // mov eax,[ecx+4] / mov edx,eax / and edx,0xFF00
    static const BYTE pat[11] = {
        0x8B, 0x41, 0x04, 0x8B, 0xD0, 0x81, 0xE2, 0x00, 0xFF, 0x00, 0x00
    };

    // Reference build: 0xC4D800 = 0xB5 bytes before the joint-2 push site
    // (0xC4D8B5), i.e. 0x86 bytes before the joint-117 site (0xC4D886).
    BYTE* guess = fem_site - 0xB5;
    if (memcmp(guess, pat, sizeof(pat)) == 0) return (DWORD)guess;

    // Tolerate a small layout change between builds.
    for (int d = 0x20; d <= 0x300; ++d) {
        BYTE* p = fem_site - d;
        if (memcmp(p, pat, sizeof(pat)) == 0) {
            log_line("mount hook: site at -0x%X from the bone site", d);
            return (DWORD)p;
        }
    }
    return 0;
}

extern "C" void mount_apply(void* manager, void* item, unsigned item_id);

static BYTE* build_mount_stub(DWORD site)
{
    static const BYTE orig[11] = {
        0x8B, 0x41, 0x04, 0x8B, 0xD0, 0x81, 0xE2, 0x00, 0xFF, 0x00, 0x00
    };
    BYTE* s = (BYTE*)VirtualAlloc(nullptr, 128, MEM_COMMIT | MEM_RESERVE,
                                  PAGE_EXECUTE_READWRITE);
    if (!s) return nullptr;

    int i = 0;
    s[i++] = 0x60;                                   // pushad
    s[i++] = 0x9C;                                   // pushfd
    s[i++] = 0x8B; s[i++] = 0x41; s[i++] = 0x04;     // mov eax,[ecx+4] (item id)
    s[i++] = 0x50;                                   // push eax (id)
    s[i++] = 0x51;                                   // push ecx (item)
    s[i++] = 0x57;                                   // push edi (item manager)
    s[i++] = 0xE8;                                   // call mount_apply
    *(DWORD*)(s + i) = (DWORD)&mount_apply - ((DWORD)s + i + 4); i += 4;
    s[i++] = 0x83; s[i++] = 0xC4; s[i++] = 0x0C;     // add esp,12
    s[i++] = 0x9D;                                   // popfd
    s[i++] = 0x61;                                   // popad
    memcpy(s + i, orig, sizeof(orig)); i += sizeof(orig);
    s[i++] = 0xE9;                                   // jmp site+11
    *(DWORD*)(s + i) = (DWORD)(site + 11) - ((DWORD)s + i + 4); i += 4;
    return s;
}

static bool install_mount_hook()
{
    if (g_mount_site) return true;
    if (!g_bone_fem_addr) return false;

    BYTE* fem = (BYTE*)g_bone_fem_addr - 1;           // push 2 site
    DWORD site = find_mount_site(fem);
    if (!site) {
        static bool logged = false;
        if (!logged) { logged = true; log_line("mount hook: site pattern not found near %p", (void*)fem); }
        return false;
    }

    BYTE* stub = build_mount_stub(site);
    if (!stub) { log_line("mount hook: stub allocation failed"); return false; }

    // Read the player lookup the mount function itself uses (see the comment
    // above): push <key> / call <fn> right after the hook site.
    BYTE* push_key = (BYTE*)site + 0x46;
    BYTE* call_rel = (BYTE*)site + 0x4B;
    if (push_key[0] == 0x68 && call_rel[0] == 0xE8) {
        g_lookup_key = *(DWORD*)(push_key + 1);
        int rel = *(int*)(call_rel + 1);
        g_lookup_fn = (void*)(call_rel + 5 + rel);
    } else {
        log_line("mount hook: player lookup pattern missing (push=%02X call=%02X)",
                 push_key[0], call_rel[0]);
    }

    SuspendedThreads st;
    suspend_other_threads(st);

    DWORD old = 0;
    bool ok = VirtualProtect((void*)site, 11, PAGE_EXECUTE_READWRITE, &old) != 0;
    if (ok) {
        BYTE code[11];
        memset(code, 0x90, sizeof(code));
        code[0] = 0xE9;
        *(DWORD*)(code + 1) = (DWORD)stub - (site + 5);
        memcpy((void*)site, code, sizeof(code));
        FlushInstructionCache(GetCurrentProcess(), (void*)site, sizeof(code));
        VirtualProtect((void*)site, 11, old, &old);
    }

    resume_threads(st);

    if (!ok) { log_line("mount hook: patch failed at %p", (void*)site); return false; }

    g_mount_site = site;
    g_mount_stub = stub;
    log_line("mount hook: %p -> %p (lookup %p key %08X)", (void*)site, (void*)stub,
             g_lookup_fn, g_lookup_key);
    return true;
}

// Runs on the game thread with ecx = item, edi = item manager (see above).
extern "C" void mount_apply(void* manager, void* item, unsigned item_id)
{
    // Before the mount function pushes the joint id: make sure the joint getter
    // can deliver ids above 127 (it only sees a sign-extended byte).
    install_bone_slot_hook(item);

    static int dbg = 0;
    if (dbg < 24) {
        ++dbg;
        log_line("wp call: item=%04X mgr=%p item=%p", item_id, manager, item);
    }

    if (!g_weapon_base || !manager) return;
    if ((item_id & 0xFF00) != 0x100) return;          // not a weapon

    int id = (int)(item_id & 0xFF);

    // Player of the weapon being mounted. The mount function resolves it a few
    // instructions later with a lookup on the coord owner; do the same first,
    // then fall back to the item manager pointer and finally to the class the
    // decide stub saw, so a layout difference in the retail build cannot
    // silently disable the feature.
    void* player = nullptr;
    if (item && g_lookup_fn) {
        void* wc = *(void**)((char*)item + 0x2C);
        if (wc) player = lookup_player(*(void**)((char*)wc + 0x18));
    }

    WeaponCfg w;
    const char* cls = nullptr;
    bool any = false;

    if (player) {
        const char* c = class_of_player(player);
        if (c && *c && resolve_char_weapon(c, id, &w)) { any = true; cls = c; }
    }
    if (!any) {
        const char* c = class_of_player((char*)manager - 0x21A0);
        if (c && *c && resolve_char_weapon(c, id, &w)) { any = true; cls = c; }
    }
    if (!any && g_decide_class[0] && resolve_char_weapon(g_decide_class, id, &w)) {
        any = true;
        cls = g_decide_class;
    }

    if (!any && !g_wp_touched[id]) return;

    DWORD data = g_weapon_base + (DWORD)id * 0x140 + 0xD0;
    if (!g_wp_touched[id]) {
        if (!safe_read(data, g_wp_base[id], sizeof(g_wp_base[id]))) return;
        g_wp_touched[id] = 1;
    }

    float v[16];
    memcpy(v, g_wp_base[id], sizeof(v));
    if (w.has_pos117) memcpy(v + 0,  w.pos117, 12);
    if (w.has_rot117) memcpy(v + 4,  w.rot117, 12);
    if (w.has_pos2)   memcpy(v + 8,  w.pos2,   12);
    if (w.has_rot2)   memcpy(v + 12, w.rot2,   12);

    float cur[16];
    if (!safe_read(data, cur, sizeof(cur))) return;

    if (memcmp(cur, v, sizeof(v)) != 0) {
        DWORD old = 0;
        if (!VirtualProtect((void*)data, sizeof(v), PAGE_READWRITE, &old)) return;
        memcpy((void*)data, v, sizeof(v));
        VirtualProtect((void*)data, sizeof(v), old, &old);
    }

    if (cls && _stricmp(g_wp_last[id], cls) != 0) {
        strncpy_s(g_wp_last[id], sizeof(g_wp_last[id]), cls, _TRUNCATE);
        log_line("wp: class=%s id=%02X pos117=%d rot117=%d pos2=%d rot2=%d",
                 cls, id, w.has_pos117, w.has_rot117, w.has_pos2, w.has_rot2);
    }
}


// ---------------------------------------------------------------- generic scan
static BYTE* scan_for_bytes(const BYTE* sig, size_t len)
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
                if (ReadProcessMemory(self, base + off, buf, to_read, &got) && got >= len) {
                    for (SIZE_T i = 0; i + len <= got; ++i) {
                        if (memcmp(buf + i, sig, len) == 0) {
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

// ---------------------------------------------------------------- file overrides
//
// When an .arc is mounted, rArchive::load parses the whole TOC and registers
// every entry in the sResource hash table (precreate -> registTable). After
// that sResource::create() finds the entry in the table and never touches the
// disk, so a loose file could never win by itself.
//
// The two detours below make a file from re5_file_overrides win over the arc:
//
//   sResource::precreate   - for an entry whose (path, resource type) has a
//                            matching file, the archive entry is not registered;
//                            instead the resource is loaded from disk with
//                            create(dti, path, 1) and that object is returned
//                            (registered and marked loaded, so the archive
//                            decoder skips it). Any file size is allowed.
//   sResource::getFullPath - after the original builds
//                            "<...>\<path>.<ext>", the path is rewritten to
//                            <dir of this asi>\re5_file_overrides\<path>.<ext>.
//
// The resource type has to be resolved exactly (dti->newInstance()->getExt()),
// otherwise every resource sharing the base name (pl0000.mod / .mtg / .chn ...)
// would be rebound, and a missing .chn kills the game with
// "Failed open file ... pl0000.chn".
//
// The hooks are installed after g_delay_ms (10 s by default): the retail exe is
// protected by Enigma, and patching the code before its integrity check
// finishes makes the game show "Program integrity check failed" and exit.

#define FOV_MAX 256           // loose files indexed at startup

static BYTE* g_tramp_create = nullptr;
static BYTE* g_tramp_precreate = nullptr;
static BYTE* g_tramp_getfullpath = nullptr;

struct FileOverride {
    char base[160];   // lowercase, "pawn\pl\pl00\model\pl0000"
    char ext[12];     // lowercase, "mod"
    char file[MAX_PATH];
};

static FileOverride g_fov[FOV_MAX];
static int  g_fov_count = 0;
static int  g_fov_skipped = 0;   // rejected by the type whitelist
static int  g_fov_dropped = 0;   // did not fit into g_fov
static char g_fov_dir[MAX_PATH] = { 0 };

static const int kSkipLogLines = 64;   // per-file "skip" log lines, then summary only

// Resource types RE5 registers in a .arc TOC, i.e. what cResource::getExt()
// returns for the classes the engine instantiates. This is the game's own
// (type id -> extension -> class) table, so a file with any other extension is
// never requested by the engine: indexing it would only burn one of the
// FOV_MAX slots - and previews, backups and source files would push real
// overrides out of the table. Anything missing here can be added with
// file_override_exts= in re5_ct_settings.ini; file_override_whitelist=0
// restores the old "index every file" behaviour.
//
//   ext   class
struct ResType { const char* ext; const char* cls; };

static const ResType kResTypes[] = {
    { "ccl",  "rChainCol" },              { "lom",  "rLotSelectorMove" },
    { "hit",  "rHit" },                   { "efs",  "rEffectStrip" },
    { "sds",  "rSoundDirectionalSet" },   { "rvt",  "rSoundReverbTable" },
    { "ase",  "rSoundAttributeSe" },      { "oba",  "rObjAdj" },
    { "scs",  "rSoundCurveSet" },         { "msg",  "rMessage" },
    { "lot",  "rLayout" },                { "stq",  "rSoundStreamRequest" },
    { "los",  "rLotSelector" },           { "lnk",  "sLightLinker::rLightLinker" },
    { "nck",  "rNeck" },                  { "srq",  "rSoundRequest" },
    { "glp",  "rGoalPos" },               { "adh",  "rAdhesion" },
    { "jex",  "rJointEx" },               { "rev",  "rSoundReverb" },
    { "tex",  "rTexture" },               { "idm",  "sId::rIdMapList" },
    { "lku",  "rLinkUnit" },              { "e2d",  "rEffect2D" },
    { "equ",  "rSoundEQ" },               { "smh",  "rScrHiding" },
    { "srd",  "rSoundRandom" },           { "cdf",  "rCloth" },
    { "pth",  "rPath" },                  { "vib",  "rVibration" },
    { "hkx",  "rHkCollision" },           { "seg",  "rSoundSeGenerator" },
    { "lcm",  "rCameraList" },            { "chn",  "rChain" },
    { "ptl",  "rPathList" },              { "egv",  "rSoundEngineValue" },
    { "sdl",  "rScheduler" },             { "mse",  "rSoundMotionSe" },
    { "cmi",  "sCubeMapInterp" },         { "mtg",  "rModelMontage" },
    { "ean",  "rEffectAnim" },            { "nav",  "rNavigationMesh" },
    { "hri",  "rHkRBInfoList" },          { "bfx",  "rShader" },
    { "sbc",  "rCollision" },             { "shp",  "rShape" },
    { "eng",  "rSoundEngine" },           { "aef",  "rAHEffect" },
    { "ahc",  "rAHCamera" },              { "pos",  "rPos" },
    { "bgm",  "rBioBgmMap" },             { "mod",  "rModel" },
    { "way",  "rAIWayPoint" },            { "shw",  "rShapeWeight" },
    { "lsp",  "rLayoutSpr" },             { "sce",  "rScenario" },
    { "fsm",  "rAIFSM" },                 { "stp",  "rStartPos" },
    { "efl",  "rEffectList" },            { "ahs",  "rAreaHitShape" },
    { "cef",  "rCommonEffect" },          { "lmt",  "rMotionList" },
    { "rtex", "rRenderTargetTexture" },   { "sngw", "rSoundSourceMusic" },
    { "spc",  "rSoundPackage" },
    // rSoundReverb is .rev_win on some platforms; both spellings are accepted
    { "rev_win", "rSoundReverb" },
};

static char g_extra_exts[32][12];
static int  g_extra_ext_count = 0;

// Parses "dds, psd;foo" -> {"dds","psd","foo"}; a leading dot is accepted.
static void add_extra_exts(const char* list)
{
    const char* p = list;
    while (p && *p && g_extra_ext_count < (int)(sizeof(g_extra_exts) / sizeof(g_extra_exts[0]))) {
        while (*p == ' ' || *p == '\t' || *p == ',' || *p == ';') ++p;
        if (!*p) break;

        char e[12];
        size_t n = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != ',' && *p != ';' && n + 1 < sizeof(e))
            e[n++] = (char)tolower((unsigned char)*p++);
        e[n] = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != ',' && *p != ';') ++p;   // drop the tail

        while (e[0] == '.') memmove(e, e + 1, strlen(e) + 1);
        if (!e[0]) continue;
        strncpy_s(g_extra_exts[g_extra_ext_count], sizeof(g_extra_exts[0]), e, _TRUNCATE);
        ++g_extra_ext_count;
    }
}

static bool ext_supported(const char* ext)
{
    if (!ext || !*ext) return false;
    for (size_t i = 0; i < sizeof(kResTypes) / sizeof(kResTypes[0]); ++i)
        if (strcmp(kResTypes[i].ext, ext) == 0) return true;
    for (int i = 0; i < g_extra_ext_count; ++i)
        if (strcmp(g_extra_exts[i], ext) == 0) return true;
    return false;
}

static void lower_copy(char* dst, size_t dst_size, const char* src)
{
    size_t i = 0;
    for (; src[i] && i + 1 < dst_size; ++i)
        dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = 0;
}

static const char* find_override(const char* base, const char* ext)
{
    for (int i = 0; i < g_fov_count; ++i)
        if (strcmp(g_fov[i].base, base) == 0 && strcmp(g_fov[i].ext, ext) == 0)
            return g_fov[i].file;
    return nullptr;
}

// Counts a file the loader will not use and logs it (first kSkipLogLines only).
static void skip_log(const char* rel, const char* why)
{
    ++g_fov_skipped;
    if (g_fov_skipped <= kSkipLogLines)
        log_line("override skip: %s (%s)", rel, why);
}

static bool add_override(const char* full, const char* rel)
{
    if (g_fov_count >= FOV_MAX) { ++g_fov_dropped; return false; }

    const char* dot = strrchr(rel, '.');
    if (!dot || dot == rel) { skip_log(rel, "no extension"); return false; }

    size_t base_len = (size_t)(dot - rel);
    if (base_len == 0 || base_len >= sizeof(g_fov[0].base)) { skip_log(rel, "path too long"); return false; }

    char ext[12];
    lower_copy(ext, sizeof(ext), dot + 1);
    if (!ext[0]) { skip_log(rel, "no extension"); return false; }

    if (g_override_whitelist && !ext_supported(ext)) {
        char why[64];
        snprintf(why, sizeof(why), "type .%s is not a game resource", ext);
        skip_log(rel, why);
        return false;
    }

    FileOverride& o = g_fov[g_fov_count];
    for (size_t i = 0; i < base_len; ++i)
        o.base[i] = (char)tolower((unsigned char)rel[i]);
    o.base[base_len] = 0;
    strncpy_s(o.ext, sizeof(o.ext), ext, _TRUNCATE);
    strncpy_s(o.file, sizeof(o.file), full, _TRUNCATE);
    ++g_fov_count;
    log_line("override file: %s (%s)", rel, o.ext);
    return true;
}

// Walks the whole folder even when the table is full, so the report at the end
// says how many files were dropped instead of stopping silently.
static void scan_override_dir(const char* dir, const char* rel, int depth)
{
    if (depth > 8) {
        skip_log(rel, "deeper than 8 levels");
        return;
    }

    char mask[MAX_PATH];
    snprintf(mask, sizeof(mask), "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(mask, &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (fd.cFileName[0] == '.') continue;
        char full[MAX_PATH], child[MAX_PATH];
        snprintf(full, sizeof(full), "%s\\%s", dir, fd.cFileName);
        snprintf(child, sizeof(child), "%s%s%s", rel, rel[0] ? "\\" : "", fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            scan_override_dir(full, child, depth + 1);
        else
            add_override(full, child);
    } while (FindNextFileA(h, &fd));

    FindClose(h);
}

// ---- resource type from the DTI: dti->newInstance() -> cResource::getExt() ----
extern "C" void* vcall_thiscall0(void* obj, void* fn);

extern "C" __attribute__((naked)) void* vcall_thiscall0(void* obj, void* fn)
{
    __asm__ __volatile__(
        "movl 4(%esp), %ecx\n"
        "pushl %ebx\n"
        "movl 12(%esp), %ebx\n"
        "call *%ebx\n"
        "popl %ebx\n"
        "ret\n");
}

static const char* dti_ext(void* dti)
{
    if (!dti) return nullptr;
    void** vt = *(void***)dti;
    if (!vt || !vt[1]) return nullptr;
    // the instance is leaked on purpose: calling the engine destructor while an
    // archive is being mounted is not worth the risk, and it is a few bytes
    void* obj = vcall_thiscall0(dti, vt[1]);
    if (!obj) return nullptr;
    void** ovt = *(void***)obj;
    if (!ovt || !ovt[0x18 / 4]) return nullptr;
    return (const char*)vcall_thiscall0(obj, ovt[0x18 / 4]);
}

// Called from the precreate stub: does this entry have a matching override file?
extern "C" int want_override(void* dti, const char* path)
{
    if (!g_file_overrides_on || !g_fov_count || !path || !*path) return 0;

    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(g_fov[0].base)) return 0;

    char base[160];
    lower_copy(base, sizeof(base), path);

    bool base_found = false;
    for (int i = 0; i < g_fov_count; ++i)
        if (strcmp(g_fov[i].base, base) == 0) { base_found = true; break; }
    if (!base_found) return 0;                    // cheap reject for most entries

    const char* ext = dti_ext(dti);
    if (!ext || !*ext) return 0;

    char e[12];
    lower_copy(e, sizeof(e), ext);
    if (!find_override(base, e)) return 0;

    log_line("override: %s.%s", path, e);
    return 1;
}

// Called from the getFullPath stub with "<...>\<path>.<ext>".
extern "C" void redirect_path(char* dst)
{
    if (!g_file_overrides_on || !g_fov_count || !dst) return;

    static const char kTag[] = "nativePC_MT\\";
    const size_t tag_len = sizeof(kTag) - 1;      // 12
    const char* tail = nullptr;
    size_t n = strlen(dst);
    for (size_t i = 0; i + tag_len <= n; ++i) {
        if (_strnicmp(dst + i, kTag, tag_len) == 0) { tail = dst + i + tag_len; break; }
    }
    if (!tail) {
        const char* bs = strchr(dst, '\\');
        if (bs) tail = bs + 1;
    }
    if (!tail || !*tail) return;

    const char* dot = strrchr(tail, '.');
    if (!dot || dot == tail) return;
    size_t base_len = (size_t)(dot - tail);
    if (base_len == 0 || base_len >= sizeof(g_fov[0].base)) return;

    char base[160], ext[12];
    for (size_t i = 0; i < base_len; ++i)
        base[i] = (char)tolower((unsigned char)tail[i]);
    base[base_len] = 0;
    lower_copy(ext, sizeof(ext), dot + 1);

    const char* file = find_override(base, ext);
    if (!file) return;
    if (strlen(file) >= 250) return;

    static char last[260] = { 0 };
    if (strcmp(last, file) != 0) {
        strncpy_s(last, sizeof(last), file, _TRUNCATE);
        log_line("override read: %s", file);
    }
    strcpy_s(dst, 260, file);
}

// ---------------------------------------------------------------- stubs
struct Emit {
    BYTE* b;
    size_t n;
    Emit(BYTE* buf) : b(buf), n(0) {}
    void u8(int v) { b[n++] = (BYTE)v; }
    void u32(unsigned v) { memcpy(b + n, &v, 4); n += 4; }
    void call_rel(void* t) { u8(0xE8); intptr_t r = (BYTE*)t - (b + n + 4); u32((unsigned)r); }
    void call_ptr(void* pp) { u8(0xFF); u8(0x15); u32((unsigned)(uintptr_t)pp); }
    void jmp_rel(void* t) { u8(0xE9); intptr_t r = (BYTE*)t - (b + n + 4); u32((unsigned)r); }
    void mov_eax_esp(int o) { u8(0x8B); u8(0x44); u8(0x24); u8(o); }
    void mov_ecx_esp(int o) { u8(0x8B); u8(0x4C); u8(0x24); u8(o); }
    void mov_edx_esp(int o) { u8(0x8B); u8(0x54); u8(0x24); u8(o); }
    void push_imm8(int v) { u8(0x6A); u8(v); }
    void add_esp(int v) { u8(0x83); u8(0xC4); u8(v); }
    void test_eax() { u8(0x85); u8(0xC0); }
    void popfd_popad() { u8(0x9D); u8(0x61); }
    void ret_n(int v) { u8(0xC2); u8(v & 0xFF); u8((v >> 8) & 0xFF); }
    size_t je_placeholder() { u8(0x74); size_t at = n; u8(0x00); return at; }
    void fix_je(size_t at, size_t target) { b[at] = (BYTE)((int)target - (int)(at + 1)); }
};

// pushad + pushfd = 0x24 bytes; arguments at [esp+0x28], [esp+0x2C];
// saved ECX (the DTI) at [esp+0x1C].
static void build_precreate_stub(BYTE* out, size_t* out_len, void* tramp)
{
    Emit e(out);
    e.u8(0x60); e.u8(0x9C);

    e.mov_eax_esp(0x2C);          // path
    e.mov_edx_esp(0x1C);          // dti
    e.u8(0x50);                   // push eax (arg2 = path)
    e.u8(0x52);                   // push edx (arg1 = dti)
    e.call_rel((void*)want_override);
    e.add_esp(8);
    e.test_eax();
    size_t je_normal = e.je_placeholder();

    // create(this = sres, dti, path, 1)
    e.mov_eax_esp(0x2C);          // path
    e.mov_edx_esp(0x1C);          // dti
    e.mov_ecx_esp(0x28);          // sres
    e.push_imm8(1);
    e.u8(0x50);                   // push eax (path)
    e.u8(0x52);                   // push edx (dti)
    e.call_ptr(&g_tramp_create);
    e.test_eax();
    size_t je_normal2 = e.je_placeholder();

    // resource must be really loaded (mState & 1), otherwise keep the arc entry
    e.u8(0xF6); e.u8(0x40); e.u8(0x4C); e.u8(0x01);
    size_t je_normal3 = e.je_placeholder();

    e.u8(0x81); e.u8(0xC4); e.u32(0x24);   // add esp,0x24 (drop pushad/pushfd)
    e.ret_n(12);

    size_t normal = e.n;
    e.fix_je(je_normal, normal);
    e.fix_je(je_normal2, normal);
    e.fix_je(je_normal3, normal);
    e.popfd_popad();
    e.jmp_rel(tramp);

    *out_len = e.n;
}

// getFullPath(eax = sResource*, ecx = cResource*, [esp+4] = dst, [esp+8] = flag)
static void build_getfullpath_stub(BYTE* out, size_t* out_len, void* tramp)
{
    Emit e(out);
    e.u8(0x60); e.u8(0x9C);

    e.mov_eax_esp(0x2C);          // flag
    e.mov_edx_esp(0x28);          // dst
    e.u8(0x50);
    e.u8(0x52);
    e.mov_eax_esp(0x28);          // sres (0x20 + 8)
    e.mov_ecx_esp(0x24);          // pr   (0x1C + 8)
    e.call_ptr(&g_tramp_getfullpath);

    e.mov_eax_esp(0x28);          // dst
    e.u8(0x50);
    e.call_rel((void*)redirect_path);
    e.add_esp(4);

    e.popfd_popad();
    e.ret_n(8);

    *out_len = e.n;
}

static BYTE* make_tramp(BYTE* target, size_t patch_len)
{
    BYTE* t = (BYTE*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE,
                                  PAGE_EXECUTE_READWRITE);
    if (!t) return nullptr;
    memcpy(t, target, patch_len);
    t[patch_len] = 0xE9;
    *(DWORD*)(t + patch_len + 1) = (DWORD)(target + patch_len) - ((DWORD)(t + patch_len) + 5);
    return t;
}

static bool patch_entry(BYTE* target, size_t len, BYTE* stub, const char* what)
{
    SuspendedThreads st;
    suspend_other_threads(st);

    DWORD old = 0;
    bool ok = VirtualProtect(target, len, PAGE_EXECUTE_READWRITE, &old) != 0;
    if (ok) {
        BYTE patch[16];
        memset(patch, 0x90, sizeof(patch));
        patch[0] = 0xE9;
        *(DWORD*)(patch + 1) = (DWORD)stub - ((DWORD)target + 5);
        memcpy(target, patch, len);
        FlushInstructionCache(GetCurrentProcess(), target, len);
        VirtualProtect(target, len, old, &old);
    }
    resume_threads(st);

    if (ok) log_line("file overrides: %s hooked at %p", what, (void*)target);
    else    log_line("file overrides: %s patch failed", what);
    return ok;
}

static DWORD WINAPI file_override_thread(LPVOID)
{
    if (GetModuleHandleA("re5_loose_loader.asi")) {
        log_line("file overrides: re5_loose_loader.asi present - skipping (it does the same)");
        return 0;
    }

    if (g_delay_ms > 0) {
        log_line("file overrides: waiting %d ms before patching", g_delay_ms);
        Sleep((DWORD)g_delay_ms);
    }

    static const BYTE sig_create[] = {
        0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x83, 0xEC, 0x0C, 0x8B, 0x45, 0x08
    };
    static const BYTE sig_precreate[] = {
        0x83, 0xEC, 0x08, 0x53, 0x8B, 0x5C, 0x24, 0x10, 0x55, 0x8B, 0x6C, 0x24, 0x18
    };
    static const BYTE sig_getfullpath[] = {
        0x53, 0x8B, 0x5C, 0x24, 0x08, 0x85, 0xDB, 0x56, 0x8B, 0xF1, 0x74, 0x5D
    };

    BYTE* p_create = nullptr, *p_pre = nullptr, *p_gfp = nullptr;
    for (int i = 0; i < 240; ++i) {
        if (!p_create) p_create = scan_for_bytes(sig_create, sizeof(sig_create));
        if (!p_pre)    p_pre    = scan_for_bytes(sig_precreate, sizeof(sig_precreate));
        if (!p_gfp)    p_gfp    = scan_for_bytes(sig_getfullpath, sizeof(sig_getfullpath));
        if (p_create && p_pre && p_gfp) break;
        Sleep(250);
    }
    if (!p_create || !p_pre || !p_gfp) {
        log_line("file overrides: signatures not found (create=%p precreate=%p getFullPath=%p)",
                 (void*)p_create, (void*)p_pre, (void*)p_gfp);
        return 0;
    }

    g_tramp_create = make_tramp(p_create, 6);
    g_tramp_precreate = make_tramp(p_pre, 8);
    g_tramp_getfullpath = make_tramp(p_gfp, 5);
    if (!g_tramp_create || !g_tramp_precreate || !g_tramp_getfullpath) {
        log_line("file overrides: trampoline allocation failed");
        return 0;
    }

    BYTE* stub_pre = (BYTE*)VirtualAlloc(nullptr, 256, MEM_COMMIT | MEM_RESERVE,
                                         PAGE_EXECUTE_READWRITE);
    BYTE* stub_gfp = (BYTE*)VirtualAlloc(nullptr, 256, MEM_COMMIT | MEM_RESERVE,
                                         PAGE_EXECUTE_READWRITE);
    if (!stub_pre || !stub_gfp) {
        log_line("file overrides: stub allocation failed");
        return 0;
    }

    size_t len = 0;
    build_precreate_stub(stub_pre, &len, g_tramp_precreate);
    build_getfullpath_stub(stub_gfp, &len, g_tramp_getfullpath);

    bool ok_pre = patch_entry(p_pre, 8, stub_pre, "precreate");
    bool ok_gfp = patch_entry(p_gfp, 5, stub_gfp, "getFullPath");
    log_line("file overrides: %s (%d file(s))", (ok_pre && ok_gfp) ? "active" : "PARTIAL",
             g_fov_count);
    return 0;
}

// Startup report: what was indexed, what was skipped and why, what did not fit.
static void report_override_scan(const char* dir)
{
    log_line("file overrides: %d/%d file(s) indexed from %s", g_fov_count, FOV_MAX, dir);
    if (g_fov_skipped) {
        log_line("file overrides: %d item(s) not indexed (unsupported type or folder too deep%s)",
                 g_fov_skipped, g_override_whitelist ? "" : ", whitelist off");
        if (g_fov_skipped > kSkipLogLines)
            log_line("file overrides: only the first %d reasons are listed above", kSkipLogLines);
    }
    if (g_fov_dropped)
        log_line("file overrides: %d file(s) DROPPED - the %d-entry limit was hit; "
                 "remove unused files from %s", g_fov_dropped, FOV_MAX, dir);
}

static void init_file_overrides(const char* dir, const char* exe_dir)
{
    (void)exe_dir;
    if (!g_file_overrides_on) {
        log_line("file overrides: disabled by config");
        return;
    }

    strncpy_s(g_fov_dir, sizeof(g_fov_dir), dir, _TRUNCATE);
    strcat_s(g_fov_dir, sizeof(g_fov_dir), "re5_file_overrides");
    CreateDirectoryA(g_fov_dir, nullptr);

    log_line("file overrides: type whitelist %s (%d built-in + %d extra type(s))",
             g_override_whitelist ? "on" : "OFF - any file is indexed",
             (int)(sizeof(kResTypes) / sizeof(kResTypes[0])), g_extra_ext_count);

    scan_override_dir(g_fov_dir, "", 0);
    report_override_scan(g_fov_dir);

    if (!g_fov_count) return;

    HANDLE h = CreateThread(nullptr, 0, file_override_thread, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
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

    char cfg_settings[MAX_PATH] = { 0 }, cfg_chars[MAX_PATH] = { 0 }, cfg_weapons[MAX_PATH] = { 0 };
    bool has_settings = find_config(cfg_settings, sizeof(cfg_settings), dir, exe_dir, kCfgSettings);
    bool has_chars = find_config(cfg_chars, sizeof(cfg_chars), dir, exe_dir, kCfgCharacters);
    bool has_weapons = find_config(cfg_weapons, sizeof(cfg_weapons), dir, exe_dir, kCfgWeapons);

    if (!has_settings && !has_chars) {
        write_default_settings(dir);
        write_default_characters(dir);
        has_settings = find_config(cfg_settings, sizeof(cfg_settings), dir, exe_dir, kCfgSettings);
        has_chars = find_config(cfg_chars, sizeof(cfg_chars), dir, exe_dir, kCfgCharacters);
    }

    if (has_settings) parse_ini(cfg_settings, "settings");
    if (has_chars) parse_ini(cfg_chars, "default");

    init_file_overrides(dir, exe_dir);

    for (int i = 0; i < 60; ++i) {
        if (install_loader_hook()) break;
        Sleep(1000);
    }
    log_line("start: module=%p", (void*)g_code);
    log_line("config: settings=%s", has_settings ? cfg_settings : "(none)");
    log_line("config: characters=%s", has_chars ? cfg_chars : "(none)");
    log_line("config: weapons=%s", has_weapons ? cfg_weapons : "(none)");
    log_line("config: per-class weapon sections=%d", g_char_weapon_count);

    load_override_folder(dir);
    load_weapon_file(has_weapons ? cfg_weapons : nullptr);
    load_weapon_folder(dir);
    if (exe_dir[0] && _stricmp(exe_dir, dir) != 0)
        load_override_folder(exe_dir);

    g_stub = build_stub();
    if (!g_stub) { log_line("no stub memory"); return 0; }

    // Mount-joint sites: retry until the packed image is decrypted.
    for (int i = 0; i < 300; ++i) {
        if (find_bone_sites()) break;
        Sleep(1000);
    }

    // Weapon offsets: wait until the table is filled, then write the config.
    // Break out as soon as the table is ready even if nothing is configured,
    // so a missing re5_ct_weapons.ini does not delay the later hooks by 60 s.
    for (int i = 0; i < 60; ++i) {
        if (apply_weapon_offsets() > 0) break;
        DWORD base = 0;
        if (weapon_table_base(&base)) break;
        Sleep(1000);
    }

    // Per-costume weapon offsets: rewrite the table entry inside the mount
    // function (needs the bone sites to locate it). Also installed when any
    // costume asks for a joint id above 127: that id travels through the same
    // function's joint getter (see the vtable hook), so the hook must run even
    // with no [WPxx] subsections configured.
    if (g_char_weapon_count > 0 || any_wide_bone()) {
        for (int i = 0; i < 30; ++i) {
            if (install_mount_hook()) break;
            Sleep(1000);
        }
    }

    if (g_dump_weapons) {
        for (int i = 0; i < 60; ++i) {
            if (dump_weapon_offsets()) break;
            Sleep(1000);
        }
    }

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
