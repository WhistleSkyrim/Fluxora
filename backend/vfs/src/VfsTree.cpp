#include "FluxoraVfs/VfsTree.hpp"

#include "FluxoraVfs/VfsLog.hpp"

#include <algorithm>
#include <cwctype>

namespace fluxora::vfs
{
    namespace
    {
        std::wstring stripTrailingSlashes(std::wstring value)
        {
            while (!value.empty() && (value.back() == L'\\' || value.back() == L'/'))
            {
                value.pop_back();
            }
            return value;
        }

        std::wstring joinPath(const std::wstring& base, const std::wstring& leaf)
        {
            if (base.empty())
            {
                return leaf;
            }
            if (leaf.empty())
            {
                return base;
            }
            return base + L"\\" + leaf;
        }

        LARGE_INTEGER toLargeInteger(const FILETIME& time)
        {
            LARGE_INTEGER value{};
            value.LowPart = time.dwLowDateTime;
            value.HighPart = static_cast<LONG>(time.dwHighDateTime);
            return value;
        }

        DirChild childFromFindData(
            const std::wstring& realPath,
            const WIN32_FIND_DATAW& data)
        {
            DirChild child;
            child.name = data.cFileName;
            child.realPath = realPath;
            child.isDirectory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            child.attributes = data.dwFileAttributes;
            child.creationTime = toLargeInteger(data.ftCreationTime);
            child.lastAccessTime = toLargeInteger(data.ftLastAccessTime);
            child.lastWriteTime = toLargeInteger(data.ftLastWriteTime);
            child.endOfFile.LowPart = data.nFileSizeLow;
            child.endOfFile.HighPart = static_cast<LONG>(data.nFileSizeHigh);
            // Allocation size rounded up to the next cluster keeps tools happy.
            constexpr LONGLONG cluster = 4096;
            child.allocationSize.QuadPart = child.isDirectory
                ? 0
                : ((child.endOfFile.QuadPart + cluster - 1) / cluster) * cluster;
            return child;
        }
    }

    std::wstring VfsTree::toLower(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(std::towlower(character));
        });
        return value;
    }

    bool VfsTree::equalsIgnoreCase(const std::wstring& a, const std::wstring& b)
    {
        return a.size() == b.size() && toLower(a) == toLower(b);
    }

    std::wstring VfsTree::normalizeRel(std::wstring rel)
    {
        std::replace(rel.begin(), rel.end(), L'/', L'\\');
        std::size_t first = 0;
        while (first < rel.size() && rel[first] == L'\\')
        {
            ++first;
        }
        rel.erase(0, first);
        rel = stripTrailingSlashes(std::move(rel));
        return rel;
    }

    // --- build state -------------------------------------------------------
    // Per-directory child maps used only while the tree is being assembled, so the
    // last (highest priority) writer wins and the result can be name-sorted once.
    namespace
    {
        std::unordered_map<std::wstring, std::unordered_map<std::wstring, DirChild>> g_buildChildren;
        std::unordered_map<std::wstring, std::wstring> g_dirRel; // relLower -> display rel
    }

    VfsTree::DirNode& VfsTree::ensureDir(const std::wstring& relLower)
    {
        return dirMap_[relLower];
    }

    void VfsTree::upsertChild(const std::wstring& parentLower, DirChild child, bool overrideExisting)
    {
        auto& children = g_buildChildren[parentLower];
        const std::wstring key = toLower(child.name);
        if (overrideExisting)
        {
            children[key] = std::move(child);
        }
        else
        {
            children.emplace(key, std::move(child));
        }
    }

    void VfsTree::walkOverlay(const std::wstring& physicalDir, const std::wstring& rel)
    {
        const std::wstring relLower = toLower(rel);
        DirNode& node = ensureDir(relLower);
        node.openPath = physicalDir; // highest-priority overlay walked last wins
        g_dirRel[relLower] = rel;

        WIN32_FIND_DATAW data{};
        const HANDLE find = FindFirstFileW((physicalDir + L"\\*").c_str(), &data);
        if (find == INVALID_HANDLE_VALUE)
        {
            return;
        }

        do
        {
            const std::wstring name = data.cFileName;
            if (name == L"." || name == L"..")
            {
                continue;
            }

            const std::wstring fullChild = joinPath(physicalDir, name);
            const std::wstring childRel = joinPath(rel, name);
            DirChild child = childFromFindData(fullChild, data);
            const bool isDirectory = child.isDirectory;

            upsertChild(relLower, child, /*overrideExisting=*/true);

            if (isDirectory)
            {
                walkOverlay(fullChild, childRel);
            }
            else
            {
                fileMap_[toLower(childRel)] = fullChild;
            }
        } while (FindNextFileW(find, &data) != 0);

        FindClose(find);
    }

    void VfsTree::mergeRealDirectory(const std::wstring& relLower, const std::wstring& realDir)
    {
        DirNode& node = dirMap_[relLower];

        WIN32_FIND_DATAW data{};
        const HANDLE find = FindFirstFileW((realDir + L"\\*").c_str(), &data);
        if (find == INVALID_HANDLE_VALUE)
        {
            node.realExists = false;
            return;
        }

        node.realExists = true;
        if (node.openPath.empty())
        {
            node.openPath = realDir;
        }

        const std::wstring rel = g_dirRel[relLower];
        do
        {
            const std::wstring name = data.cFileName;
            if (name == L"." || name == L"..")
            {
                continue;
            }

            const std::wstring fullChild = joinPath(realDir, name);
            const std::wstring childRel = joinPath(rel, name);
            DirChild child = childFromFindData(fullChild, data);

            // The real game directory is the lowest priority: only fill gaps.
            upsertChild(relLower, child, /*overrideExisting=*/false);
            if (!child.isDirectory)
            {
                fileMap_.emplace(toLower(childRel), fullChild);
            }
        } while (FindNextFileW(find, &data) != 0);

        FindClose(find);
    }

    void VfsTree::finalize()
    {
        for (auto& [relLower, children] : g_buildChildren)
        {
            DirNode& node = dirMap_[relLower];
            node.children.clear();
            node.children.reserve(children.size());
            for (auto& [nameLower, child] : children)
            {
                node.children.push_back(child);
            }

            std::sort(node.children.begin(), node.children.end(),
                [](const DirChild& a, const DirChild& b)
                {
                    return toLower(a.name) < toLower(b.name);
                });
        }

        g_buildChildren.clear();
        g_dirRel.clear();
    }

    void VfsTree::build(const VfsConfig& config)
    {
        target_ = stripTrailingSlashes(config.target);
        overwrite_ = stripTrailingSlashes(config.overwrite);

        // Always have a root node so the data directory itself is virtualized.
        ensureDir(L"");
        g_dirRel[L""] = L"";

        // Mods in load order ascending: each is walked on top of the previous, so
        // a later mod overrides an earlier one.
        for (const std::wstring& mod : config.mods)
        {
            const std::wstring root = stripTrailingSlashes(mod);
            if (GetFileAttributesW(root.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                walkOverlay(root, L"");
            }
        }

        // The writable overwrite overlay has the highest read priority.
        if (!overwrite_.empty() && GetFileAttributesW(overwrite_.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            walkOverlay(overwrite_, L"");
        }

        // Merge the real game directory into every touched directory so unmodded
        // siblings still appear in the merged listing.
        std::vector<std::wstring> affected;
        affected.reserve(dirMap_.size());
        for (const auto& [relLower, node] : dirMap_)
        {
            affected.push_back(relLower);
        }

        for (const std::wstring& relLower : affected)
        {
            const std::wstring rel = g_dirRel.count(relLower) ? g_dirRel[relLower] : relLower;
            const std::wstring realDir = rel.empty() ? target_ : joinPath(target_, rel);
            mergeRealDirectory(relLower, realDir);
        }

        finalize();
        built_ = true;

        VfsLog::writef(
            L"VfsTree built: %zu files, %zu virtual directories, target=%s",
            fileMap_.size(),
            dirMap_.size(),
            target_.c_str());
    }

    bool VfsTree::isVirtualDir(const std::wstring& relLower) const
    {
        return dirMap_.find(relLower) != dirMap_.end();
    }

    const std::vector<DirChild>* VfsTree::listing(const std::wstring& relLower) const
    {
        const auto it = dirMap_.find(relLower);
        return it == dirMap_.end() ? nullptr : &it->second.children;
    }

    VfsTree::PathInfo VfsTree::classify(const std::wstring& rel) const
    {
        const std::wstring relN = normalizeRel(rel);
        const std::wstring key = toLower(relN);

        if (const auto dir = dirMap_.find(key); dir != dirMap_.end())
        {
            PathInfo info;
            info.kind = PathInfo::Kind::Directory;
            info.winner = dir->second.openPath;
            info.directoryRealExists = dir->second.realExists;
            return info;
        }

        if (const auto file = fileMap_.find(key); file != fileMap_.end())
        {
            PathInfo info;
            info.kind = PathInfo::Kind::File;
            info.winner = file->second;
            return info;
        }

        PathInfo info;
        info.kind = PathInfo::Kind::Unknown;
        const auto slash = key.find_last_of(L'\\');
        const std::wstring parentLower = slash == std::wstring::npos ? std::wstring() : key.substr(0, slash);
        info.parentVirtual = isVirtualDir(parentLower);
        return info;
    }
}
