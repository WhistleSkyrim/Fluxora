#pragma once

#include <string>
#include <vector>

namespace fluxora::vfs
{
    struct VfsMountConfig
    {
        std::wstring target;     // real directory this mount is projected onto
        std::wstring overwrite;  // writable overlay for this mount
        std::vector<std::wstring> mods; // load order ascending (last wins)
        std::vector<std::wstring> excludedRootNames;

        [[nodiscard]] bool isValid() const noexcept
        {
            return !target.empty();
        }
    };

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
        std::vector<VfsMountConfig> mounts;

        [[nodiscard]] bool isValid() const noexcept
        {
            return !mounts.empty();
        }
    };

    // Reads and parses the descriptor pointed to by the FLUXORA_VFS_CONFIG
    // environment variable. Returns false if the variable is missing, the file
    // cannot be read, or the JSON is malformed.
    bool loadVfsConfigFromEnvironment(VfsConfig& config);
}
