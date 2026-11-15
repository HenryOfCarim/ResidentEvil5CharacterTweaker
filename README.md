# RE5 Char Tweaks

An ASI mod for **Resident Evil 5 (PC)** that adds **per-costume** tweaks:

- **Back weapon** — force the holstered weapon to be mounted, or disable it.
- **Custom mount joint** — attach the back weapon to any skeleton joint.
- **Chain / chain-collision simulation** — enable `.chn` / `.ccl` for costumes
  whose class resource table has `NULL` slots (Rebecca, Josh, Chris `.ccl`,
  Barry `.ccl`, and custom classes), so files added to the archive are actually
  loaded.
- **File overrides (loose files)** — drop unpacked files into
  `re5_file_overrides\` and they replace the matching `.arc` entries (any file
  size, no repacking).

Configuration is keyed by **class name (costume)**, not by character, and mods
can ship per-costume override files without touching the master config.

---

## Features

| Feature | Key | Values |
|---|---|---|
| Back weapon on/off | `back` | `-1` vanilla, `0` never, `1` force |
| Custom mount joint | `custom_bone` + `bone` | `0/1`, joint No `0..127` |
| Chain simulation | `chn` | `0` off, `1` `.chn`, `2` `.ccl`, `3` both |
| Dump weapon offsets | `dump_weapons` | `1` writes `%TEMP%\re5_weapon_offsets.txt` |
| File overrides | `file_overrides` | `1` on, `0` off (files from `re5_file_overrides\`) |

---

## Requirements

- Windows, 32-bit game (`re5dx9.exe`), RE5 Gold Edition / Steam build.
- **[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
  6.0.0 (Win32)** by ThirteenAG — the tested and recommended loader. The
  working binary is bundled in `dist/loader/dinput8.dll`
  (2,197,504 bytes, SHA-256 `96450A3158588219392620A194B2E538298230136F59481A3ED98E19435CC0F1`).
  Put it in the game root (next to `re5dx9.exe`).
  **Do not use v10.0.2** — its new file-overload/DXGI hooks crash RE5 at
  startup (the exe is Enigma-packed), even with no `.asi` installed.
- Works with both the 2018 Steam build and the Enigma-packed retail build:
  the decision check is located at runtime by **signature**, not by a fixed
  address.

---

## Installation

1. If the game folder has no working ASI loader, copy
   `dist/loader/dinput8.dll` to the game root (next to `re5dx9.exe`).
2. Copy `dist/re5_char_tweaks.asi` to `scripts\` (or the game root or
   `plugins\`; the bundled 6.0.0 loader scans those folders).
3. Copy the three config files **next to the `.asi`**:
   `re5_ct_settings.ini` (global switches), `re5_ct_characters.ini`
   (character/costume table) and `re5_ct_weapons.ini` (weapon table).
4. Optional: create `re5_char_overrides\` / `re5_weapon_overrides\` next to
   the `.asi` for per-mod overrides (see below).
6. Optional: create `re5_file_overrides\` next to the `.asi` for loose file
   overrides of `.arc` entries (see "File overrides" below). If the standalone
   `re5_loose_loader.asi` is installed in the same folder, this mod skips its
   own file-override hooks — use that loader's `re5_overrides\` folder instead.
7. Remove older test ASIs (`re5_excella_back.asi`, `re5_sheva_bone_test.asi`,
   `re5_chain_slots.asi`) to avoid conflicts.
8. Launch the game.

If the config files are missing, the mod writes defaults for the settings and
the character table (all 30 costumes, `uPl05ExcellaNormal` with `back=1`); the
weapon table is optional and is used only when present. A read-only game folder
may prevent writing; copy the shipped files instead.

Old installations are still supported: `re5_char_tweaks.ini` and
`re5_weapon_offsets.ini` are read when the new files are absent.

---

## Configuration

Three files, all lowercase snake_case with the `re5_ct_` prefix, all next to
the `.asi`:

| File | Purpose |
|---|---|
| `re5_ct_settings.ini` | global switches: `file_overrides`, `delay_ms`, `dump_weapons` |
| `re5_ct_characters.ini` | one section per **class (costume)** plus `[default]` |
| `re5_ct_weapons.ini` | one section per **weapon** (`[WPxx]`), mount offsets |

The three drop-in folders keep their established names, because other mods
ship files for them: `re5_char_overrides\`, `re5_weapon_overrides\`,
`re5_file_overrides\`.

`re5_ct_settings.ini`:

```ini
[settings]
file_overrides=1   ; 1 = re5_file_overrides\ overrides .arc entries
delay_ms=10000     ; wait before patching (Enigma integrity check)
dump_weapons=1     ; write %TEMP%\re5_weapon_offsets.txt
```

`re5_ct_characters.ini` — one section per class plus `[default]`; keys not set
in a class section fall back to `[default]`:

```ini
[default]
back=-1
custom_bone=0
bone=0
chn=0

[uPl05ExcellaNormal]
back=1

[uPl00ChrisCos3]
back=0
custom_bone=1
bone=4
chn=0
```

### Class list

Each section in the INI is prefixed with the costume name as a comment.

| Character | Class | Costume |
|---|---|---|
| Chris | `uPl00ChrisNormal` | BSAA (no vest) |
| Chris | `uPl00ChrisArmorA` | BSAA + melee vest |
| Chris | `uPl00ChrisArmorB` | BSAA + bullet vest |
| Chris | `uPl00ChrisArmorF` | BSAA + melee + bullet vests |
| Chris | `uPl00ChrisPlain` | jacket (start of the game) |
| Chris | `uPl00ChrisCos1` | Safari |
| Chris | `uPl00ChrisCos2` | S.T.A.R.S. |
| Chris | `uPl00ChrisCos3` | Warrior |
| Chris | `uPl00ChrisCos4` | Heavy Metal |
| Chris | `uPl00ChrisEp1` | Lost in Nightmares |
| Sheva | `uPl01ShebaNormal` | BSAA (no vest) |
| Sheva | `uPl01ShebaArmorA` | BSAA + melee vest |
| Sheva | `uPl01ShebaArmorB` | BSAA + bullet vest |
| Sheva | `uPl01ShebaArmorF` | BSAA + melee + bullet vests |
| Sheva | `uPl01ShebaPlain` | basic gear (start of the game) |
| Sheva | `uPl01ShebaCos1` | Tribal |
| Sheva | `uPl01ShebaCos2` | Clubbin' |
| Sheva | `uPl01ShebaCos3` | Business |
| Sheva | `uPl01ShebaCos4` | Fairy Tale |
| Jill | `uPl02JillCos1` | BSAA |
| Jill | `uPl02JillCos2` | Battle Suit |
| Jill | `uPl02JillCos3` | Desperate Escape |
| Jill | `uPl02JillCos4` | Lost in Nightmares |
| Wesker | `uPl03WeskerCos1` | Midnight |
| Wesker | `uPl03WeskerCos2` | S.T.A.R.S. |
| Josh | `uPl04JoshCos1` | Mercenaries Reunion (unused Chris LiN) |
| Josh | `uPl04JoshCos2` | Desperate Escape |
| Excella | `uPl05ExcellaNormal` | Mercenaries Reunion |
| Barry | `uPl06BarryNormal` | Mercenaries Reunion |
| Rebecca | `uPl07RebeccaNormal` | Mercenaries Reunion |

Useful joint numbers (Sheva, `pl0100.mod`): `2` spine, `4` neck, `100` face,
`19`/`22` feet, `18`/`21` shins.

---

## Per-mod overrides

The mod creates/reads a folder **`re5_char_overrides`** next to the `.asi`.
Each file is named after the class and contains plain keys (a `[class]`
header is also accepted):

```
scripts\
├── re5_char_tweaks.asi
├── re5_ct_settings.ini
├── re5_ct_characters.ini
├── re5_ct_weapons.ini
└── re5_char_overrides\
    └── uPl00ChrisCos3.ini
```

`uPl00ChrisCos3.ini`:

```ini
back=0
custom_bone=1
bone=4
```

Override files are loaded **after** the master config, in alphabetical order;
later files win. A mod that replaces a single costume only needs to ship that
one file.

---

## How it works

The game decides whether to mount a holstered weapon in a small function that
reads the class type word from the class DTI and compares it with `0x500`:

```asm
mov  eax,[eax+0x20]      ; MtDTI::type
and  eax,0FFFF00h
cmp  eax,500h
setne al                 ; 1 = mount
pop  esi
ret
```

The mod:

1. finds that code by its **18-byte signature** (the retail Enigma-packed
   build has the code shifted by ~0x510 vs. the 2018 build, so fixed addresses
   are not reliable);
2. replaces the 5-byte tail (`setne al; pop esi; ret`) with a jump into a
   stub;
3. the stub identifies the costume (class resource-table pointer from vtable
   slot `+0xA0`, falling back to the DTI class name), reads the INI, writes
   the custom joint bytes if requested, and returns the desired `AL`;
4. the mount joint is a 2-byte `push imm8` (`6A 02` on the female branch,
   `6A 75` on the male branch). The retail Enigma build shifts this code too,
   so both sites are located by signature at runtime
   (`6A 02 50 FF D2 8D 87 F0 …` / `6A 75 50 FF D2 8D 87 D0 …`, 0x2F apart)
   and only the immediate byte is rewritten;
5. for `chn`, the class resource table slots 17 (`+0x88`, `rChain`) and 18
   (`+0x90`, `rChainCol`) are filled at **resource-load time**: the mod hooks
   the resource-set loader (`0xBC2490`), reads the table pointer from its first
   argument, identifies the class by the model path in slot 0 and writes that
   path into the empty slots, so the loader requests `.chn` / `.ccl` from the
   archive.

File overrides are a separate pair of detours (details in
[`docs/TECHNICAL.md`](docs/TECHNICAL.md) §4): `sResource::precreate` is stopped
from registering the archive entry and the resource is loaded from disk
instead, and `sResource::getFullPath` is redirected to `re5_file_overrides\`.
The resource type is resolved from the DTI, so only the matching
`(path, type)` is replaced, and the hooks are installed after the Enigma
integrity check (`delay_ms`).

### Weapon mount offsets (advanced)

The position/rotation of a holstered weapon relative to its joint is stored per
**weapon**, in a global table:

```
0x11A53F0 + (weaponId & 0xFF) * 0x140
  +0xD0  position vec3   +0xE0  rotation vec3 (degrees)   joint 117 branch
  +0xF0  position vec3   +0x100 rotation vec3 (degrees)   spine branch
```

The mount function applies them with `uCoord::setPosition` / `setRotation`
after `setParent(model, player, joint)`. The table is zero in the exe file
(BSS) and is filled at startup by the weapon-parameter constructor
(`0x7F2DB0`, 144 call sites with hardcoded constants), so it cannot be edited
through the `.arc` files. To inspect the current values, set
`dump_weapons=1` in `[default]`; the mod writes
`%TEMP%\re5_weapon_offsets.txt` with every valid entry (id, name, the four
vec3s).

### Weapon offsets file

`re5_ct_weapons.ini` has one section per weapon (`[WP00]` … `[WP8F]`) with
the four vec3s copied from the dumped defaults:

```ini
[WP0F]
pos117=0.000,0.000,0.000
rot117=0.000,0.000,0.000
pos2=-5.000,7.000,-13.000
rot2=-0.838,-1.396,-1.937
```

`pos117`/`rot117` are used by the joint-117 branch (male rigs), `pos2`/`rot2`
by the spine branch (female rigs). Position is x,y,z in game units, rotation is
x,y,z in radians.

Per-mod overrides follow the same pattern as the costumes: a file named after
the weapon in `re5_weapon_overrides\`, with plain keys:

```
scripts\re5_weapon_overrides\WP0F.ini
```

```ini
pos2=-5,7,-13
rot2=-0.838,-1.396,-1.937
```

Overrides are loaded after the master file, alphabetically; later files win.
The values are written into the runtime table once the game has filled it.

### File overrides (loose files)

Drop files into `re5_file_overrides\` next to the `.asi`, keeping the
**internal path from the archive TOC** (the path ARCtool shows inside
`Image\Archive\<ArcName>\`, without that prefix):

```
scripts\re5_file_overrides\
├── pawn\pl\pl00\model\pl0000.mod
├── pawn\pl\pl00\model\pl0000.mtg
├── pawn\pl\pl00\motion\pl00action.lmt
└── id\jpn\merce\merce_11_360_NOMIP_BM.tex
```

The file replaces the archive entry of the same **(path, resource type)**,
whatever its size, and the `.arc` files are not modified.

How it works (retail build, `re5dx9.exe` 1.8.2x):

* When an `.arc` is mounted, `rArchive::load` parses the whole TOC and registers
  every entry in the `sResource` hash table (`precreate` -> `registTable`).
  After that `sResource::create` finds the entry and never touches the disk, so
  a loose file could never win on its own.
* The mod hooks `sResource::precreate` (`0x448840`): if the entry has a matching
  file, the archive entry is **not** registered — instead the resource is loaded
  from disk with `create(dti, path, 1)` and that object is returned (registered
  and marked loaded, so the archive decoder skips it).
* It also hooks `sResource::getFullPath` (`0x4487D0`) to rewrite
  `nativePC_MT\<path>.<ext>` to `<asi dir>\re5_file_overrides\<path>.<ext>`.

The resource type is resolved exactly from the DTI
(`dti->newInstance()->getExt()`): `rModel` -> `.mod`, `rModelMontage` -> `.mtg`,
`rJointEx` -> `.jex`, `rTexture` -> `.tex`, `rMotionList` -> `.lmt`,
`rChain` -> `.chn`, `rChainCol` -> `.ccl`, `rEffectList` -> `.efl`, ... A file
with a different extension will not be picked up. Path case does not matter.

Notes:

* the hooks are installed **~10 s after start** (`delay_ms`): the retail exe is
  Enigma-protected and patching the code before its integrity check finishes
  makes the game exit with "Program integrity check failed";
* one internal path = one resource: costumes stored in different `.arc` files
  under the same path (`pawn\pl\pl00\model\pl0000`) are all overridden — for a
  single costume, repack that `.arc`;
* a broken file is not fatal: if `create` cannot load it, the archive entry is
  used as before;
* if `re5_loose_loader.asi` is installed in the same folder, this mod skips its
  own file-override hooks (the standalone loader does the same job).

Config keys (in `re5_ct_settings.ini`):

```ini
file_overrides=1   ; 0 = disable the file-override hooks
delay_ms=10000     ; how long to wait before patching code (Enigma check)
```

See [`docs/TECHNICAL.md`](docs/TECHNICAL.md) for the address table and the
research notes.

---

## Building

Requires **TDM-GCC 32-bit** (or any MinGW-w64 i686 toolchain).

```bat
build.bat
```

or manually:

```bat
g++ -m32 -shared -O2 -static-libgcc -static-libstdc++ -o dist\re5_char_tweaks.asi src\dllmain.cpp
```

Output:

- `dist/re5_char_tweaks.asi`

---

## Troubleshooting

The mod logs to **`%TEMP%\re5_char_tweaks.log`**:

```
start: module=00400000
config: settings=...\scripts\re5_ct_settings.ini
config: characters=...\scripts\re5_ct_characters.ini
config: weapons=...\scripts\re5_ct_weapons.ini
override: ...\re5_char_overrides\uPl00ChrisCos3.ini -> [uPl00ChrisCos3]
site found at 00B61AA6
site patched at 00B61AA6
decide: class=uPl00ChrisCos3 type=00000 back=0 custom=1 bone=4 chn=0
chain slots: uPl07RebeccaNormal table=... chn=on ccl=on
```

| Symptom | Cause |
|---|---|
| no log at all | no ASI loader in the game folder, or a loader that crashes RE5 (v10.0.2) |
| crash at startup with no log | loader v10.0.2 — use the bundled 6.0.0 `dinput8.dll` |
| `done (patched=0)` + `site found` missing | the signature was not found — send the log |
| `tail mismatch at ...` | the site bytes differ in your build — send the log |
| `class=` empty / `?` | class identification failed — send the log |
| `chain slots:` missing for `chn=3` | the table was not ready yet, or the class has no model path |

If the log is missing, check that Ultimate ASI Loader is installed and that the
`.asi` is in a folder it scans (game root, `scripts`, `plugins`, `update`).

---

## Known limitations

- `chn` only makes the engine **request** the file; the `.chn`/`.ccl` must
  exist in the costume's archive under the model base name.
- `.ccl` alone does nothing — chain collision is attached to the chain unit
  created by `.chn`.
- A foreign `.chn` may not fit the skeleton (`ref_joint_no`).
- Single-player oriented; co-op with two different costumes/configs is not
  tested.
- Not compatible with other mods that hook the same decision site or patch the
  same resource-table slots.
- File overrides are keyed by the archive-internal path: costumes stored in
  different `.arc` files under the same path are all overridden. For a single
  costume, repack that `.arc`.
- Resources the game does not request through `sResource::create` cannot be
  overridden by a hook; those still need repacking.
- No in-game reload: an overridden file is read when the resource is loaded
  (level start / spawn), so replacing it requires a reload of that resource
  (the standalone `re5_loose_loader.asi` adds a warm-reload helper).
- One instance per process.

---

## Credits

- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
  by ThirteenAG — the supported ASI loader (not bundled with this mod).
- Costume names follow the community **RE5 GE - ARC LIST**
  ([residentevilmodding.boards.net](https://residentevilmodding.boards.net/thread/3989/re5-ge-arc-list)).
- The mechanism was reverse-engineered from `re5dx9.exe` (2018 Steam build)
  and verified in-game. Research notes: [`docs/TECHNICAL.md`](docs/TECHNICAL.md).
