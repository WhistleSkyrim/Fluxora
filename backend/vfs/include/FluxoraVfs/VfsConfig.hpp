#pragma once

#include <string>
#include <vector>

namespace fluxora::vfs
{
    // Parsed form of the JSON descriptor written by the manager. Paths are stored
    // as plain DOS paths (for example "C:\\Games\\Skyrim\\Data"); the tree builder
    // normalizes them as needed.
    struct VfsConfig
    {
        int schemaVersion{0};
        std::wstring target;     // real game data directory
        std::wstring overwrite;  // writable overlay (highest read priority)
        std::wstring logPath;
        std::wstring hookDll;    // FluxoraVfs.dll path, for child re-injection
        std::vector<std::wstring> mods; // load order ascending (last wins)

        [[nodiscard]] bool isValid() const noexcept
        {
            return !target.empty();
        }
    };

    // Reads and parses the descriptor pointed to by the FLUXORA_VFS_CONFIG
    // environment variable. Returns false if the variable is missing, the file
    // cannot be read, or the JSON is malformed.
    bool loadVfsConfigFromEnvironment(VfsConfig& config);
}
