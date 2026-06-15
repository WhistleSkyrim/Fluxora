# Fluxora Virtual File System (USVFS-style)

This is Fluxora's own user-mode virtual file system, built on the same principle
as Mod Organizer 2 / usvfs: **nothing is ever copied into the game folder.** The
mods stay where they are installed (the instance `mods/<mod>` folders) and are
made to *appear* inside the game's data directory only while the game runs, by
hooking the file system inside the game process.

## How it works

```
Fluxora (WPF UI)
        │  "Run"  →  fluxora_launch_game_executable
        ▼
FluxoraCore.dll  ── VirtualFileSystemService
        │  1. builds the load order (enabled mods, ascending priority)
        │  2. writes a tiny JSON descriptor (.flow/vfs/vfs-config.json):
        │       { target: <game>\Data, overwrite, mods:[...], hookDll, logPath }
        │  3. sets FLUXORA_VFS_CONFIG and starts the game with
        │     DetourCreateProcessWithDllEx → FluxoraVfs.dll injected
        ▼
game.exe  (+ every child process: script extender, launchers, tools)
        │  FluxoraVfs.dll (DllMain)
        │  • reads the descriptor
        │  • builds a merged virtual tree of <game>\Data:
        │      real game files  <  mod[0] < mod[1] < … < overwrite
        │  • installs inline hooks (Microsoft Detours) on ntdll:
        │      NtCreateFile, NtOpenFile, NtQueryAttributesFile,
        │      NtQueryFullAttributesFile, NtQueryDirectoryFile(Ex), NtClose
        │  • re-injects itself into child processes (CreateProcessW/A)
        ▼
Windows now reports the merged data directory:
  • opening a modded file is redirected to the real file in mods\…
  • listing a directory returns the merged set (base + mods)
  • new writes land in the writable "overwrite" overlay, never the game folder
```

Priority: a later mod in the load order overrides an earlier one, every mod
overrides the base game file, and the `overwrite` overlay wins over everything.

## Building

The VFS needs an inline-hooking engine. Fluxora uses **Microsoft Detours**,
which is fetched automatically the first time CMake configures the backend
(internet required once; it is then cached under `build/`).

Normal build (VFS on by default):

```powershell
.\Build.ps1
```

Offline / no internet — point CMake at a local Detours checkout:

```powershell
cmake -S backend -B build\backend -DDETOURS_SOURCE_DIR=C:\path\to\Detours
```

Build without the VFS at all (launching then behaves like a plain run):

```powershell
cmake -S backend -B build\backend -DFLUXORA_ENABLE_VFS=OFF
```

`Build.ps1` copies both `FluxoraCore.dll` and `FluxoraVfs.dll` into `Output\`.
They must sit next to each other — the core locates the hook DLL relative to
itself when launching.

## Notes and current limits

* **x64 only.** The hook DLL must match the game's bitness; this build targets
  64-bit games (and the 64-bit Fluxora app). A 32-bit `FluxoraVfs` would be
  needed for 32-bit games.
* The injected DLL uses the **static CRT** so it loads into any game without
  requiring the Visual C++ redistributable.
* A diagnostic log is written per run to `<instance>\.flow\vfs\vfs.log`.
* The merged tree is built once at launch from the active profile's enabled
  mods; changes made in the UI take effect on the next launch.
* Directory-relative opens that cross between two different mod folders are a
  known edge case (most engines open by full path, which is fully handled).
