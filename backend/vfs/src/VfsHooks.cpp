#include "FluxoraVfs/VfsHooks.hpp"

#include "FluxoraVfs/NtApi.hpp"
#include "FluxoraVfs/VfsLog.hpp"
#include "FluxoraVfs/VfsProtocol.hpp"
#include "FluxoraVfs/VfsTree.hpp"

#include <detours.h>

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fluxora::vfs
{
    using namespace nt;

    namespace
    {
        // --- global state ---------------------------------------------------
        struct RuntimeMount
        {
            VfsTree tree;
            std::wstring targetLower;
            std::wstring overwrite;
            std::vector<std::wstring> excludedRootNames;
        };

        std::vector<RuntimeMount> g_mounts;
        std::string g_hookDllAnsi;
        std::wstring g_configPath;

        // Re-entrancy guard. While a hook is doing its work it inevitably calls
        // back into the very APIs it hooks (GetFileAttributes, CopyFile, ...).
        // The guard makes those nested calls fall straight through to the real
        // function, so the manager-style logic never recurses into itself.
        thread_local int g_guardDepth = 0;
        struct Guard
        {
            Guard() noexcept { ++g_guardDepth; }
            ~Guard() noexcept { --g_guardDepth; }
        };
        bool reentrant() noexcept { return g_guardDepth > 0; }

        // Real (trampolined) function pointers.
        NtCreateFileFn Real_NtCreateFile = nullptr;
        NtOpenFileFn Real_NtOpenFile = nullptr;
        NtQueryAttributesFileFn Real_NtQueryAttributesFile = nullptr;
        NtQueryFullAttributesFileFn Real_NtQueryFullAttributesFile = nullptr;
        NtQueryDirectoryFileFn Real_NtQueryDirectoryFile = nullptr;
        NtQueryDirectoryFileExFn Real_NtQueryDirectoryFileEx = nullptr;
        NtSetInformationFileFn Real_NtSetInformationFile = nullptr;
        NtDeleteFileFn Real_NtDeleteFile = nullptr;
        NtCloseFn Real_NtClose = nullptr;
        decltype(&CreateProcessW) Real_CreateProcessW = CreateProcessW;
        decltype(&CreateProcessA) Real_CreateProcessA = CreateProcessA;

        // Per-handle directory enumeration state for virtualized directories.
        struct DirEnumState
        {
            std::size_t mountIndex = 0;
            std::wstring relLower;
            bool built = false;
            bool patternInitialized = false;
            bool matchAll = true;
            std::wstring pattern; // lowercase
            std::size_t index = 0;
            std::vector<DirChild> entries;
        };

        std::mutex g_dirMutex;
        std::unordered_map<HANDLE, DirEnumState> g_dirStates;

        struct FileState
        {
            std::size_t mountIndex = 0;
            std::wstring relLower;
        };

        std::unordered_map<HANDLE, FileState> g_fileStates;

        // --- small path helpers --------------------------------------------
        std::wstring joinPath(const std::wstring& base, const std::wstring& leaf)
        {
            if (base.empty()) return leaf;
            if (leaf.empty()) return base;
            return base + L"\\" + leaf;
        }

        std::string toAnsi(const std::wstring& value)
        {
            if (value.empty()) return {};
            const int size = WideCharToMultiByte(
                CP_ACP, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            std::string out(static_cast<size_t>(size), '\0');
            WideCharToMultiByte(
                CP_ACP, 0, value.data(), static_cast<int>(value.size()), out.data(), size, nullptr, nullptr);
            return out;
        }

        char toLowerAscii(char value)
        {
            return value >= 'A' && value <= 'Z'
                ? static_cast<char>(value - 'A' + 'a')
                : value;
        }

        bool equalsIgnoreCaseAscii(std::string_view left, std::string_view right)
        {
            if (left.size() != right.size())
            {
                return false;
            }

            for (std::size_t index = 0; index < left.size(); ++index)
            {
                if (toLowerAscii(left[index]) != toLowerAscii(right[index]))
                {
                    return false;
                }
            }

            return true;
        }

        bool isEnvironmentEntryForName(const std::wstring& entry, std::wstring_view name)
        {
            const std::size_t searchFrom = !entry.empty() && entry.front() == L'=' ? 1 : 0;
            const std::size_t separator = entry.find(L'=', searchFrom);
            return separator == name.size() &&
                VfsTree::toLower(entry.substr(0, separator)) == VfsTree::toLower(std::wstring(name));
        }

        bool isEnvironmentEntryForName(const std::string& entry, std::string_view name)
        {
            const std::size_t searchFrom = !entry.empty() && entry.front() == '=' ? 1 : 0;
            const std::size_t separator = entry.find('=', searchFrom);
            return separator == name.size() &&
                equalsIgnoreCaseAscii(std::string_view(entry.data(), separator), name);
        }

        std::vector<wchar_t> environmentBlockWithConfig(const wchar_t* environment)
        {
            std::vector<wchar_t> block;
            if (environment == nullptr || g_configPath.empty())
            {
                return block;
            }

            const std::wstring variableName = protocol::configEnvironmentVariable;
            const std::wstring assignment = variableName + L"=" + g_configPath;
            bool wroteConfig = false;
            const wchar_t* cursor = environment;
            while (*cursor != L'\0')
            {
                const std::wstring entry(cursor);
                const bool isConfigEntry = isEnvironmentEntryForName(entry, variableName);
                const std::wstring& output = isConfigEntry ? assignment : entry;
                wroteConfig = wroteConfig || isConfigEntry;
                block.insert(block.end(), output.begin(), output.end());
                block.push_back(L'\0');
                cursor += entry.size() + 1;
            }

            if (!wroteConfig)
            {
                block.insert(block.end(), assignment.begin(), assignment.end());
                block.push_back(L'\0');
            }
            block.push_back(L'\0');
            return block;
        }

        std::vector<char> environmentBlockWithConfig(const char* environment)
        {
            std::vector<char> block;
            if (environment == nullptr || g_configPath.empty())
            {
                return block;
            }

            const std::string variableName = toAnsi(protocol::configEnvironmentVariable);
            const std::string assignment = variableName + "=" + toAnsi(g_configPath);
            bool wroteConfig = false;
            const char* cursor = environment;
            while (*cursor != '\0')
            {
                const std::string entry(cursor);
                const bool isConfigEntry = isEnvironmentEntryForName(entry, variableName);
                const std::string& output = isConfigEntry ? assignment : entry;
                wroteConfig = wroteConfig || isConfigEntry;
                block.insert(block.end(), output.begin(), output.end());
                block.push_back('\0');
                cursor += entry.size() + 1;
            }

            if (!wroteConfig)
            {
                block.insert(block.end(), assignment.begin(), assignment.end());
                block.push_back('\0');
            }
            block.push_back('\0');
            return block;
        }

        std::wstring stripNtPrefix(std::wstring path)
        {
            if (path.rfind(L"\\??\\", 0) == 0 || path.rfind(L"\\\\?\\", 0) == 0)
            {
                path.erase(0, 4);
            }
            return path;
        }

        bool isAbsoluteObjectName(const std::wstring& objectName)
        {
            return objectName.rfind(L"\\??\\", 0) == 0 ||
                objectName.rfind(L"\\\\?\\", 0) == 0 ||
                (objectName.size() >= 2 && objectName[1] == L':');
        }

        std::wstring relativeObjectName(std::wstring objectName)
        {
            while (!objectName.empty() && (objectName.front() == L'\\' || objectName.front() == L'/'))
            {
                objectName.erase(objectName.begin());
            }
            return objectName;
        }

        std::vector<std::wstring> normalizedExcludedRootNames(const std::vector<std::wstring>& names)
        {
            std::vector<std::wstring> excluded;
            excluded.reserve(names.size());
            for (const std::wstring& name : names)
            {
                const std::wstring normalized = VfsTree::normalizeRel(name);
                if (!normalized.empty() && normalized.find(L'\\') == std::wstring::npos)
                {
                    excluded.push_back(VfsTree::toLower(normalized));
                }
            }
            return excluded;
        }

        bool isExcludedRelativePath(const RuntimeMount& mount, const std::wstring& relLower)
        {
            if (relLower.empty())
            {
                return false;
            }

            const std::size_t separator = relLower.find(L'\\');
            const std::wstring_view first = separator == std::wstring::npos
                ? std::wstring_view(relLower)
                : std::wstring_view(relLower.data(), separator);
            return std::find(
                mount.excludedRootNames.begin(),
                mount.excludedRootNames.end(),
                first) != mount.excludedRootNames.end();
        }

        std::wstring parentRelLower(const std::wstring& relLower);

        bool virtualDosPathFromTrackedDirectoryHandle(
            POBJECT_ATTRIBUTES attributes,
            const std::wstring& objectName,
            std::wstring& dosPath)
        {
            if (attributes == nullptr ||
                attributes->RootDirectory == nullptr ||
                isAbsoluteObjectName(objectName))
            {
                return false;
            }

            std::size_t mountIndex = 0;
            std::wstring rootRelLower;
            {
                std::scoped_lock lock(g_dirMutex);
                const auto state = g_dirStates.find(attributes->RootDirectory);
                if (state == g_dirStates.end())
                {
                    return false;
                }

                mountIndex = state->second.mountIndex;
                rootRelLower = state->second.relLower;
            }

            if (mountIndex >= g_mounts.size())
            {
                return false;
            }

            const RuntimeMount& mount = g_mounts[mountIndex];
            const std::wstring virtualRoot = rootRelLower.empty()
                ? mount.tree.target()
                : joinPath(mount.tree.target(), rootRelLower);
            const std::wstring leaf = relativeObjectName(objectName);
            dosPath = leaf.empty() ? virtualRoot : joinPath(virtualRoot, leaf);
            return true;
        }

        bool virtualDosPathFromTrackedFileHandle(
            HANDLE handle,
            const std::wstring& objectName,
            std::wstring& dosPath)
        {
            if (handle == nullptr || handle == INVALID_HANDLE_VALUE || isAbsoluteObjectName(objectName))
            {
                return false;
            }

            FileState fileState;
            {
                std::scoped_lock lock(g_dirMutex);
                const auto state = g_fileStates.find(handle);
                if (state == g_fileStates.end())
                {
                    return false;
                }

                fileState = state->second;
            }

            if (fileState.mountIndex >= g_mounts.size())
            {
                return false;
            }

            const RuntimeMount& mount = g_mounts[fileState.mountIndex];
            const std::wstring parent = parentRelLower(fileState.relLower);
            const std::wstring leaf = relativeObjectName(objectName);
            const std::wstring targetRel = parent.empty() ? leaf : joinPath(parent, leaf);
            dosPath = targetRel.empty() ? mount.tree.target() : joinPath(mount.tree.target(), targetRel);
            return true;
        }

        // Resolve the absolute DOS path an OBJECT_ATTRIBUTES refers to. Handles
        // both fully qualified NT names and opens relative to a directory handle.
        bool dosPathFromObjectAttributes(POBJECT_ATTRIBUTES attributes, std::wstring& dosPath)
        {
            if (attributes == nullptr || attributes->ObjectName == nullptr ||
                attributes->ObjectName->Buffer == nullptr)
            {
                return false;
            }

            const std::wstring objectName(
                attributes->ObjectName->Buffer,
                attributes->ObjectName->Length / sizeof(wchar_t));

            if (virtualDosPathFromTrackedDirectoryHandle(attributes, objectName, dosPath))
            {
                return true;
            }

            if (attributes->RootDirectory != nullptr)
            {
                wchar_t rootBuffer[MAX_PATH * 2];
                const DWORD length = GetFinalPathNameByHandleW(
                    attributes->RootDirectory,
                    rootBuffer,
                    static_cast<DWORD>(std::size(rootBuffer)),
                    FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
                if (length == 0 || length >= std::size(rootBuffer))
                {
                    return false;
                }

                std::wstring root = stripNtPrefix(std::wstring(rootBuffer, length));
                if (root.size() < 2 || root[1] != L':')
                {
                    return false;
                }

                std::wstring leaf = relativeObjectName(objectName);
                dosPath = leaf.empty() ? root : root + L"\\" + leaf;
                return true;
            }

            if (objectName.empty() || objectName.front() != L'\\')
            {
                return false;
            }

            dosPath = stripNtPrefix(objectName);
            return dosPath.size() >= 2 && dosPath[1] == L':';
        }

        struct TargetMatch
        {
            std::size_t mountIndex = 0;
            std::wstring rel;
        };

        // True when `dosPath` is a mount target or lives inside it; `match.rel` is
        // filled with the remainder ("" for the target directory itself).
        bool underMountedTarget(const std::wstring& dosPath, TargetMatch& match)
        {
            std::wstring lower = VfsTree::toLower(dosPath);
            while (!lower.empty() && (lower.back() == L'\\' || lower.back() == L'/'))
            {
                lower.pop_back();
            }

            for (std::size_t index = 0; index < g_mounts.size(); ++index)
            {
                const std::wstring& targetLower = g_mounts[index].targetLower;
                if (lower == targetLower)
                {
                    match.mountIndex = index;
                    match.rel.clear();
                    return true;
                }

                if (lower.size() > targetLower.size() + 1 &&
                    lower.compare(0, targetLower.size(), targetLower) == 0 &&
                    lower[targetLower.size()] == L'\\')
                {
                    const std::wstring relLower = lower.substr(targetLower.size() + 1);
                    if (isExcludedRelativePath(g_mounts[index], relLower))
                    {
                        continue;
                    }

                    match.mountIndex = index;
                    match.rel = dosPath.substr(targetLower.size() + 1);
                    return true;
                }
            }

            return false;
        }

        void makeParentDirectories(const std::wstring& filePath)
        {
            const std::size_t slash = filePath.find_last_of(L'\\');
            if (slash == std::wstring::npos)
            {
                return;
            }

            const std::wstring directory = filePath.substr(0, slash);
            for (std::size_t index = 0; index < directory.size(); ++index)
            {
                if (directory[index] == L'\\' && index > 2)
                {
                    CreateDirectoryW(directory.substr(0, index).c_str(), nullptr);
                }
            }
            CreateDirectoryW(directory.c_str(), nullptr);
        }

        std::wstring parentRelLower(const std::wstring& relLower)
        {
            const std::size_t slash = relLower.find_last_of(L'\\');
            return slash == std::wstring::npos ? std::wstring() : relLower.substr(0, slash);
        }

        bool hasDirectoryAttribute(const std::wstring& path)
        {
            if (path.empty())
            {
                return false;
            }

            const DWORD attributes = GetFileAttributesW(path.c_str());
            return attributes != INVALID_FILE_ATTRIBUTES &&
                (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        }

        std::wstring overwritePathForRel(const RuntimeMount& mount, const std::wstring& rel)
        {
            if (mount.overwrite.empty())
            {
                return {};
            }

            return rel.empty() ? mount.overwrite : joinPath(mount.overwrite, rel);
        }

        bool overwriteDirectoryExists(const RuntimeMount& mount, const std::wstring& relLower)
        {
            return hasDirectoryAttribute(overwritePathForRel(mount, relLower));
        }

        bool dispositionCreates(ULONG disposition)
        {
            // FILE_SUPERSEDE(0), FILE_CREATE(2), FILE_OPEN_IF(3), FILE_OVERWRITE_IF(5)
            return disposition == 0 || disposition == 2 || disposition == 3 || disposition == 5;
        }

        bool dispositionTruncates(ULONG disposition)
        {
            // FILE_SUPERSEDE(0), FILE_OVERWRITE(4), FILE_OVERWRITE_IF(5)
            return disposition == 0 || disposition == 4 || disposition == 5;
        }

        bool desiredAccessWrites(ACCESS_MASK access)
        {
            constexpr ACCESS_MASK writeBits =
                FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA |
                DELETE | GENERIC_WRITE | GENERIC_ALL;
            return (access & writeBits) != 0;
        }

        // Copy-on-write into the writable overwrite overlay. Returns the path the
        // write should be redirected to, leaving every mod folder untouched.
        std::wstring writeRedirect(
            const RuntimeMount& mount,
            const std::wstring& rel,
            bool fileKnown,
            const std::wstring& winner,
            ULONG disposition)
        {
            const std::wstring candidate = joinPath(mount.overwrite, rel);
            if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                return candidate;
            }

            makeParentDirectories(candidate);
            if (fileKnown && !dispositionTruncates(disposition) && !winner.empty())
            {
                // Read-modify-write: seed the overlay with the current contents.
                CopyFileW(winner.c_str(), candidate.c_str(), TRUE);
            }
            return candidate;
        }

        struct OpenDecision
        {
            bool redirect = false;
            std::wstring path;
            bool registerMerge = false;
            std::size_t mountIndex = 0;
            std::wstring relLower;
        };

        OpenDecision decideOpen(
            std::size_t mountIndex,
            const std::wstring& rel,
            ULONG disposition,
            bool writeAccess,
            bool directoryOpen)
        {
            const RuntimeMount& mount = g_mounts[mountIndex];
            const std::wstring relN = VfsTree::normalizeRel(rel);
            const std::wstring relLower = VfsTree::toLower(relN);
            const VfsTree::PathInfo info = mount.tree.classify(relN);

            OpenDecision decision;
            decision.mountIndex = mountIndex;
            decision.relLower = relLower;
            switch (info.kind)
            {
            case VfsTree::PathInfo::Kind::Directory:
                decision.registerMerge = true;
                if (!info.directoryRealExists)
                {
                    decision.redirect = true;
                    decision.path = info.winner;
                }
                return decision;

            case VfsTree::PathInfo::Kind::File:
                if (writeAccess)
                {
                    decision.redirect = true;
                    decision.path = writeRedirect(mount, relN, true, info.winner, disposition);
                }
                else if (!VfsTree::equalsIgnoreCase(info.winner, joinPath(mount.tree.target(), relN)))
                {
                    decision.redirect = true;
                    decision.path = info.winner;
                }
                return decision;

            case VfsTree::PathInfo::Kind::Unknown:
            default:
                if (!mount.overwrite.empty())
                {
                    const std::wstring candidate = joinPath(mount.overwrite, relN);
                    const DWORD attributes = GetFileAttributesW(candidate.c_str());
                    if (attributes != INVALID_FILE_ATTRIBUTES)
                    {
                        decision.redirect = true;
                        decision.path = candidate;
                        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                        {
                            decision.registerMerge = true;
                        }
                        return decision;
                    }

                    if (dispositionCreates(disposition) || writeAccess)
                    {
                        const std::wstring realCandidate = joinPath(mount.tree.target(), relN);
                        const DWORD realAttributes = GetFileAttributesW(realCandidate.c_str());
                        const bool realFileExists =
                            realAttributes != INVALID_FILE_ATTRIBUTES &&
                            (realAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
                        decision.redirect = true;
                        decision.path = writeRedirect(
                            mount,
                            relN,
                            realFileExists,
                            realFileExists ? realCandidate : std::wstring(),
                            disposition);
                        if (directoryOpen)
                        {
                            decision.registerMerge = true;
                        }
                    }
                }
                return decision;
            }
        }

        // Temporarily rewrites an OBJECT_ATTRIBUTES to point at a redirected DOS
        // path, restoring the caller's fields when it goes out of scope.
        class RedirectScope
        {
        public:
            RedirectScope(POBJECT_ATTRIBUTES attributes, const std::wstring& dosPath)
                : attributes_(attributes), ntPath_(L"\\??\\" + dosPath)
            {
                unicode_.Buffer = const_cast<PWSTR>(ntPath_.c_str());
                unicode_.Length = static_cast<USHORT>(ntPath_.size() * sizeof(wchar_t));
                unicode_.MaximumLength = static_cast<USHORT>(unicode_.Length + sizeof(wchar_t));
                savedName_ = attributes_->ObjectName;
                savedRoot_ = attributes_->RootDirectory;
                attributes_->ObjectName = &unicode_;
                attributes_->RootDirectory = nullptr;
            }

            ~RedirectScope() { restore(); }

            void restore() noexcept
            {
                if (attributes_ != nullptr)
                {
                    attributes_->ObjectName = savedName_;
                    attributes_->RootDirectory = savedRoot_;
                    attributes_ = nullptr;
                }
            }

            RedirectScope(const RedirectScope&) = delete;
            RedirectScope& operator=(const RedirectScope&) = delete;

        private:
            POBJECT_ATTRIBUTES attributes_;
            std::wstring ntPath_;
            UNICODE_STRING unicode_{};
            PUNICODE_STRING savedName_{};
            HANDLE savedRoot_{};
        };

        void registerDirectoryHandle(HANDLE handle, std::size_t mountIndex, const std::wstring& relLower)
        {
            if (handle == nullptr ||
                handle == INVALID_HANDLE_VALUE ||
                mountIndex >= g_mounts.size())
            {
                return;
            }

            const RuntimeMount& mount = g_mounts[mountIndex];
            if (isExcludedRelativePath(mount, relLower))
            {
                return;
            }

            if (!mount.tree.isVirtualDir(relLower) && !overwriteDirectoryExists(mount, relLower))
            {
                return;
            }

            std::scoped_lock lock(g_dirMutex);
            DirEnumState state;
            state.mountIndex = mountIndex;
            state.relLower = relLower;
            g_dirStates[handle] = std::move(state);
        }

        void registerFileHandle(HANDLE handle, std::size_t mountIndex, const std::wstring& relLower)
        {
            if (handle == nullptr || handle == INVALID_HANDLE_VALUE || mountIndex >= g_mounts.size())
            {
                return;
            }

            std::scoped_lock lock(g_dirMutex);
            g_fileStates[handle] = FileState{mountIndex, relLower};
        }

        void unregisterDirectoryHandle(HANDLE handle)
        {
            std::scoped_lock lock(g_dirMutex);
            g_dirStates.erase(handle);
            g_fileStates.erase(handle);
        }

        void clearRuntimeState()
        {
            std::scoped_lock lock(g_dirMutex);
            g_dirStates.clear();
            g_fileStates.clear();
            g_mounts.clear();
            g_hookDllAnsi.clear();
            g_configPath.clear();
        }

        // --- directory enumeration synthesis -------------------------------
        DirChild makeDotEntry(const std::wstring& name)
        {
            DirChild dot;
            dot.name = name;
            dot.isDirectory = true;
            dot.attributes = FILE_ATTRIBUTE_DIRECTORY;
            return dot;
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
            constexpr LONGLONG cluster = 4096;
            child.allocationSize.QuadPart = child.isDirectory
                ? 0
                : ((child.endOfFile.QuadPart + cluster - 1) / cluster) * cluster;
            return child;
        }

        void upsertMergedEntry(std::vector<DirChild>& entries, DirChild child)
        {
            const std::wstring key = VfsTree::toLower(child.name);
            const auto existing = std::find_if(
                entries.begin(),
                entries.end(),
                [&key](const DirChild& candidate)
                {
                    return VfsTree::toLower(candidate.name) == key;
                });

            if (existing == entries.end())
            {
                entries.push_back(std::move(child));
            }
            else
            {
                *existing = std::move(child);
            }
        }

        void mergeDynamicOverwriteEntries(DirEnumState& state)
        {
            if (state.mountIndex >= g_mounts.size())
            {
                return;
            }

            const RuntimeMount& mount = g_mounts[state.mountIndex];
            if (isExcludedRelativePath(mount, state.relLower))
            {
                return;
            }

            const std::wstring directory = overwritePathForRel(mount, state.relLower);
            if (!hasDirectoryAttribute(directory))
            {
                return;
            }

            WIN32_FIND_DATAW data{};
            const HANDLE find = FindFirstFileW((directory + L"\\*").c_str(), &data);
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

                upsertMergedEntry(
                    state.entries,
                    childFromFindData(joinPath(directory, name), data));
            } while (FindNextFileW(find, &data) != 0);

            FindClose(find);
        }

        bool hasPathSegment(const std::wstring& pathLower, std::wstring_view segment)
        {
            if (pathLower == segment)
            {
                return true;
            }

            const std::wstring token = L"\\" + std::wstring(segment);
            return pathLower.size() >= token.size() &&
                (pathLower.ends_with(token) || pathLower.find(token + L"\\") != std::wstring::npos);
        }

        bool shouldLogDiagnosticEnumeration(const DirEnumState& state)
        {
            if (state.mountIndex >= g_mounts.size())
            {
                return false;
            }

            const std::wstring& target = g_mounts[state.mountIndex].targetLower;
            if (state.relLower == L"skse\\plugins" ||
                state.relLower == L"dllplugins" ||
                hasPathSegment(target, L"__mo_saves") ||
                hasPathSegment(target, L"saves"))
            {
                return true;
            }

            return false;
        }

        void buildEnumEntries(DirEnumState& state)
        {
            state.entries.clear();
            if (state.mountIndex >= g_mounts.size())
            {
                return;
            }

            const std::vector<DirChild>* listing =
                g_mounts[state.mountIndex].tree.listing(state.relLower);

            const auto matches = [&state](const std::wstring& nameLower)
            {
                return state.matchAll || VfsTree::wildcardMatch(nameLower, state.pattern);
            };

            if (matches(L"."))
            {
                state.entries.push_back(makeDotEntry(L"."));
            }
            if (matches(L".."))
            {
                state.entries.push_back(makeDotEntry(L".."));
            }

            if (listing != nullptr)
            {
                for (const DirChild& child : *listing)
                {
                    state.entries.push_back(child);
                }
            }

            mergeDynamicOverwriteEntries(state);
            std::sort(
                state.entries.begin(),
                state.entries.end(),
                [](const DirChild& left, const DirChild& right)
                {
                    return VfsTree::toLower(left.name) < VfsTree::toLower(right.name);
                });
            state.entries.erase(
                std::remove_if(
                    state.entries.begin(),
                    state.entries.end(),
                    [&matches](const DirChild& child)
                    {
                        return !matches(VfsTree::toLower(child.name));
                    }),
                state.entries.end());

            if (shouldLogDiagnosticEnumeration(state))
            {
                const std::wstring rel = state.relLower.empty() ? L"<root>" : state.relLower;
                const std::wstring pattern = state.pattern.empty() ? L"<all>" : state.pattern;
                VfsLog::writef(
                    L"VfsEnum target=%s rel=%s pattern=%s entries=%zu",
                    g_mounts[state.mountIndex].tree.target().c_str(),
                    rel.c_str(),
                    pattern.c_str(),
                    state.entries.size());
            }

            state.index = 0;
            state.built = true;
        }

        ULONG entryBaseSize(int infoClass)
        {
            switch (infoClass)
            {
            case FileDirectoryInformation:
                return static_cast<ULONG>(offsetof(FileDirectoryInformationRecord, FileName));
            case FileFullDirectoryInformation:
                return static_cast<ULONG>(offsetof(FileFullDirectoryInformationRecord, FileName));
            case FileBothDirectoryInformation:
                return static_cast<ULONG>(offsetof(FileBothDirectoryInformationRecord, FileName));
            case FileNamesInformation:
                return static_cast<ULONG>(offsetof(FileNamesInformationRecord, FileName));
            case FileIdBothDirectoryInformation:
                return static_cast<ULONG>(offsetof(FileIdBothDirectoryInformationRecord, FileName));
            case FileIdFullDirectoryInformation:
                return static_cast<ULONG>(offsetof(FileIdFullDirectoryInformationRecord, FileName));
            case FileIdExtdDirectoryInformation:
                return static_cast<ULONG>(offsetof(FileIdExtdDirectoryInformationRecord, FileName));
            case FileIdExtdBothDirectoryInformation:
                return static_cast<ULONG>(offsetof(FileIdExtdBothDirectoryInformationRecord, FileName));
            case FileId64ExtdDirectoryInformation:
                return static_cast<ULONG>(offsetof(FileId64ExtdDirectoryInformationRecord, FileName));
            case FileId64ExtdBothDirectoryInformation:
                return static_cast<ULONG>(offsetof(FileId64ExtdBothDirectoryInformationRecord, FileName));
            case FileIdAllExtdDirectoryInformation:
                return static_cast<ULONG>(offsetof(FileIdAllExtdDirectoryInformationRecord, FileName));
            case FileIdAllExtdBothDirectoryInformation:
                return static_cast<ULONG>(offsetof(FileIdAllExtdBothDirectoryInformationRecord, FileName));
            default:
                return 0; // unsupported class -> caller falls back to the real API
            }
        }

        void writeEntryRecord(int infoClass, BYTE* dst, const DirChild& child, ULONG nameBytes)
        {
            const ULONG base = entryBaseSize(infoClass);
            ZeroMemory(dst, base);

            switch (infoClass)
            {
            case FileDirectoryInformation:
            {
                auto* rec = reinterpret_cast<FileDirectoryInformationRecord*>(dst);
                rec->CreationTime = child.creationTime;
                rec->LastAccessTime = child.lastAccessTime;
                rec->LastWriteTime = child.lastWriteTime;
                rec->ChangeTime = child.lastWriteTime;
                rec->EndOfFile = child.endOfFile;
                rec->AllocationSize = child.allocationSize;
                rec->FileAttributes = child.attributes;
                rec->FileNameLength = nameBytes;
                memcpy(rec->FileName, child.name.data(), nameBytes);
                break;
            }
            case FileFullDirectoryInformation:
            {
                auto* rec = reinterpret_cast<FileFullDirectoryInformationRecord*>(dst);
                rec->CreationTime = child.creationTime;
                rec->LastAccessTime = child.lastAccessTime;
                rec->LastWriteTime = child.lastWriteTime;
                rec->ChangeTime = child.lastWriteTime;
                rec->EndOfFile = child.endOfFile;
                rec->AllocationSize = child.allocationSize;
                rec->FileAttributes = child.attributes;
                rec->FileNameLength = nameBytes;
                memcpy(rec->FileName, child.name.data(), nameBytes);
                break;
            }
            case FileBothDirectoryInformation:
            {
                auto* rec = reinterpret_cast<FileBothDirectoryInformationRecord*>(dst);
                rec->CreationTime = child.creationTime;
                rec->LastAccessTime = child.lastAccessTime;
                rec->LastWriteTime = child.lastWriteTime;
                rec->ChangeTime = child.lastWriteTime;
                rec->EndOfFile = child.endOfFile;
                rec->AllocationSize = child.allocationSize;
                rec->FileAttributes = child.attributes;
                rec->FileNameLength = nameBytes;
                memcpy(rec->FileName, child.name.data(), nameBytes);
                break;
            }
            case FileIdBothDirectoryInformation:
            {
                auto* rec = reinterpret_cast<FileIdBothDirectoryInformationRecord*>(dst);
                rec->CreationTime = child.creationTime;
                rec->LastAccessTime = child.lastAccessTime;
                rec->LastWriteTime = child.lastWriteTime;
                rec->ChangeTime = child.lastWriteTime;
                rec->EndOfFile = child.endOfFile;
                rec->AllocationSize = child.allocationSize;
                rec->FileAttributes = child.attributes;
                rec->FileNameLength = nameBytes;
                memcpy(rec->FileName, child.name.data(), nameBytes);
                break;
            }
            case FileIdFullDirectoryInformation:
            {
                auto* rec = reinterpret_cast<FileIdFullDirectoryInformationRecord*>(dst);
                rec->CreationTime = child.creationTime;
                rec->LastAccessTime = child.lastAccessTime;
                rec->LastWriteTime = child.lastWriteTime;
                rec->ChangeTime = child.lastWriteTime;
                rec->EndOfFile = child.endOfFile;
                rec->AllocationSize = child.allocationSize;
                rec->FileAttributes = child.attributes;
                rec->FileNameLength = nameBytes;
                memcpy(rec->FileName, child.name.data(), nameBytes);
                break;
            }
            case FileIdExtdDirectoryInformation:
            {
                auto* rec = reinterpret_cast<FileIdExtdDirectoryInformationRecord*>(dst);
                rec->CreationTime = child.creationTime;
                rec->LastAccessTime = child.lastAccessTime;
                rec->LastWriteTime = child.lastWriteTime;
                rec->ChangeTime = child.lastWriteTime;
                rec->EndOfFile = child.endOfFile;
                rec->AllocationSize = child.allocationSize;
                rec->FileAttributes = child.attributes;
                rec->FileNameLength = nameBytes;
                memcpy(rec->FileName, child.name.data(), nameBytes);
                break;
            }
            case FileIdExtdBothDirectoryInformation:
            {
                auto* rec = reinterpret_cast<FileIdExtdBothDirectoryInformationRecord*>(dst);
                rec->CreationTime = child.creationTime;
                rec->LastAccessTime = child.lastAccessTime;
                rec->LastWriteTime = child.lastWriteTime;
                rec->ChangeTime = child.lastWriteTime;
                rec->EndOfFile = child.endOfFile;
                rec->AllocationSize = child.allocationSize;
                rec->FileAttributes = child.attributes;
                rec->FileNameLength = nameBytes;
                memcpy(rec->FileName, child.name.data(), nameBytes);
                break;
            }
            case FileId64ExtdDirectoryInformation:
            {
                auto* rec = reinterpret_cast<FileId64ExtdDirectoryInformationRecord*>(dst);
                rec->CreationTime = child.creationTime;
                rec->LastAccessTime = child.lastAccessTime;
                rec->LastWriteTime = child.lastWriteTime;
                rec->ChangeTime = child.lastWriteTime;
                rec->EndOfFile = child.endOfFile;
                rec->AllocationSize = child.allocationSize;
                rec->FileAttributes = child.attributes;
                rec->FileNameLength = nameBytes;
                memcpy(rec->FileName, child.name.data(), nameBytes);
                break;
            }
            case FileId64ExtdBothDirectoryInformation:
            {
                auto* rec = reinterpret_cast<FileId64ExtdBothDirectoryInformationRecord*>(dst);
                rec->CreationTime = child.creationTime;
                rec->LastAccessTime = child.lastAccessTime;
                rec->LastWriteTime = child.lastWriteTime;
                rec->ChangeTime = child.lastWriteTime;
                rec->EndOfFile = child.endOfFile;
                rec->AllocationSize = child.allocationSize;
                rec->FileAttributes = child.attributes;
                rec->FileNameLength = nameBytes;
                memcpy(rec->FileName, child.name.data(), nameBytes);
                break;
            }
            case FileIdAllExtdDirectoryInformation:
            {
                auto* rec = reinterpret_cast<FileIdAllExtdDirectoryInformationRecord*>(dst);
                rec->CreationTime = child.creationTime;
                rec->LastAccessTime = child.lastAccessTime;
                rec->LastWriteTime = child.lastWriteTime;
                rec->ChangeTime = child.lastWriteTime;
                rec->EndOfFile = child.endOfFile;
                rec->AllocationSize = child.allocationSize;
                rec->FileAttributes = child.attributes;
                rec->FileNameLength = nameBytes;
                memcpy(rec->FileName, child.name.data(), nameBytes);
                break;
            }
            case FileIdAllExtdBothDirectoryInformation:
            {
                auto* rec = reinterpret_cast<FileIdAllExtdBothDirectoryInformationRecord*>(dst);
                rec->CreationTime = child.creationTime;
                rec->LastAccessTime = child.lastAccessTime;
                rec->LastWriteTime = child.lastWriteTime;
                rec->ChangeTime = child.lastWriteTime;
                rec->EndOfFile = child.endOfFile;
                rec->AllocationSize = child.allocationSize;
                rec->FileAttributes = child.attributes;
                rec->FileNameLength = nameBytes;
                memcpy(rec->FileName, child.name.data(), nameBytes);
                break;
            }
            case FileNamesInformation:
            {
                auto* rec = reinterpret_cast<FileNamesInformationRecord*>(dst);
                rec->FileNameLength = nameBytes;
                memcpy(rec->FileName, child.name.data(), nameBytes);
                break;
            }
            default:
                break;
            }
        }

        NTSTATUS synthesizeListing(
            DirEnumState& state,
            PVOID buffer,
            ULONG length,
            int infoClass,
            bool returnSingleEntry,
            PIO_STATUS_BLOCK ioStatus)
        {
            const ULONG base = entryBaseSize(infoClass);
            if (base == 0)
            {
                return StatusNoSuchFile; // signals "fall back to the real API"
            }

            BYTE* output = static_cast<BYTE*>(buffer);
            ULONG offset = 0;
            ULONG infoLength = 0;
            ULONG count = 0;
            BYTE* previous = nullptr;

            while (state.index < state.entries.size())
            {
                const DirChild& child = state.entries[state.index];
                const ULONG nameBytes = static_cast<ULONG>(child.name.size() * sizeof(wchar_t));
                const ULONG recordSize = base + nameBytes;

                if (offset + recordSize > length)
                {
                    if (count == 0)
                    {
                        ioStatus->Information = 0;
                        ioStatus->Status = StatusBufferOverflow;
                        return StatusBufferOverflow;
                    }
                    break;
                }

                BYTE* destination = output + offset;
                writeEntryRecord(infoClass, destination, child, nameBytes);
                *reinterpret_cast<ULONG*>(destination) = 0; // NextEntryOffset

                if (previous != nullptr)
                {
                    *reinterpret_cast<ULONG*>(previous) = static_cast<ULONG>(destination - previous);
                }
                previous = destination;

                infoLength = offset + recordSize;
                offset += (recordSize + 7) & ~7u;
                ++count;
                ++state.index;

                if (returnSingleEntry)
                {
                    break;
                }
            }

            if (count == 0)
            {
                ioStatus->Information = 0;
                ioStatus->Status = StatusNoMoreFiles;
                return StatusNoMoreFiles;
            }

            ioStatus->Information = infoLength;
            ioStatus->Status = StatusSuccess;
            return StatusSuccess;
        }

        bool dosPathFromRootAndName(
            HANDLE rootDirectory,
            const std::wstring& objectName,
            std::wstring& dosPath)
        {
            if (objectName.empty())
            {
                return false;
            }

            UNICODE_STRING unicode{};
            unicode.Buffer = const_cast<PWSTR>(objectName.c_str());
            unicode.Length = static_cast<USHORT>(objectName.size() * sizeof(wchar_t));
            unicode.MaximumLength = static_cast<USHORT>(unicode.Length + sizeof(wchar_t));

            OBJECT_ATTRIBUTES attributes{};
            attributes.Length = sizeof(attributes);
            attributes.RootDirectory = rootDirectory;
            attributes.ObjectName = &unicode;
            return dosPathFromObjectAttributes(&attributes, dosPath);
        }

        bool renameTargetDosPath(
            HANDLE sourceHandle,
            HANDLE rootDirectory,
            const std::wstring& objectName,
            std::wstring& dosPath)
        {
            if (rootDirectory != nullptr || isAbsoluteObjectName(objectName))
            {
                return dosPathFromRootAndName(rootDirectory, objectName, dosPath);
            }

            return virtualDosPathFromTrackedFileHandle(sourceHandle, objectName, dosPath) ||
                dosPathFromRootAndName(rootDirectory, objectName, dosPath);
        }

        bool overwritePathForMountedDosPath(const std::wstring& dosPath, std::wstring& overwritePath)
        {
            TargetMatch match;
            if (!underMountedTarget(dosPath, match) || match.mountIndex >= g_mounts.size())
            {
                return false;
            }

            const RuntimeMount& mount = g_mounts[match.mountIndex];
            if (mount.overwrite.empty())
            {
                return false;
            }

            overwritePath = overwritePathForRel(mount, VfsTree::normalizeRel(match.rel));
            makeParentDirectories(overwritePath);
            return true;
        }

        struct RewrittenInformation
        {
            std::vector<BYTE> storage;

            [[nodiscard]] PVOID data() noexcept
            {
                return storage.empty() ? nullptr : storage.data();
            }

            [[nodiscard]] ULONG size() const noexcept
            {
                return static_cast<ULONG>(storage.size());
            }
        };

        bool rewriteRenameInformation(
            HANDLE sourceHandle,
            PVOID fileInformation,
            ULONG length,
            ULONG informationClass,
            RewrittenInformation& rewritten)
        {
            const bool extended = informationClass == FileRenameInformationEx;
            const ULONG base = extended
                ? static_cast<ULONG>(offsetof(FileRenameInformationExData, FileName))
                : static_cast<ULONG>(offsetof(FileRenameInformationData, FileName));
            if (fileInformation == nullptr || length < base)
            {
                return false;
            }

            HANDLE rootDirectory = nullptr;
            ULONG fileNameLength = 0;
            const WCHAR* fileName = nullptr;
            if (extended)
            {
                const auto* source = static_cast<const FileRenameInformationExData*>(fileInformation);
                rootDirectory = source->RootDirectory;
                fileNameLength = source->FileNameLength;
                fileName = source->FileName;
            }
            else
            {
                const auto* source = static_cast<const FileRenameInformationData*>(fileInformation);
                rootDirectory = source->RootDirectory;
                fileNameLength = source->FileNameLength;
                fileName = source->FileName;
            }

            if (fileNameLength == 0 || fileNameLength > length - base)
            {
                return false;
            }

            const std::wstring targetName(fileName, fileNameLength / sizeof(wchar_t));
            std::wstring targetDosPath;
            if (!renameTargetDosPath(sourceHandle, rootDirectory, targetName, targetDosPath))
            {
                return false;
            }

            std::wstring redirectedDosPath;
            if (!overwritePathForMountedDosPath(targetDosPath, redirectedDosPath))
            {
                return false;
            }

            const std::wstring redirectedNtPath = L"\\??\\" + redirectedDosPath;
            const ULONG redirectedNameLength =
                static_cast<ULONG>(redirectedNtPath.size() * sizeof(wchar_t));
            rewritten.storage.assign(base + redirectedNameLength, 0);

            if (extended)
            {
                const auto* source = static_cast<const FileRenameInformationExData*>(fileInformation);
                auto* target = reinterpret_cast<FileRenameInformationExData*>(rewritten.storage.data());
                target->Flags = source->Flags;
                target->RootDirectory = nullptr;
                target->FileNameLength = redirectedNameLength;
                memcpy(target->FileName, redirectedNtPath.data(), redirectedNameLength);
            }
            else
            {
                const auto* source = static_cast<const FileRenameInformationData*>(fileInformation);
                auto* target = reinterpret_cast<FileRenameInformationData*>(rewritten.storage.data());
                target->ReplaceIfExists = source->ReplaceIfExists;
                target->RootDirectory = nullptr;
                target->FileNameLength = redirectedNameLength;
                memcpy(target->FileName, redirectedNtPath.data(), redirectedNameLength);
            }

            return true;
        }

        // --- the hooks ------------------------------------------------------
        NTSTATUS NTAPI Hook_NtCreateFile(
            PHANDLE FileHandle,
            ACCESS_MASK DesiredAccess,
            POBJECT_ATTRIBUTES ObjectAttributes,
            PIO_STATUS_BLOCK IoStatusBlock,
            PLARGE_INTEGER AllocationSize,
            ULONG FileAttributes,
            ULONG ShareAccess,
            ULONG CreateDisposition,
            ULONG CreateOptions,
            PVOID EaBuffer,
            ULONG EaLength)
        {
            if (reentrant() || g_mounts.empty())
            {
                return Real_NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock,
                    AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions,
                    EaBuffer, EaLength);
            }

            Guard guard;

            std::wstring dosPath;
            TargetMatch match;
            OpenDecision decision;
            bool handled = false;
            if (dosPathFromObjectAttributes(ObjectAttributes, dosPath) && underMountedTarget(dosPath, match))
            {
                const bool writeAccess = desiredAccessWrites(DesiredAccess) || dispositionCreates(CreateDisposition);
                const bool directoryOpen = (CreateOptions & FILE_DIRECTORY_FILE) != 0;
                decision = decideOpen(match.mountIndex, match.rel, CreateDisposition, writeAccess, directoryOpen);
                handled = true;
            }

            NTSTATUS status;
            if (handled && decision.redirect)
            {
                RedirectScope redirect(ObjectAttributes, decision.path);
                status = Real_NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock,
                    AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions,
                    EaBuffer, EaLength);
            }
            else
            {
                status = Real_NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock,
                    AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions,
                    EaBuffer, EaLength);
            }

            if (handled && status == StatusSuccess && FileHandle != nullptr)
            {
                if (decision.registerMerge)
                {
                    registerDirectoryHandle(*FileHandle, decision.mountIndex, decision.relLower);
                }
                else
                {
                    registerFileHandle(*FileHandle, decision.mountIndex, decision.relLower);
                }
            }

            return status;
        }

        NTSTATUS NTAPI Hook_NtOpenFile(
            PHANDLE FileHandle,
            ACCESS_MASK DesiredAccess,
            POBJECT_ATTRIBUTES ObjectAttributes,
            PIO_STATUS_BLOCK IoStatusBlock,
            ULONG ShareAccess,
            ULONG OpenOptions)
        {
            if (reentrant() || g_mounts.empty())
            {
                return Real_NtOpenFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock,
                    ShareAccess, OpenOptions);
            }

            Guard guard;

            std::wstring dosPath;
            TargetMatch match;
            OpenDecision decision;
            bool handled = false;
            if (dosPathFromObjectAttributes(ObjectAttributes, dosPath) && underMountedTarget(dosPath, match))
            {
                const bool writeAccess = desiredAccessWrites(DesiredAccess);
                const bool directoryOpen = (OpenOptions & FILE_DIRECTORY_FILE) != 0;
                decision = decideOpen(
                    match.mountIndex,
                    match.rel,
                    /*disposition=*/1 /*FILE_OPEN*/,
                    writeAccess,
                    directoryOpen);
                handled = true;
            }

            NTSTATUS status;
            if (handled && decision.redirect)
            {
                RedirectScope redirect(ObjectAttributes, decision.path);
                status = Real_NtOpenFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock,
                    ShareAccess, OpenOptions);
            }
            else
            {
                status = Real_NtOpenFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock,
                    ShareAccess, OpenOptions);
            }

            if (handled && status == StatusSuccess && FileHandle != nullptr)
            {
                if (decision.registerMerge)
                {
                    registerDirectoryHandle(*FileHandle, decision.mountIndex, decision.relLower);
                }
                else
                {
                    registerFileHandle(*FileHandle, decision.mountIndex, decision.relLower);
                }
            }

            return status;
        }

        NTSTATUS NTAPI Hook_NtQueryAttributesFile(
            POBJECT_ATTRIBUTES ObjectAttributes,
            FileBasicInformationData* FileInformation)
        {
            if (reentrant() || g_mounts.empty())
            {
                return Real_NtQueryAttributesFile(ObjectAttributes, FileInformation);
            }

            Guard guard;

            std::wstring dosPath;
            TargetMatch match;
            if (dosPathFromObjectAttributes(ObjectAttributes, dosPath) && underMountedTarget(dosPath, match))
            {
                const OpenDecision decision =
                    decideOpen(
                        match.mountIndex,
                        match.rel,
                        /*disposition=*/1,
                        /*writeAccess=*/false,
                        /*directoryOpen=*/false);
                if (decision.redirect)
                {
                    RedirectScope redirect(ObjectAttributes, decision.path);
                    return Real_NtQueryAttributesFile(ObjectAttributes, FileInformation);
                }
            }

            return Real_NtQueryAttributesFile(ObjectAttributes, FileInformation);
        }

        NTSTATUS NTAPI Hook_NtQueryFullAttributesFile(
            POBJECT_ATTRIBUTES ObjectAttributes,
            FileNetworkOpenInformationData* FileInformation)
        {
            if (reentrant() || g_mounts.empty())
            {
                return Real_NtQueryFullAttributesFile(ObjectAttributes, FileInformation);
            }

            Guard guard;

            std::wstring dosPath;
            TargetMatch match;
            if (dosPathFromObjectAttributes(ObjectAttributes, dosPath) && underMountedTarget(dosPath, match))
            {
                const OpenDecision decision =
                    decideOpen(
                        match.mountIndex,
                        match.rel,
                        /*disposition=*/1,
                        /*writeAccess=*/false,
                        /*directoryOpen=*/false);
                if (decision.redirect)
                {
                    RedirectScope redirect(ObjectAttributes, decision.path);
                    return Real_NtQueryFullAttributesFile(ObjectAttributes, FileInformation);
                }
            }

            return Real_NtQueryFullAttributesFile(ObjectAttributes, FileInformation);
        }

        NTSTATUS handleDirectoryQuery(
            HANDLE FileHandle,
            PIO_STATUS_BLOCK IoStatusBlock,
            PVOID FileInformation,
            ULONG Length,
            ULONG FileInformationClass,
            bool returnSingleEntry,
            PUNICODE_STRING FileName,
            bool restartScan,
            bool& fellBack)
        {
            fellBack = false;

            std::scoped_lock lock(g_dirMutex);
            const auto it = g_dirStates.find(FileHandle);
            if (it == g_dirStates.end())
            {
                fellBack = true;
                return StatusSuccess;
            }

            DirEnumState& state = it->second;

            if (!state.patternInitialized || restartScan)
            {
                if (FileName != nullptr && FileName->Buffer != nullptr && FileName->Length > 0)
                {
                    state.pattern = VfsTree::toLower(std::wstring(
                        FileName->Buffer, FileName->Length / sizeof(wchar_t)));
                    state.matchAll = state.pattern == L"*" || state.pattern == L"*.*";
                }
                else
                {
                    state.matchAll = true;
                    state.pattern.clear();
                }
                state.patternInitialized = true;
                state.built = false;
            }

            if (!state.built || restartScan)
            {
                buildEnumEntries(state);
            }

            const NTSTATUS status = synthesizeListing(
                state, FileInformation, Length, static_cast<int>(FileInformationClass),
                returnSingleEntry, IoStatusBlock);

            if (status == StatusNoSuchFile)
            {
                // Unsupported information class: let the real API answer instead so
                // the game still functions (only mod-only files in this directory
                // would be missing for that exotic class).
                fellBack = true;
                return StatusSuccess;
            }

            return status;
        }

        NTSTATUS NTAPI Hook_NtQueryDirectoryFile(
            HANDLE FileHandle,
            HANDLE Event,
            PIoApcRoutine ApcRoutine,
            PVOID ApcContext,
            PIO_STATUS_BLOCK IoStatusBlock,
            PVOID FileInformation,
            ULONG Length,
            ULONG FileInformationClass,
            BOOLEAN ReturnSingleEntry,
            PUNICODE_STRING FileName,
            BOOLEAN RestartScan)
        {
            if (reentrant() || g_mounts.empty())
            {
                return Real_NtQueryDirectoryFile(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock,
                    FileInformation, Length, FileInformationClass, ReturnSingleEntry, FileName, RestartScan);
            }

            Guard guard;
            bool fellBack = false;
            const NTSTATUS status = handleDirectoryQuery(
                FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass,
                ReturnSingleEntry != 0, FileName, RestartScan != 0, fellBack);

            if (fellBack)
            {
                return Real_NtQueryDirectoryFile(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock,
                    FileInformation, Length, FileInformationClass, ReturnSingleEntry, FileName, RestartScan);
            }

            return status;
        }

        NTSTATUS NTAPI Hook_NtQueryDirectoryFileEx(
            HANDLE FileHandle,
            HANDLE Event,
            PIoApcRoutine ApcRoutine,
            PVOID ApcContext,
            PIO_STATUS_BLOCK IoStatusBlock,
            PVOID FileInformation,
            ULONG Length,
            ULONG FileInformationClass,
            ULONG QueryFlags,
            PUNICODE_STRING FileName)
        {
            if (reentrant() || g_mounts.empty())
            {
                return Real_NtQueryDirectoryFileEx(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock,
                    FileInformation, Length, FileInformationClass, QueryFlags, FileName);
            }

            Guard guard;
            const bool single = (QueryFlags & QueryFlagReturnSingleEntry) != 0;
            const bool restart = (QueryFlags & QueryFlagRestartScan) != 0;
            bool fellBack = false;
            const NTSTATUS status = handleDirectoryQuery(
                FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass,
                single, FileName, restart, fellBack);

            if (fellBack)
            {
                return Real_NtQueryDirectoryFileEx(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock,
                    FileInformation, Length, FileInformationClass, QueryFlags, FileName);
            }

            return status;
        }

        NTSTATUS NTAPI Hook_NtSetInformationFile(
            HANDLE FileHandle,
            PIO_STATUS_BLOCK IoStatusBlock,
            PVOID FileInformation,
            ULONG Length,
            ULONG FileInformationClass)
        {
            if (reentrant() || g_mounts.empty())
            {
                return Real_NtSetInformationFile(
                    FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);
            }

            Guard guard;
            if (FileInformationClass == FileRenameInformation ||
                FileInformationClass == FileRenameInformationEx)
            {
                RewrittenInformation rewritten;
                if (rewriteRenameInformation(
                        FileHandle,
                        FileInformation,
                        Length,
                        FileInformationClass,
                        rewritten))
                {
                    return Real_NtSetInformationFile(
                        FileHandle,
                        IoStatusBlock,
                        rewritten.data(),
                        rewritten.size(),
                        FileInformationClass);
                }
            }

            return Real_NtSetInformationFile(
                FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);
        }

        NTSTATUS NTAPI Hook_NtDeleteFile(POBJECT_ATTRIBUTES ObjectAttributes)
        {
            if (reentrant() || g_mounts.empty())
            {
                return Real_NtDeleteFile(ObjectAttributes);
            }

            Guard guard;

            std::wstring dosPath;
            std::wstring redirectedDosPath;
            if (dosPathFromObjectAttributes(ObjectAttributes, dosPath) &&
                overwritePathForMountedDosPath(dosPath, redirectedDosPath) &&
                GetFileAttributesW(redirectedDosPath.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                RedirectScope redirect(ObjectAttributes, redirectedDosPath);
                return Real_NtDeleteFile(ObjectAttributes);
            }

            TargetMatch match;
            if (underMountedTarget(dosPath, match))
            {
                return StatusObjectNameNotFound;
            }

            return Real_NtDeleteFile(ObjectAttributes);
        }

        NTSTATUS NTAPI Hook_NtClose(HANDLE Handle)
        {
            if (!reentrant())
            {
                unregisterDirectoryHandle(Handle);
            }
            return Real_NtClose(Handle);
        }

        BOOL WINAPI Hook_CreateProcessW(
            LPCWSTR lpApplicationName,
            LPWSTR lpCommandLine,
            LPSECURITY_ATTRIBUTES lpProcessAttributes,
            LPSECURITY_ATTRIBUTES lpThreadAttributes,
            BOOL bInheritHandles,
            DWORD dwCreationFlags,
            LPVOID lpEnvironment,
            LPCWSTR lpCurrentDirectory,
            LPSTARTUPINFOW lpStartupInfo,
            LPPROCESS_INFORMATION lpProcessInformation)
        {
            if (reentrant() || g_hookDllAnsi.empty())
            {
                return Real_CreateProcessW(lpApplicationName, lpCommandLine, lpProcessAttributes,
                    lpThreadAttributes, bInheritHandles, dwCreationFlags, lpEnvironment,
                    lpCurrentDirectory, lpStartupInfo, lpProcessInformation);
            }

            Guard guard;
            std::vector<wchar_t> unicodeEnvironment;
            std::vector<char> ansiEnvironment;
            LPVOID environment = lpEnvironment;
            if (lpEnvironment != nullptr && !g_configPath.empty())
            {
                if ((dwCreationFlags & CREATE_UNICODE_ENVIRONMENT) != 0)
                {
                    unicodeEnvironment = environmentBlockWithConfig(static_cast<const wchar_t*>(lpEnvironment));
                    if (!unicodeEnvironment.empty())
                    {
                        environment = unicodeEnvironment.data();
                    }
                }
                else
                {
                    ansiEnvironment = environmentBlockWithConfig(static_cast<const char*>(lpEnvironment));
                    if (!ansiEnvironment.empty())
                    {
                        environment = ansiEnvironment.data();
                    }
                }
            }

            // Inject the VFS into the child too, so the entire process tree shares
            // the same virtual view (script extenders, launchers, helper tools).
            if (DetourCreateProcessWithDllExW(lpApplicationName, lpCommandLine, lpProcessAttributes,
                    lpThreadAttributes, bInheritHandles, dwCreationFlags, environment, lpCurrentDirectory,
                    lpStartupInfo, lpProcessInformation, g_hookDllAnsi.c_str(),
                    reinterpret_cast<PDETOUR_CREATE_PROCESS_ROUTINEW>(Real_CreateProcessW)))
            {
                return TRUE;
            }

            // If the child cannot be injected, launching it normally would silently
            // drop the MO2-style virtual view for script extenders and launchers.
            const DWORD injectionError = GetLastError();
            VfsLog::writef(
                L"Child injection failed (error %lu); blocked unvirtualized child launch.", injectionError);
            SetLastError(injectionError);
            return FALSE;
        }

        BOOL WINAPI Hook_CreateProcessA(
            LPCSTR lpApplicationName,
            LPSTR lpCommandLine,
            LPSECURITY_ATTRIBUTES lpProcessAttributes,
            LPSECURITY_ATTRIBUTES lpThreadAttributes,
            BOOL bInheritHandles,
            DWORD dwCreationFlags,
            LPVOID lpEnvironment,
            LPCSTR lpCurrentDirectory,
            LPSTARTUPINFOA lpStartupInfo,
            LPPROCESS_INFORMATION lpProcessInformation)
        {
            if (reentrant() || g_hookDllAnsi.empty())
            {
                return Real_CreateProcessA(lpApplicationName, lpCommandLine, lpProcessAttributes,
                    lpThreadAttributes, bInheritHandles, dwCreationFlags, lpEnvironment,
                    lpCurrentDirectory, lpStartupInfo, lpProcessInformation);
            }

            Guard guard;
            std::vector<wchar_t> unicodeEnvironment;
            std::vector<char> ansiEnvironment;
            LPVOID environment = lpEnvironment;
            if (lpEnvironment != nullptr && !g_configPath.empty())
            {
                if ((dwCreationFlags & CREATE_UNICODE_ENVIRONMENT) != 0)
                {
                    unicodeEnvironment = environmentBlockWithConfig(static_cast<const wchar_t*>(lpEnvironment));
                    if (!unicodeEnvironment.empty())
                    {
                        environment = unicodeEnvironment.data();
                    }
                }
                else
                {
                    ansiEnvironment = environmentBlockWithConfig(static_cast<const char*>(lpEnvironment));
                    if (!ansiEnvironment.empty())
                    {
                        environment = ansiEnvironment.data();
                    }
                }
            }

            if (DetourCreateProcessWithDllExA(lpApplicationName, lpCommandLine, lpProcessAttributes,
                    lpThreadAttributes, bInheritHandles, dwCreationFlags, environment, lpCurrentDirectory,
                    lpStartupInfo, lpProcessInformation, g_hookDllAnsi.c_str(),
                    reinterpret_cast<PDETOUR_CREATE_PROCESS_ROUTINEA>(Real_CreateProcessA)))
            {
                return TRUE;
            }

            const DWORD injectionError = GetLastError();
            VfsLog::writef(
                L"Child injection failed (error %lu); blocked unvirtualized child launch.", injectionError);
            SetLastError(injectionError);
            return FALSE;
        }

        template <typename T>
        bool attach(T& real, T hook, const char* name, bool required = true)
        {
            if (real == nullptr)
            {
                VfsLog::writef(L"Hook target was not found: %S", name);
                return !required;
            }

            const LONG result = DetourAttach(reinterpret_cast<PVOID*>(&real), reinterpret_cast<PVOID>(hook));
            if (result != NO_ERROR)
            {
                VfsLog::writef(L"Failed to attach hook: %S (error %ld)", name, result);
                return !required;
            }

            return true;
        }

        template <typename T>
        void detach(T& real, T hook)
        {
            if (real != nullptr)
            {
                DetourDetach(reinterpret_cast<PVOID*>(&real), reinterpret_cast<PVOID>(hook));
            }
        }
    }

    bool installHooks(const VfsConfig& config)
    {
        g_mounts.clear();
        for (const VfsMountConfig& mountConfig : config.mounts)
        {
            if (!mountConfig.isValid() ||
                (mountConfig.mods.empty() && mountConfig.overwrite.empty()))
            {
                continue;
            }

            RuntimeMount mount;
            mount.tree.build(mountConfig);
            mount.targetLower = VfsTree::toLower(mount.tree.target());
            mount.overwrite = mount.tree.overwrite();
            mount.excludedRootNames = normalizedExcludedRootNames(mountConfig.excludedRootNames);
            g_mounts.push_back(std::move(mount));
        }

        std::sort(
            g_mounts.begin(),
            g_mounts.end(),
            [](const RuntimeMount& left, const RuntimeMount& right)
            {
                return left.targetLower.size() > right.targetLower.size();
            });

        if (g_mounts.empty())
        {
            return false; // nothing to virtualize
        }

        g_hookDllAnsi = toAnsi(config.hookDll);
        g_configPath = config.configPath;

        const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll == nullptr)
        {
            clearRuntimeState();
            return false;
        }

        Real_NtCreateFile = reinterpret_cast<NtCreateFileFn>(GetProcAddress(ntdll, "NtCreateFile"));
        Real_NtOpenFile = reinterpret_cast<NtOpenFileFn>(GetProcAddress(ntdll, "NtOpenFile"));
        Real_NtQueryAttributesFile = reinterpret_cast<NtQueryAttributesFileFn>(
            GetProcAddress(ntdll, "NtQueryAttributesFile"));
        Real_NtQueryFullAttributesFile = reinterpret_cast<NtQueryFullAttributesFileFn>(
            GetProcAddress(ntdll, "NtQueryFullAttributesFile"));
        Real_NtQueryDirectoryFile = reinterpret_cast<NtQueryDirectoryFileFn>(
            GetProcAddress(ntdll, "NtQueryDirectoryFile"));
        Real_NtQueryDirectoryFileEx = reinterpret_cast<NtQueryDirectoryFileExFn>(
            GetProcAddress(ntdll, "NtQueryDirectoryFileEx"));
        Real_NtSetInformationFile = reinterpret_cast<NtSetInformationFileFn>(
            GetProcAddress(ntdll, "NtSetInformationFile"));
        Real_NtDeleteFile = reinterpret_cast<NtDeleteFileFn>(GetProcAddress(ntdll, "NtDeleteFile"));
        Real_NtClose = reinterpret_cast<NtCloseFn>(GetProcAddress(ntdll, "NtClose"));

        if (DetourTransactionBegin() != NO_ERROR)
        {
            clearRuntimeState();
            return false;
        }
        DetourUpdateThread(GetCurrentThread());

        bool hooksAttached = true;
        hooksAttached &= attach(Real_NtCreateFile, &Hook_NtCreateFile, "NtCreateFile");
        hooksAttached &= attach(Real_NtOpenFile, &Hook_NtOpenFile, "NtOpenFile");
        hooksAttached &= attach(Real_NtQueryAttributesFile, &Hook_NtQueryAttributesFile, "NtQueryAttributesFile");
        hooksAttached &= attach(Real_NtQueryFullAttributesFile, &Hook_NtQueryFullAttributesFile, "NtQueryFullAttributesFile");
        hooksAttached &= attach(Real_NtQueryDirectoryFile, &Hook_NtQueryDirectoryFile, "NtQueryDirectoryFile");
        hooksAttached &= attach(
            Real_NtQueryDirectoryFileEx,
            &Hook_NtQueryDirectoryFileEx,
            "NtQueryDirectoryFileEx",
            false);
        hooksAttached &= attach(Real_NtSetInformationFile, &Hook_NtSetInformationFile, "NtSetInformationFile");
        hooksAttached &= attach(Real_NtDeleteFile, &Hook_NtDeleteFile, "NtDeleteFile");
        hooksAttached &= attach(Real_NtClose, &Hook_NtClose, "NtClose");
        hooksAttached &= attach(Real_CreateProcessW, &Hook_CreateProcessW, "CreateProcessW");
        hooksAttached &= attach(Real_CreateProcessA, &Hook_CreateProcessA, "CreateProcessA");
        if (!hooksAttached)
        {
            DetourTransactionAbort();
            clearRuntimeState();
            return false;
        }

        const LONG result = DetourTransactionCommit();
        if (result != NO_ERROR)
        {
            VfsLog::writef(L"DetourTransactionCommit failed: %ld", result);
            clearRuntimeState();
            return false;
        }

        VfsLog::write(L"Virtual file system hooks installed.");
        return true;
    }

    void uninstallHooks()
    {
        if (DetourTransactionBegin() != NO_ERROR)
        {
            return;
        }
        DetourUpdateThread(GetCurrentThread());

        detach(Real_NtCreateFile, &Hook_NtCreateFile);
        detach(Real_NtOpenFile, &Hook_NtOpenFile);
        detach(Real_NtQueryAttributesFile, &Hook_NtQueryAttributesFile);
        detach(Real_NtQueryFullAttributesFile, &Hook_NtQueryFullAttributesFile);
        detach(Real_NtQueryDirectoryFile, &Hook_NtQueryDirectoryFile);
        detach(Real_NtQueryDirectoryFileEx, &Hook_NtQueryDirectoryFileEx);
        detach(Real_NtSetInformationFile, &Hook_NtSetInformationFile);
        detach(Real_NtDeleteFile, &Hook_NtDeleteFile);
        detach(Real_NtClose, &Hook_NtClose);
        detach(Real_CreateProcessW, &Hook_CreateProcessW);
        detach(Real_CreateProcessA, &Hook_CreateProcessA);

        DetourTransactionCommit();

        clearRuntimeState();
    }
}
