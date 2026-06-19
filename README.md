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

## Unique Features

- **Speed as the top priority.** Fluxora focuses on fast game startup, responsive UI feedback, and minimal waiting while working with mods. The native C++ core handles heavy operations, keeping the manager lightweight and responsive where traditional launchers and mod managers often slow down.
- **Full offline mode.** Installed games, profiles, and mods remain available without an internet connection, so users can launch the game and play with the selected build even when offline.
- **One-click mod pack sharing.** Users can prepare and share their builds without manual packaging, complicated instructions, or lengthy setup.
- **Import existing Mod Organizer 2 builds.** Fluxora helps import an existing MO2 build so users can continue working with it in this application.

## Build

The recommended build entry point creates the local application payload and the installer:

```powershell
./Build.ps1 -Configuration Release -Runtime win-x64
```

The script creates:

- `output/` - local application payload staging. It is used only to assemble the installer payload and must not be published as a release artifact.
- `output-installer/` - `FluxoraSetup.exe`, the branded installer that embeds the application payload, asks for language, privacy policy and terms acceptance, lets the user choose the installation folder, and installs Fluxora through the native C++ installer core.

## Release Policy

Fluxora releases are installer-only. Publish only `output-installer/FluxoraSetup.exe`; do not commit, push, upload, attach, zip, or otherwise distribute `output/` or any portable build.

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
