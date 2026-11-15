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

### 4.4 Enigma and the delivery delay

The retail exe is Enigma-packed and checks its own code integrity at startup.
Patching any of the functions above before that check finishes makes the game
show `Program integrity check failed ... This program will now close`. The hooks
are therefore installed after `delay_ms` (10 s by default), which is late
enough for the check and early enough for character/stage archives (they are
mounted tens of seconds into a level).

### 4.5 Signatures used at runtime

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

### 4.6 Coexistence

If `re5_loose_loader.asi` (the standalone loader) is present in the same folder,
this mod skips its own override hooks — two detours on the same functions would
patch each other's jump and crash. In that configuration the standalone
loader's `re5_overrides\` folder is the active one.

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
