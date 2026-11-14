Ultimate ASI Loader 6.0.0 (Win32)
Author: ThirteenAG
https://github.com/ThirteenAG/Ultimate-ASI-Loader

Why this version is bundled:
  Newer builds (tested: v10.0.2) crash Resident Evil 5 at startup. The game's
  re5dx9.exe is protected (Steam + Enigma) and the v10 file-overload / DXGI
  hooks break its startup path even when no .asi file is installed.

File:    dinput8.dll
Size:    2197504 bytes
SHA-256: 96450A3158588219392620A194B2E538298230136F59481A3ED98E19435CC0F1
Version: FileVersion 6.0.0, InternalName Ultimate-ASI-Loader-Win32
Build:   WidescreenFixesPack build (PDB path), file date 2023-12-23

Install:
  Copy dinput8.dll to the game root (next to re5dx9.exe).
  Keep re5_char_tweaks.asi in scripts\ (or the game root).

License: MIT (see LICENSE).
