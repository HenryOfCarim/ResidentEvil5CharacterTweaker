# Technical notes

Reverse-engineering notes for the back-weapon mount, the `.chn` / `.ccl`
binding and the loose-file (`.arc` override) mechanism in
**Resident Evil 5 (PC, `re5dx9.exe`)**.

Reference binary: 2018 Steam PC build, 15,046,608 bytes, ImageBase `0x400000`,
`RVA = VA - 0x400000`. The retail game is packed (Steam + Enigma) and the code
is **shifted** relative to this build, so the mod locates the code by signature
at runtime instead of using fixed addresses.

---

## 1. Back-weapon mount

### 1.1 Decision (`shouldMountBackWeapon`)

2018 build: `0xB61F70`. It checks the slot-14 item id and the class type:

```asm
b61f73: push 0x0E
b61f75: lea  0x21A0(%esi),%ecx      ; uPlayer + 0x21A0 = item manager
b61f7b: call 0xC4AE10               ; getSlot(manager, 14)
b61f80: test %eax,%eax
b61f82: je   0xB61FA0               ; empty -> type check
b61f84: mov  0x4(%eax),%eax         ; item id
b61f87: cmp  $0x115,%eax            ; Gatling Gun
b61f8c: je   0xB61F9C               ; -> return 0
b61f8e: cmp  $0x10F,%eax            ; Longbow
b61f93: je   0xB61F9C
b61f95: cmp  $0x121,%eax            ; Flamethrower
b61f9a: jne  0xB61FA0
b61f9c: xor  %al,%al
b61f9e: ret

b61fa0: mov  (%esi),%eax
b61fa2: mov  0x10(%eax),%edx
b61fa5: mov  %esi,%ecx
b61fa7: call *%edx                  ; getDTI()
b61fa9: mov  0x20(%eax),%eax        ; MtDTI type word
b61fac: and  $0xFFFF00,%eax
b61fb1: cmp  $0x500,%eax
b61fb6: setne %al                   ; 1 = mount
b61fb9: pop  %esi
b61fba: ret
```

`MtDTI + 0x20` is filled by the `MtDTI` constructor (`0x6D54B0`) from the 4th
registration argument (`push $0x1000XXX`), or from `crc32(name) & 0x7FFFFFFF`
when that argument is zero. A class whose masked type is `0x500` never gets a
back weapon (Excella is such a class).

### 1.2 Signature used by the mod

```
8B 40 20             mov  eax,[eax+0x20]
25 00 FF FF 00       and  eax,0xFFFF00
3D 00 05 00 00       cmp  eax,0x500
0F 95 C0             setne al          <- patch site (offset +13)
5E                   pop  esi
C3                   ret
```

Observed runtime addresses:

| build | `setne` site |
|---|---|
| 2018 Steam (reference) | `0xB61FB6` |
| retail Enigma-packed | `0xB61AA6` (found by scan; `base+RVA` is zeroed at that moment) |

The mod replaces the 5-byte tail `0F 95 C0 5E C3` with `E9 rel32` to a stub.
The stub runs on the game thread with `ESI = uPlayer*`, `EAX = masked type`,
calls the config resolver, stores `AL`, executes `pop esi; ret`.

### 1.3 Mount joint

The attach function (`0xC4D700` in the reference build, slot 0xE manager call)
does:

```asm
c4d875: cmpb $0x0,0x1AF8(%eax)   ; uPlayer + 0x1AF8
c4d884: jne  0xC4D8B5
c4d886: push $0x75               ; joint 117 (male socket)
...
c4d8b5: push $0x2                ; joint 2 (spine, female)
```

Both are 2-byte `push imm8` instructions, so the joint can be changed in place:

| branch | VA (2018) | bytes | signature used by the mod |
|---|---|---|---|
| joint 117 | `0xC4D886` | `6A 75` | `6A 75 50 FF D2 8D 87 D0 00 00 00` |
| joint 2 | `0xC4D8B5` | `6A 02` | `6A 02 50 FF D2 8D 87 F0 00 00 00` |

The retail Enigma build shifts this code as well (`0xC4D8B6` contains
unrelated bytes `00 3B`), so the mod finds both sites by the signatures above
(they are `0x2F` bytes apart) and rewrites only the immediate byte.

`uPlayer + 0x1AF8`: `0` = joint 117, non-zero = joint 2.

### 1.4 Position / rotation

Per-weapon table: base `0x11A53F0`, index `weaponId & 0xFF`, stride `0x140`.

| offset | meaning |
|---|---|
| `+0xD0` | position (joint 117 branch) |
| `+0xE0` | rotation (joint 117 branch) |
| `+0xF0` | position (joint 2 branch) |
| `+0x100` | rotation (joint 2 branch) |

### 1.5 Per-costume overrides

The table is global, so a costume override must be applied right before the
mount code consumes the entry. The mount function does this (2018 build):

```asm
c4d7f6: cmp  byte [ebp+0xc],1        ; mount flag
c4d7fa: jne  0xC4D931                ; -> nothing mounted
c4d800: mov  eax,[ecx+4]             ; item id         <- patch site (11 bytes)
c4d803: mov  edx,eax
c4d805: and  edx,0xFF00
c4d80b: cmp  edx,0x100               ; weapon item?
c4d817: and  eax,0xFF
c4d81c: lea  edi,[eax+eax*4] ; shl edi,6
c4d822: add  edi,0x11A53F0           ; edi = table entry
c4d88b: lea  eax,[edi+0xD0] ; push ; call 0x588530   ; setPosition
c4d899: add  edi,0xE0       ; push ; call 0x48EE00   ; setRotation
```

At `0xC4D800`:

* `ecx` = the item in slot 16 (`id` at `+4`);
* `edi` = the item manager, i.e. `uPlayer + 0x21A0` (the caller at `0xB70488`
  passes exactly that pointer);
* the table entry is `base + (id & 0xFF) * 0x140`.

The player is *not* taken from `edi` alone: the mount function resolves it itself
a few instructions later, and the mod reuses that call so the retail build's
layout cannot break it:

```asm
c4d83b: mov  ecx,[esi+0x18]          ; esi = [item+0x2c], coord owner
c4d846: push 0x11F34F8               ; key (a data address, shifted in retail)
c4d84b: call 0x61D980                ; -> owner if it owns the key, else 0
c4d850: test eax,eax / je ...
c4d875: cmp  byte [eax+0x1AF8],0     ; gender -> joint 117 / 2
```

`mount_apply` therefore tries, in order:

1. the same lookup on `[[item+0x2C]+0x18]` (target and key are read from the
   code at install time, so the shifted data address does not matter);
2. `class_of_player(edi - 0x21A0)`;
3. the class the decide stub stored last.

The first one that yields a per-costume entry wins.

The mod replaces the 11 bytes `8B 41 04 8B D0 81 E2 00 FF 00 00` with a jump
to a stub that:

1. saves the registers and flags (`pushad`/`pushfd`), calls
   `mount_apply(edi, ecx, [ecx+4])`;
2. `mount_apply` resolves the costume, composes the four vec3s
   (class -> `[default]` -> block value; only the exact class name matches, a
   character-wide section is deliberately unsupported), and writes them to
   `table[id] + 0xD0..+0x10B`;
3. restores the registers, re-executes the original 11 bytes and jumps back to
   `0xC4D80B`.

The first write for a weapon snapshots the 16 floats at `+0xD0`, so a costume
that defines nothing for that weapon gets the previous block back (vanilla, or
a global `re5_ct_weapons.ini` override applied at startup).

The site is located relative to the mount-joint push sites found by signature:
it is `0xB5` bytes before the `6A 02` (joint 2) site, i.e. `0x86` bytes before
the `6A 75` (joint 117) site, which is `0x2F` earlier; if the 11-byte pattern is
not there, a small window in front of the joint-2 site is scanned. The bytes are
re-executed in the stub, so the patch only moves the entry point and does not
depend on the surrounding code layout.

---

## 2. Class resource tables and `.chn` / `.ccl`

Every character class has a getter at vtable slot `+0xA0` returning a static
array of 8-byte entries `{ char* name; MtDTI* type; }`. Entry *name* is the
resource base name and the type supplies the extension (`rChain::getExt` ->
`chn`, `rChainCol::getExt` -> `ccl`).

| slot | offset | type | extension |
|---|---|---|---|
| 17 | `+0x88` | `rChain` | `.chn` |
| 18 | `+0x90` | `rChainCol` | `.ccl` |

If the slot name is `NULL`, the loader never calls `cResourceManager::load`,
so a file dropped into the archive is ignored. Writing the model-name pointer
(slot 0) into the empty slot enables the load.

Tables (2018 build):

| class | table |
|---|---|
| uPl00ChrisNormal | `0x1019C30` |
| uPl00ChrisArmorA/B/F | `0x1019CF0` / `0x1019DB0` / `0x1019E70` |
| uPl00ChrisPlain | `0x1019F30` |
| uPl00ChrisCos1..4 | `0x1019FF0` / `0x101A0B0` / `0x101A170` / `0x101A308` |
| uPl00ChrisEp1 | `0x101A248` |
| uPl01ShebaNormal | `0x101A3E0` |
| uPl01ShebaArmorA/B/F | `0x101A4A0` / `0x101A560` / `0x101A620` |
| uPl01ShebaPlain | `0x101A6E0` |
| uPl01ShebaCos1..4 | `0x101A7A0` / `0x101A860` / `0x101A920` / `0x101A9F8` |
| uPl02JillCos1..4 | `0x101AAD0` / `0x101AB90` / `0x101AC50` / `0x101AD10` |
| uPl03WeskerCos1..2 | `0x101ADD0` / `0x101AE90` |
| uPl04JoshCos1..2 | `0x101B010` / `0x101B0D0` |
| uPl05ExcellaNormal | `0x101B190` |
| uPl06BarryNormal | `0x101B268` |
| uPl07RebeccaNormal | `0x101B328` |

`uChainEx` factory: `0x850D50`; `uChain::setColResource`: `0x612260`;
The mod patches slots 17/18 at resource-load time: it hooks the resource-set
loader (`0xBC2490`, signature `53 55 57 8B 7C 24 10 33 DB 3B FB 8B E9`), reads
the class resource table pointer from the first argument, identifies the class
by the model path in slot 0, and fills the empty slots with that path when the
class's `chn` setting is enabled.
`uChain::setup`: `0x612F80`.

---

## 3. ASI loader

The mod is a 32-bit DLL loaded as `.asi`. The tested loader is **Ultimate ASI
Loader 6.0.0 (Win32)** by ThirteenAG
(<https://github.com/ThirteenAG/Ultimate-ASI-Loader>), bundled as
`dist/loader/dinput8.dll`:

- size `2,197,504` bytes;
- SHA-256 `96450A3158588219392620A194B2E538298230136F59481A3ED98E19435CC0F1`;
- VERSIONINFO: `FileVersion 6.0.0`, `InternalName Ultimate-ASI-Loader-Win32`.

It is installed as `dinput8.dll` in the game root and loads `.asi` files from
the game root and the `scripts` / `plugins` folders.

**Version note:** v10.0.2 crashes RE5 at startup — its new file-overload /
DXGI hooks break the Enigma-packed `re5dx9.exe`, even with no `.asi` loaded.
The 6.0.0 build predates those hooks.

The game imports exactly one function from `DINPUT8.dll`
(`DirectInput8Create`), so any proxy-based ASI loader using that name can work,
provided it does not hook the packed executable's startup path.

---

## 4. File overrides (loose files)

Goal: a file placed outside the archives (`re5_file_overrides\<archive path>.<ext>`)
replaces the matching `.arc` entry, for any file size, without repacking.

### 4.1 Why a loose file cannot win on its own

When an `.arc` is mounted, `rArchive::load` (2018 build `0x626BC0`) parses the
whole TOC and registers **every** entry in the `sResource` hash table:

```
rArchive::load -> entryDecode (0x455120) -> precreate (0x454490) -> registTable (0x454F90)
```

`precreate` also marks the object as archive-backed (`mAttr |= 0x80`) and stores
the entry offset/size. Later `sResource::create` (`0x4547C0`) does
`makeID` -> `findTable` and returns that object; the disk path is only built
when the lookup misses. A loose file is therefore just a *fallback* for
resources that are not in the table (which is why "stub arcs" work), and the
priority depends on who registered the id first.

### 4.2 The two detours

```
sResource::precreate   (0x454490)  ecx = DTI, [esp+4] = sResource*,
                                   [esp+8] = TOC path (no extension)
sResource::getFullPath (0x454420)  eax = sResource*, ecx = cResource*,
                                   [esp+4] = dst, [esp+8] = short-path flag
```

* `precreate` — if the entry has a matching override file, the archive entry is
  **not** registered. Instead the resource is loaded from disk with
  `create(dti, path, 1)` (`0x4547C0`, mode 1 = "load from HDD") and that object
  is returned to `entryDecode`. It is already registered and has `mState |= 1`,
  so `executeDecode` skips the archive data.
* `getFullPath` — the original builds `"%s\%s.%s"` (`0x10AEA5C`) from
  `mResourcePath` + `pr->mPath` + `getExt()`. A post-processing stub rewrites
  the result to `<asi dir>\re5_file_overrides\<path>.<ext>` when a matching
  file exists, so nothing has to be copied into `nativePC_MT`.

### 4.3 Getting the resource type right

The TOC path has **no extension**; the type comes from the DTI. It must be
resolved exactly or every resource sharing the base name is rebound:

```c
void** vt = *(void***)dti;
void*  obj = vcall_thiscall0(dti, vt[1]);        // dti->newInstance()
void** ovt = *(void***)obj;
const char* ext = vcall_thiscall0(obj, ovt[6]);  // cResource::getExt() @ +0x18
```

Without this check, an override for `pl0000.mod` also rebinds `pl0000.chn`,
`pl0000.ccl`, `pl0000.mtg` … For each of them `create(...)` is called, and a
missing `.chn` is fatal: the engine exits with

```
Failed open file. ...\nativePC_MT\pawn\pl\pl00\model\pl0000.chn 0
```

Additional guard: after `create` returns, `mState & 1` (`cResource + 0x4C`) is
checked; if the resource did not actually load, the archive entry is used as
before (a broken override is not fatal).

### 4.4 The index built at startup

Both detours need an O(1) answer to "does this `(path, type)` have a file?", so
the folder is walked once in `init_file_overrides` (`FindFirstFile`, depth ≤ 8,
names starting with `.` ignored) and every kept file becomes one entry:

```c
struct FileOverride { char base[160]; char ext[12]; char file[MAX_PATH]; };
static FileOverride g_fov[256];      // FOV_MAX
```

`base` is the relative path without its extension, `ext` the extension, both
lowercased — Windows is case-insensitive, the TOC paths differ in case between
builds. Lookups are a linear scan (`find_override`) over at most 256 entries,
which is nothing next to a TOC pass.

Two filters apply while indexing, because the *presence* of a file is not free:

* **type whitelist** (`kResTypes`, `file_override_whitelist`) — only extensions
  the engine can ask for, taken from the game's own DTI table (below).
  Previews, backups (`pl0000.mod.bak`), source `.dds`/`.psd` and readme files
  are rejected here; `file_override_exts` adds types,
  `file_override_whitelist=0` disables the filter (old behaviour: everything
  indexed);
* **entry limit** (`FOV_MAX`, 256) — the walk continues after the table is
  full so the startup report can say how many files were dropped instead of
  silently truncating mid-tree.

Every decision is logged (`override file:` / `override skip: <rel> (<why>)`,
per-file lines capped at 64, then totals). The scan result decides whether the
hooks are installed at all: an empty folder means no detours are written, so a
folder that was empty at startup is only picked up on the next run.

### 4.4.1 RE5 resource types

The `(type id -> extension -> class)` table, i.e. what `getExt()` returns for
every class the game can instantiate. `sResource::precreate` only sees the TOC
path (no extension), so this table is the only way to tell `pl0000.mod` from
`pl0000.chn` for the same base path.

| ext | class | ext | class |
|---|---|---|---|
| `.ccl` | `rChainCol` | `.cmi` | `sCubeMapInterp::rCubeMapInterp` |
| `.lom` | `rLotSelectorMove` | `.mtg` | `rModelMontage` |
| `.hit` | `rHit` | `.ean` | `rEffectAnim` |
| `.efs` | `rEffectStrip` | `.nav` | `rNavigationMesh` |
| `.sds` | `rSoundDirectionalSet` | `.hri` | `rHkRBInfoList` |
| `.rvt` | `rSoundReverbTable` | `.bfx` | `rShader` |
| `.ase` | `rSoundAttributeSe` | `.sbc` | `rCollision` |
| `.oba` | `rObjAdj` | `.shp` | `rShape` |
| `.scs` | `rSoundCurveSet` | `.eng` | `rSoundEngine` |
| `.msg` | `rMessage` | `.aef` | `rAHEffect` |
| `.lot` | `rLayout` | `.ahc` | `rAHCamera` |
| `.stq` | `rSoundStreamRequest` | `.pos` | `rPos` |
| `.los` | `rLotSelector` | `.bgm` | `rBioBgmMap` |
| `.lnk` | `sLightLinker::rLightLinker` | `.mod` | `rModel` |
| `.nck` | `rNeck` | `.way` | `rAIWayPoint` |
| `.srq` | `rSoundRequest` | `.shw` | `rShapeWeight` |
| `.glp` | `rGoalPos` | `.lsp` | `rLayoutSpr` |
| `.adh` | `rAdhesion` | `.sce` | `rScenario` |
| `.jex` | `rJointEx` | `.fsm` | `rAIFSM` |
| `.rev` | `rSoundReverb` (`.rev_win` on some platforms) | `.stp` | `rStartPos` |
| `.tex` | `rTexture` | `.efl` | `rEffectList` |
| `.idm` | `sId::rIdMapList` | `.ahs` | `rAreaHitShape` |
| `.lku` | `rLinkUnit` | `.cef` | `rCommonEffect` |
| `.e2d` | `rEffect2D` | `.lmt` | `rMotionList` |
| `.equ` | `rSoundEQ` | `.rtex` | `rRenderTargetTexture` |
| `.smh` | `rScrHiding` | `.sngw` | `rSoundSourceMusic` |
| `.srd` | `rSoundRandom` | `.spc` | `rSoundPackage` |
| `.cdf` | `rCloth` | | |
| `.pth` | `rPath` | | |
| `.vib` | `rVibration` | | |
| `.hkx` | `rHkCollision` | | |
| `.seg` | `rSoundSeGenerator` | | |
| `.lcm` | `rCameraList` | | |
| `.chn` | `rChain` | | |
| `.ptl` | `rPathList` | | |
| `.egv` | `rSoundEngineValue` | | |
| `.sdl` | `rScheduler` | | |
| `.mse` | `rSoundMotionSe` | | |

`kResTypes` in `dllmain.cpp` holds the same 65 entries plus `.rev_win`. Note
that the extension is *not* a reliable hint about the content: `.sbc` is
collision (`rCollision`), not sound, and `.sdl` is the scheduler, not a shader
(shaders are `.bfx`). Types are matched exactly — the game asks for one of
these strings, and a file named with anything else is never read.

### 4.5 Enigma and the delivery delay

The retail exe is Enigma-packed and checks its own code integrity at startup.
Patching any of the functions above before that check finishes makes the game
show `Program integrity check failed ... This program will now close`. The hooks
are therefore installed after `delay_ms` (10 s by default), which is late
enough for the check and early enough for character/stage archives (they are
mounted tens of seconds into a level).

### 4.6 Signatures used at runtime

```
sResource::create       55 8B EC 83 E4 F8 83 EC 0C 8B 45 08
sResource::precreate    83 EC 08 53 8B 5C 24 10 55 8B 6C 24 18
sResource::getFullPath  53 8B 5C 24 08 85 DB 56 8B F1 74 5D
sResource::releaseTable 53 55 8B 6C 24 10 8B 5D 5C 56 57 8B 7D 58   (reload only)
rArchive::load          55 8B EC 83 E4 F8 83 EC 1C 53 56 8B 75 08
```

Observed on the Enigma-packed retail build: `create` `0x448B70`, `precreate`
`0x448840`, `getFullPath` `0x4487D0`, `rArchive::load` `0x40FB50` — i.e. the
2018-build addresses are shifted, so only the signatures are reliable.

### 4.7 Coexistence

If `re5_loose_loader.asi` (the standalone loader) is present in the same folder,
this mod skips its own override hooks — two detours on the same functions would
patch each other's jump and crash. In that configuration the standalone
loader's `re5_overrides\` folder is the active one.

### 4.8 Hot-path budget (stutter with the Ultimate Trainer)

`decide_c` is reached from a code site the game evaluates for every player,
every frame. Two things used to run unbounded there and produced visible
stutter as soon as another tool touched the same bytes:

* `find_bone_sites()` — a full address-space walk (`VirtualQuery` +
  `ReadProcessMemory` in 64 KB chunks). It was retried on every call while
  `g_bone_fem_addr` was still 0, so if the mount signature never matched
  (trainer patched it, or a foreign build) the game rescanned its whole memory
  60×/s. Now: image-scope scan first (the mount code is in `re5dx9.exe`, so
  ~its `SizeOfImage` instead of every executable region), one attempt per 2 s,
  at most 20 attempts, then a single log line and `custom_bone` off.
* `write_bone_bytes()` / `reset_bone_bytes()` — each change did
  `VirtualProtect` + *suspend every thread* + write + `FlushInstructionCache`.
  Costume switching alone is fine, but two writers (trainer "Handedness" /
  "Weapon on back", or co-op with two costumes asking for different joints)
  turn it into an every-frame stop-the-world. Now: byte stores are plain atomic
  writes (no thread suspension — the patched byte is an immediate operand of
  `push imm8`, so a torn state is not possible), and writes are capped at 8 per
  250 ms window; exceeding it locks `custom_bone` for the session with a log
  line naming the reason.

The process check for `RE5GEUltimateTrainer.exe` (Toolhelp32) exists only to
explain the symptom in the log — the trainer is 64-bit and separate, so it is
not a module and cannot be detected by `GetModuleHandle`.

---

## 5. Caveats

- All fixed addresses above are for the 2018 Steam build; the retail protected
  build differs. The mod relies on the signature and on the resource-table
  pointers only.
- The `.chn`/`.ccl` files must exist in the archive under the model's base name;
  the mod only fills the slots.
- A `.chn` references joints by `ref_joint_no`; a chain from another character
  may not fit.
- File overrides are keyed by the archive-internal path, so costumes stored in
  different `.arc` files under the same path (`pawn\pl\pl00\model\pl0000`)
  are all overridden. For a single costume, repack that `.arc`.
- Resources the game never requests through `sResource::create` cannot be
  overridden by a hook; they still need repacking.
