#pragma once

// Shared contract between the Fluxora manager (FluxoraCore.dll, the process that
// launches the game) and the injected virtual file system (FluxoraVfs.dll, the
// DLL that lives inside the game process).
//
// The manager builds a small JSON document describing the merged, virtual data
// directory and writes it next to the instance. It then launches the game with
// FluxoraVfs.dll injected and the path of that JSON document exported through an
// environment variable. The injected DLL reads the document, builds the virtual
// tree and installs the file-system hooks. The same environment variable is
// inherited by every child process, so the whole process tree shares one view.
//
// Nothing is ever copied into the game folder: the manager only ever writes this
// tiny descriptor; the mod files stay in the instance "mods" folder and are made
// to *appear* inside the game/data directories by the hooks at runtime.

namespace fluxora::vfs::protocol
{
    // Environment variable carrying the absolute path of the JSON descriptor.
    inline constexpr const wchar_t* configEnvironmentVariable = L"FLUXORA_VFS_CONFIG";

    // Current descriptor schema version. Bump when the JSON layout changes in a
    // way the injected DLL must be able to detect.
    inline constexpr int schemaVersion = 2;

    // JSON field names of the descriptor document.
    namespace fields
    {
        inline constexpr const wchar_t* schemaVersion = L"schemaVersion";

        // Absolute path of the real game data directory the virtual tree is
        // projected onto (for example "C:\\Games\\Skyrim Special Edition\\Data").
        inline constexpr const wchar_t* target = L"target";

        // Absolute path of the writable overlay. Newly created files that do not
        // already exist in any mod land here instead of in the real game folder.
        inline constexpr const wchar_t* overwrite = L"overwrite";

        // Absolute path of the log file the injected DLL appends to.
        inline constexpr const wchar_t* logPath = L"logPath";

        // Absolute path of FluxoraVfs.dll, used when re-injecting child processes.
        inline constexpr const wchar_t* hookDll = L"hookDll";

        // Ordered array of absolute mod directories. Order is load order ascending:
        // the FIRST entry has the LOWEST priority and the LAST entry the HIGHEST.
        // A later mod therefore overrides files contributed by an earlier mod, and
        // every mod overrides the real game files. The writable overwrite overlay
        // sits above all of them.
        inline constexpr const wchar_t* mods = L"mods";

        // Schema v2 supports multiple virtual mount points. Each mount object uses
        // the same target/overwrite/mods fields above.
        inline constexpr const wchar_t* mounts = L"mounts";

        // Top-level folder names that should not be projected into this mount.
        // Root Builder uses a top-level "root" folder for game-root files, so the
        // data mount excludes it while a separate game-root mount projects it.
        inline constexpr const wchar_t* excludedRootNames = L"excludedRootNames";
    }
}
