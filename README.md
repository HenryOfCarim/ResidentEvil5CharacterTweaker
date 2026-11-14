# RE5 Char Tweaks

An ASI mod for **Resident Evil 5 (PC)** that adds **per-costume** tweaks:

- **Back weapon** — force the holstered weapon to be mounted, or disable it.
- **Custom mount joint** — attach the back weapon to any skeleton joint.
- **Chain / chain-collision simulation** — enable `.chn` / `.ccl` for costumes
  whose class resource table has `NULL` slots (Rebecca, Josh, Chris `.ccl`,
  Barry `.ccl`, and custom classes), so files added to the archive are actually
  loaded.

Configuration is keyed by **class name (costume)**, not by character, and mods
can ship per-costume override files without touching the master config.

---

## Features

| Feature | Key | Values |
|---|---|---|
| Back weapon on/off | `back` | `-1` vanilla, `0` never, `1` force |
| Custom mount joint | `custom_bone` + `bone` | `0/1`, joint No `0..127` |
| Chain simulation | `chn` | `0` off, `1` `.chn`, `2` `.ccl`, `3` both |

---

## Requirements

- Windows, 32-bit game (`re5dx9.exe`), RE5 Gold Edition / Steam build.
- **[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)**
  by ThirteenAG — the supported ASI loader. Download the 32-bit `dinput8.dll`
  from its releases and put it in the game root (next to `re5dx9.exe`). It
  loads `.asi` files from the game root and from the `scripts`, `plugins`, and
  `update` folders.
- Works with both the 2018 Steam build and the Enigma-packed retail build:
  the decision check is located at runtime by **signature**, not by a fixed
  address.

---

## Installation

1. Install **Ultimate ASI Loader** (32-bit) into the game root as
   `dinput8.dll` (see above).
2. Copy `dist/re5_char_tweaks.asi` to `scripts\` (or the game root,
   `plugins\`, or `update\`).
3. Copy `dist/re5_char_tweaks.ini` **next to the `.asi`**.
4. Optional: create `re5_char_overrides\` next to the `.asi` for per-mod
   overrides (see below).
5. Remove older test ASIs (`re5_excella_back.asi`, `re5_sheva_bone_test.asi`,
   `re5_chain_slots.asi`) to avoid conflicts.
6. Launch the game.

If the INI is missing, the mod writes a default one (all 30 costumes,
`uPl05ExcellaNormal` with `back=1`). A read-only game folder may prevent that;
copy the shipped INI instead.

---

## Configuration

`re5_char_tweaks.ini` — one section per **class (costume)** plus `[default]`.
Keys not set in a class section fall back to `[default]`.

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

| Character | Classes |
|---|---|
| Chris | `uPl00ChrisNormal`, `uPl00ChrisArmorA/B/F`, `uPl00ChrisPlain`, `uPl00ChrisCos1..4`, `uPl00ChrisEp1` |
| Sheva | `uPl01ShebaNormal`, `uPl01ShebaArmorA/B/F`, `uPl01ShebaPlain`, `uPl01ShebaCos1..4` |
| Jill | `uPl02JillCos1..4` |
| Wesker | `uPl03WeskerCos1..2` |
| Josh | `uPl04JoshCos1..2` |
| Excella | `uPl05ExcellaNormal` |
| Barry | `uPl06BarryNormal` |
| Rebecca | `uPl07RebeccaNormal` |

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
├── re5_char_tweaks.ini
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
4. the mount joint is a 2-byte `push imm8` — `0xC4D8B6` (female branch,
   joint 2) and `0xC4D887` (male branch, joint 117);
5. for `chn`, the class resource table slots 17 (`+0x88`, `rChain`) and 18
   (`+0x90`, `rChainCol`) are filled with the model-name pointer when they are
   `NULL`, which makes the loader request `.chn` / `.ccl` from the archive.

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
start: module=00400000 ini=...\scripts\re5_char_tweaks.ini
override: ...\re5_char_overrides\uPl00ChrisCos3.ini -> [uPl00ChrisCos3]
site found at 00B61AA6
site patched at 00B61AA6
decide: class=uPl00ChrisCos3 type=00000 back=0 custom=1 bone=4 chn=0
chain slots: uPl07RebeccaNormal table=... chn=on ccl=on
```

| Symptom | Cause |
|---|---|
| no log at all | no ASI loader in the game folder |
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
- One instance per process.

---

## Credits

- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
  by ThirteenAG — the supported ASI loader (not bundled with this mod).
- The mechanism was reverse-engineered from `re5dx9.exe` (2018 Steam build)
  and verified in-game. Research notes: [`docs/TECHNICAL.md`](docs/TECHNICAL.md).
