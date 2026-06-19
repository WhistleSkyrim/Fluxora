#pragma once

#include "FluxoraVfs/VfsConfig.hpp"

#include <windows.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fluxora::vfs
{
    // One merged child entry of a virtual directory. Metadata is captured once,
    // while the tree is built, so directory enumeration needs no per-call I/O.
    struct DirChild
    {
        std::wstring name;       // display name, original case
        std::wstring realPath;   // absolute backing path on disk
        bool isDirectory{false};
        ULONG attributes{FILE_ATTRIBUTE_NORMAL};
        LARGE_INTEGER creationTime{};
        LARGE_INTEGER lastAccessTime{};
        LARGE_INTEGER lastWriteTime{};
        LARGE_INTEGER endOfFile{};
        LARGE_INTEGER allocationSize{};
    };

    // The merged, read-mostly view of one virtualized game directory.
    //
    // It is assembled once when the DLL is injected: every enabled mod (in load
    // order) plus the writable overwrite overlay are walked and merged on top of
    // the real game directory. Files contributed by a later mod win over earlier
    // mods, and any mod wins over the real game file. Only directories actually
    // touched by a mod are "virtualized"; every other path falls straight through
    // to the real disk, so the cost is proportional to the mods, not the game.
    class VfsTree final
    {
    public:
        // What a path under the mount target maps to in the merged tree.
        struct PathInfo
        {
            enum class Kind
            {
                Unknown,   // not provided by any mod / overlay
                File,
                Directory
            };

            Kind kind{Kind::Unknown};
            std::wstring winner;       // File: backing file; Directory: a real dir to open
            bool directoryRealExists{false}; // Directory also exists in the real game folder
            bool parentVirtual{false}; // Unknown: its parent directory is virtualized
        };

        void build(const VfsMountConfig& config);

        [[nodiscard]] const std::wstring& target() const noexcept { return target_; }
        [[nodiscard]] const std::wstring& overwrite() const noexcept { return overwrite_; }
        [[nodiscard]] bool isBuilt() const noexcept { return built_; }

        // Pure lookup: classify a path (relative to the mount target, backslashes,
        // "" = the data directory itself). The hooks turn this into a concrete
        // open/redirect decision and perform any disk side effects themselves.
        [[nodiscard]] PathInfo classify(const std::wstring& rel) const;

        // True when `relLower` (a lowercase, normalized relative path) is a
        // directory whose listing must be synthesized from the merged tree.
        [[nodiscard]] bool isVirtualDir(const std::wstring& relLower) const;

        // Merged, name-sorted children of a virtual directory, or nullptr.
        [[nodiscard]] const std::vector<DirChild>* listing(const std::wstring& relLower) const;

        // Path helpers shared with the hooks.
        [[nodiscard]] static std::wstring toLower(std::wstring value);
        [[nodiscard]] static std::wstring normalizeRel(std::wstring rel);
        [[nodiscard]] static bool equalsIgnoreCase(const std::wstring& a, const std::wstring& b);
        [[nodiscard]] static bool wildcardMatch(std::wstring_view name, std::wstring_view pattern);

    private:
        struct DirNode
        {
            bool realExists{false};        // the real game directory has this path
            std::wstring openPath;         // an existing directory to back a handle
            std::vector<DirChild> children;
        };

        void walkOverlay(const std::wstring& sourceRoot, const std::wstring& rel);
        void mergeRealDirectory(const std::wstring& relLower, const std::wstring& realDir);
        DirNode& ensureDir(const std::wstring& relLower);
        void upsertChild(const std::wstring& parentLower, DirChild child, bool overrideExisting);
        [[nodiscard]] bool isExcludedTopLevelName(const std::wstring& name) const;
        void finalize();

        std::wstring target_;
        std::wstring overwrite_;
        std::vector<std::wstring> excludedRootNames_;

        // rel(lower) -> winning backing file path. Files only.
        std::unordered_map<std::wstring, std::wstring> fileMap_;
        // rel(lower) -> directory node. "" is the data root.
        std::unordered_map<std::wstring, DirNode> dirMap_;

        bool built_{false};
    };
}
