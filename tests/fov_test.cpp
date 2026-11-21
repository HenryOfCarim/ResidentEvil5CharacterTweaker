// Standalone test for the loose-file indexer (whitelist, logging, FOV_MAX).
//
// The indexer is static inside the .asi, so the test pulls dllmain.cpp in and
// calls scan_override_dir() on a throwaway tree: no game, no hooks, no exe.
//
//   g++ -m32 -O1 -o tests\fov_test.exe tests\fov_test.cpp
//   tests\fov_test.exe
//
// Exit code 0 = all cases passed.

#include "../src/dllmain.cpp"

#include <direct.h>
#include <string>

// Fake joint getter for the wide-id thunk: the thunk tail-jumps here with the
// engine's stack layout (ecx = actor, [esp+4] = arg1, [esp+8] = joint).
static int g_seen_joint = 0;
extern "C" void fake_joint_getter(int arg1, int joint)
{
    (void)arg1;
    g_seen_joint = joint;
}

static int g_fail = 0;
static int g_pass = 0;

static void check(bool ok, const char* what)
{
    if (ok) { ++g_pass; printf("  ok   %s\n", what); }
    else    { ++g_fail; printf("  FAIL %s\n", what); }
}

static void check_eq(int got, int want, const char* what)
{
    if (got == want) { ++g_pass; printf("  ok   %s (%d)\n", what, got); }
    else             { ++g_fail; printf("  FAIL %s (got %d, want %d)\n", what, got, want); }
}

static void reset_index()
{
    memset(g_fov, 0, sizeof(g_fov));
    g_fov_count = 0;
    g_fov_skipped = 0;
    g_fov_dropped = 0;
    g_extra_ext_count = 0;
    g_override_whitelist = 1;
}

static void mkdirs(const char* path)
{
    char tmp[MAX_PATH];
    strncpy_s(tmp, sizeof(tmp), path, _TRUNCATE);
    for (char* p = tmp; *p; ++p) {
        if (*p == '\\') {
            *p = 0;
            CreateDirectoryA(tmp, nullptr);
            *p = '\\';
        }
    }
    CreateDirectoryA(tmp, nullptr);
}

static void touch(const char* path)
{
    char dir[MAX_PATH];
    strncpy_s(dir, sizeof(dir), path, _TRUNCATE);
    char* slash = strrchr(dir, '\\');
    if (slash) { *slash = 0; mkdirs(dir); }

    FILE* f = nullptr;
    if (fopen_s(&f, path, "wb") == 0 && f) { fputc('x', f); fclose(f); }
}

// Number of indexed entries with this base path (any extension).
static int count_base(const char* base)
{
    int n = 0;
    for (int i = 0; i < g_fov_count; ++i)
        if (strcmp(g_fov[i].base, base) == 0) ++n;
    return n;
}

static bool log_contains(const char* log_path, const char* needle)
{
    FILE* f = nullptr;
    if (fopen_s(&f, log_path, "rb") != 0 || !f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return false; }
    char* buf = (char*)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return false; }
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = 0;
    fclose(f);
    bool found = strstr(buf, needle) != nullptr;
    free(buf);
    return found;
}

int main()
{
    char root[MAX_PATH];
    snprintf(root, sizeof(root), "%s\\ct_fov_test", getenv("TEMP") ? getenv("TEMP") : ".");

    // Fresh tree + fresh log.
    system(("cmd /c rmdir /s /q \"" + std::string(root) + "\" 2>nul").c_str());
    mkdirs(root);

    char logpath[MAX_PATH];
    snprintf(logpath, sizeof(logpath), "%s\\re5_char_tweaks.log", root);
    DeleteFileA(logpath);
    SetEnvironmentVariableA("TEMP", root);   // log_line() writes into root

    // ---- case 1: whitelist keeps game types, skips the rest ----------------
    printf("case 1: type whitelist\n");
    reset_index();
    touch((std::string(root) + "\\ovr\\pawn\\pl\\pl00\\model\\pl0000.mod").c_str());
    touch((std::string(root) + "\\ovr\\pawn\\pl\\pl00\\model\\pl0000.tex").c_str());
    touch((std::string(root) + "\\ovr\\pawn\\pl\\pl00\\model\\pl0000.chn").c_str());
    touch((std::string(root) + "\\ovr\\pawn\\pl\\pl00\\model\\pl0000.mtg").c_str());
    touch((std::string(root) + "\\ovr\\enemy\\pl\\em00\\model\\em0000.MOD").c_str());  // uppercase
    touch((std::string(root) + "\\ovr\\notes.txt").c_str());
    touch((std::string(root) + "\\ovr\\preview.png").c_str());
    touch((std::string(root) + "\\ovr\\pl0000.mod.bak").c_str());
    touch((std::string(root) + "\\ovr\\backup\\pl0000.mod.orig").c_str());
    // depth 9 -> past the 8-level limit
    touch((std::string(root) + "\\ovr\\a\\b\\c\\d\\e\\f\\g\\h\\i\\deep.mod").c_str());

    scan_override_dir((std::string(root) + "\\ovr").c_str(), "", 0);

    check_eq(g_fov_count, 5, "game files indexed (mod/tex/chn/mtg/uppercase MOD)");
    check_eq(g_fov_skipped, 5, "files skipped (txt, png, .mod.bak, .mod.orig, depth>8)");
    check_eq(g_fov_dropped, 0, "nothing dropped");
    check_eq(count_base("pawn\\pl\\pl00\\model\\pl0000"), 4, "pl0000 indexed for 4 resource types");
    check_eq(count_base("enemy\\pl\\em00\\model\\em0000"), 1, "uppercase .MOD lowered to 'mod'");
    check(find_override("pawn\\pl\\pl00\\model\\pl0000", "mod") != nullptr, "override lookup by (path,type) works");
    check(find_override("pawn\\pl\\pl00\\model\\pl0000", "dds") == nullptr, "no override for an unknown type");
    check(log_contains(logpath, "override skip: notes.txt (type .txt is not a game resource)"),
          "skip reason logged for notes.txt");
    check(log_contains(logpath, "override skip: a\\b\\c\\d\\e\\f\\g\\h\\i (deeper than 8 levels)"),
          "depth limit logged");

    // Startup report lines (what the user reads first).
    report_override_scan("ovr");
    check(log_contains(logpath, "file overrides: 5/256 file(s) indexed from ovr"),
          "report: indexed count");
    check(log_contains(logpath, "file overrides: 5 item(s) not indexed"), "report: skipped count");

    // ---- case 2: ini escape hatches ---------------------------------------
    printf("case 2: file_override_exts / whitelist=0\n");
    reset_index();
    add_extra_exts("PNG, .psd");
    check_eq(g_extra_ext_count, 2, "extra types parsed (leading dot tolerated)");
    check(ext_supported("png") && ext_supported("psd"), "extra types are accepted");
    check(!ext_supported("dds"), "unlisted type still rejected");
    scan_override_dir((std::string(root) + "\\ovr").c_str(), "", 0);
    check_eq(g_fov_count, 6, "preview.png indexed once it is whitelisted");
    check_eq(g_fov_skipped, 4, "one file fewer skipped");

    reset_index();
    g_override_whitelist = 0;
    scan_override_dir((std::string(root) + "\\ovr").c_str(), "", 0);
    check_eq(g_fov_count, 9, "whitelist off indexes every file that has an extension");
    check_eq(g_fov_skipped, 1, "only the too-deep branch is skipped with the whitelist off");

    // ---- case 3: FOV_MAX --------------------------------------------------
    printf("case 3: %d-entry limit\n", FOV_MAX);
    reset_index();
    const int kFiles = FOV_MAX + 40;
    mkdirs((std::string(root) + "\\big\\pawn\\pl\\pl00\\model").c_str());
    for (int i = 0; i < kFiles; ++i) {
        char p[MAX_PATH];
        snprintf(p, sizeof(p), "%s\\big\\pawn\\pl\\pl00\\model\\f%03d.mod", root, i);
        touch(p);
    }
    scan_override_dir((std::string(root) + "\\big").c_str(), "", 0);
    check(g_fov_count == FOV_MAX, "table filled up to FOV_MAX");
    check(g_fov_dropped == kFiles - FOV_MAX, "every file past the limit is counted, scanning continues");
    check(g_fov_skipped == 0, "nothing rejected by the whitelist here");
    report_override_scan("big");
    check(log_contains(logpath, "file overrides: 40 file(s) DROPPED"), "report: dropped-files warning");

    // ---- case 4: the ini actually drives the two new keys -----------------
    printf("case 4: re5_ct_settings.ini wiring\n");
    reset_index();
    {
        char ini[MAX_PATH];
        snprintf(ini, sizeof(ini), "%s\\re5_ct_settings.ini", root);
        FILE* f = nullptr;
        if (fopen_s(&f, ini, "w") == 0 && f) {
            fputs("[settings]\n"
                  "file_overrides=1\n"
                  "file_override_whitelist=0\n"
                  "file_override_exts=dds, .psd\n", f);
            fclose(f);
        }
        parse_ini(ini, "settings");
        check(g_override_whitelist == 0, "file_override_whitelist=0 read from the ini");
        check(g_extra_ext_count == 2, "file_override_exts read from the ini");
        check(ext_supported("dds"), "'dds' now accepted");
    }

    // ---- case 5: the built-in table itself --------------------------------
    printf("case 5: built-in type table\n");
    {
        const int n = (int)(sizeof(kResTypes) / sizeof(kResTypes[0]));
        check_eq(n, 66, "66 built-in types (65 from the DTI table + rev_win)");
        bool dup = false, bad = false;
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j)
                if (strcmp(kResTypes[i].ext, kResTypes[j].ext) == 0) dup = true;
            size_t len = strlen(kResTypes[i].ext);
            if (len == 0 || len > 11) bad = true;                       // ext[12] holds ext+0
            for (size_t k = 0; k < len; ++k)
                if (kResTypes[i].ext[k] != tolower((unsigned char)kResTypes[i].ext[k])) bad = true;
        }
        check(!dup, "no duplicate extension");
        check(!bad, "every entry is lowercase and fits ext[12]");

        // Spot checks against the DTI table: the quirks that my first,
        // inferred version got wrong.
        check(ext_supported("sbc"), "sbc (rCollision) accepted");
        check(ext_supported("bfx"), "bfx (rShader) accepted");
        check(ext_supported("cdf"), "cdf (rCloth) accepted");
        check(ext_supported("sds"), "sds (rSoundDirectionalSet) accepted");
        check(ext_supported("rev") && ext_supported("rev_win"), "both rev spellings accepted");
        check(!ext_supported("shd"), "shd is not an RE5 type");
        check(!ext_supported("mdf") && !ext_supported("gui"), "invented types are gone");
    }

    // ---- case 6: per-frame guards (the stutter fix) -----------------------
    printf("case 6: hot-path guards\n");
    {
        // decide_c() runs every frame; these must never spiral.
        g_bone_write_tick = 0;
        g_bone_write_burst = 0;
        g_bone_write_locked = false;
        int allowed = 0;
        for (int i = 0; i < 9; ++i)
            if (bone_write_allowed()) ++allowed;
        check_eq(allowed, 8, "at most 8 bone writes per 250 ms burst");
        check(g_bone_write_locked, "the write after the burst locks custom bone");
        check(!bone_write_allowed(), "locked = no further writes this session");

        g_bone_fem_addr = 0;
        g_bone_scan_tries = 0;
        g_bone_scan_gave_up = false;
        g_bone_scan_next = 0;
        check(bone_scan_due(), "first discovery attempt is allowed");
        check(!bone_scan_due(), "second attempt inside the 2 s window is refused");

        int tries = 0;
        for (int i = 0; i < 40; ++i) {
            g_bone_scan_next = 0;                       // bypass the rate limit
            if (bone_scan_due()) ++tries;
        }
        check_eq(tries, 19, "discovery attempts are capped (20 total)");
        check(g_bone_scan_gave_up, "discovery gives up instead of scanning forever");
        check(!bone_scan_due(), "after giving up it stays off");
    }

    // ---- case 7: wide bone ids (thunk on the joint getter) ----------------
    printf("case 7: wide bone ids\n");
    {
        // The mount site pushes a sign-extended byte; the thunk on the joint
        // getter maps that byte back to the real id.
        BYTE* thunk = build_bone_thunk();
        check(thunk != nullptr, "thunk generated");
        if (thunk) {
            check(thunk[0] == 0x8B && thunk[1] == 0x54 && thunk[2] == 0x24 &&
                  thunk[3] == 0x08, "starts with `mov edx,[esp+8]`");
            check(thunk[4] == 0x3B && thunk[5] == 0x15, "`cmp edx,[imm32]`");
            check(*(DWORD*)(thunk + 6) == (DWORD)&g_bone_enc,
                  "compares against g_bone_enc");
            check(thunk[10] == 0x75, "`jne` to the pass-through");
            check(thunk[11] == 10, "jne skips exactly the two rewrite insns");
            check(thunk[12] == 0x8B && thunk[13] == 0x15, "`mov edx,[imm32]`");
            check(*(DWORD*)(thunk + 14) == (DWORD)&g_bone_val,
                  "loads g_bone_val");
            check(thunk[18] == 0x89 && thunk[19] == 0x54 && thunk[20] == 0x24 &&
                  thunk[21] == 0x08, "`mov [esp+8],edx`");
            check(thunk[22] == 0xFF && thunk[23] == 0x25, "`jmp [imm32]`");
            check(*(DWORD*)(thunk + 24) == (DWORD)&g_bone_orig_joint,
                  "jumps through g_bone_orig_joint");
        }

        // Behaviour: the thunk is jumped to by the engine with (arg1, joint) on
        // the stack and lands in the original getter by tail jump.
        g_seen_joint = -12345;
        g_bone_orig_joint = (void*)&fake_joint_getter;
        typedef void (*ThunkFn)(int, int);

        set_bone_encoding(220);
        check_eq(g_bone_enc, -36, "220 is pushed as the signed byte -36");
        ((ThunkFn)thunk)(7, g_bone_enc);
        check_eq(g_seen_joint, 220, "encoded byte is translated back to 220");

        g_seen_joint = -12345;
        ((ThunkFn)thunk)(7, -1);
        check_eq(g_seen_joint, -1, "vanilla -1 ('no joint') passes through");

        g_seen_joint = -12345;
        ((ThunkFn)thunk)(7, 117);
        check_eq(g_seen_joint, 117, "117 passes through while 220 is configured");

        set_bone_encoding(120);
        g_seen_joint = -12345;
        ((ThunkFn)thunk)(7, 120);
        check_eq(g_seen_joint, 120, "ids 0..127 are never rewritten");

        set_bone_encoding(128);
        check_eq(g_bone_enc, -128, "128 is pushed as -128");
        set_bone_encoding(254);
        check_eq(g_bone_enc, -2, "254 is pushed as -2");
        set_bone_encoding(255);
        check_eq(g_bone_enc, 0x7FFFFFFF,
                 "255 is not translated (its byte is the -1 'none' sentinel)");
        set_bone_encoding(-1);
        check_eq(g_bone_enc, 0x7FFFFFFF, "reset disables the translation");
    }

    // ---- case 8: the mount hook is installed for wide ids ------------------
    printf("case 8: wide id wiring\n");
    {
        // any_wide_bone() decides whether the mount hook (and with it the joint
        // getter hook) is installed even with no [WPxx] subsections.
        int saved = g_section_count;
        g_section_count = 0;

        Section* s = ensure_section("uTestNarrow");
        s->bone = 120; s->has_bone = 1; s->custom = 1; s->has_custom = 1;
        check(!any_wide_bone(), "narrow-only config does not install the hook");

        s = ensure_section("uTestWide");
        s->bone = 220; s->has_bone = 1; s->custom = 1; s->has_custom = 1;
        check(any_wide_bone(), "bone=220 asks for the mount/joint-getter hook");

        g_section_count = saved;
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}