# Fluxora Mod Manager

Fluxora is a desktop mod manager built as part of the ModdingFlow ecosystem. The project is split into a native C++ backend for core modding functionality and a C# WPF frontend for the user interface.

## Tech Stack

- **C++ Core**: native backend layer for hooks, mod loading, and low-level integration.
- **C# WPF App**: desktop frontend for managing mods, settings, and user workflows.

## Project Structure

```text
Fluxora/
├── backend/                # C++ core project
│   ├── include/            # Public backend headers
│   └── src/                # Backend implementation files
├── frontend/               # C# WPF application
│   ├── Models/             # UI data models
│   ├── Services/           # Frontend services and native bridge layer
│   └── ViewModels/         # WPF view models
├── Icons/                  # Application icons and UI icon assets
├── LICENSE
└── README.md
```

## Backend

The backend is the native core of the mod manager. It is responsible for features such as:

- hook lifecycle management;
- mod discovery and registration;
- native logging;
- future integration points for game-specific mod loaders.

The backend CMake project lives in `backend`.

## Frontend

The frontend is a WPF application that provides the visual interface for Fluxora. It currently includes starter services for:

- application settings;
- installed mod catalog management;
- communication with the native C++ core.

The WPF project lives in `frontend`.

## Уникальные функции

- Перенос готовой сборки из Mod Organizer 2 в Fluxora, чтобы продолжить работу с ней в нашей программе.

## Build

The recommended build entry point creates both distributable folders:

```powershell
./Build.ps1 -Configuration Release -Runtime win-x64
```

The script creates:

- `output/` - portable Fluxora build. By default the C#/.NET runtime is published as a single `FluxoraModding.exe`, with `FluxoraCore.dll` and `FluxoraVfs.dll` kept beside it for the native bridge and virtual file system hook. Use `-LooseFiles` if you need the old expanded .NET publish layout for debugging.
- `output-installer/` - `FluxoraSetup.exe`, a branded installer that embeds the portable build, asks for language, privacy policy and terms acceptance, lets the user choose the installation folder, and unpacks Fluxora through the native C++ installer core.

### Backend

```powershell
cmake -S backend -B build/backend
cmake --build build/backend
```

### Frontend

```powershell
dotnet build frontend/Fluxora.App.csproj
```

## Status

This repository currently contains the base project layout and service skeletons. Game-specific hook implementations, mod package formats, and UI workflows are intended to be added in later development stages.

## Ownership

Fluxora is part of ModdingFlow. See the `LICENSE` file for usage restrictions.
