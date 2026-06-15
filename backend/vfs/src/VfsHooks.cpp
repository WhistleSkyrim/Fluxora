#include "FluxoraVfs/VfsHooks.hpp"

#include "FluxoraVfs/NtApi.hpp"
#include "FluxoraVfs/VfsLog.hpp"
#include "FluxoraVfs/VfsTree.hpp"

#include <detours.h>

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <string>
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
        };

        std::vector<RuntimeMount> g_mounts;
        std::string g_hookDllAnsi;

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

        std::wstring stripNtPrefix(std::wstring path)
        {
            if (path.rfind(L"\\??\\", 0) == 0 || path.rfind(L"\\\\?\\", 0) == 0)
            {
                path.erase(0, 4);
            }
            return path;
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

                std::wstring leaf = objectName;
                while (!leaf.empty() && (leaf.front() == L'\\' || leaf.front() == L'/'))
                {
                    leaf.erase(leaf.begin());
                }
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
            bool writeAccess)
        {
            const RuntimeMount& mount = g_mounts[mountIndex];
            const std::wstring relN = VfsTree::normalizeRel(rel);
            const std::wstring relLower = VfsTree::toLower(relN);
            const VfsTree::PathInfo info = mount.tree.classify(relN);

            OpenDecision decision;
            decision.mountIndex = mountIndex;
            switch (info.kind)
            {
            case VfsTree::PathInfo::Kind::Directory:
                decision.registerMerge = true;
                decision.relLower = relLower;
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
                if (!mount.overwrite.empty() && info.parentVirtual)
                {
                    const std::wstring candidate = joinPath(mount.overwrite, relN);
                    const DWORD attributes = GetFileAttributesW(candidate.c_str());
                    if (attributes != INVALID_FILE_ATTRIBUTES)
                    {
                        decision.redirect = true;
                        decision.path = candidate;
                        return decision;
                    }

                    if (dispositionCreates(disposition) || writeAccess)
                    {
                        decision.redirect = true;
                        decision.path = writeRedirect(mount, relN, false, std::wstring(), disposition);
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
                mountIndex >= g_mounts.size() ||
                !g_mounts[mountIndex].tree.isVirtualDir(relLower))
            {
                return;
            }

            std::scoped_lock lock(g_dirMutex);
            DirEnumState state;
            state.mountIndex = mountIndex;
            state.relLower = relLower;
            g_dirStates[handle] = std::move(state);
        }

        void unregisterDirectoryHandle(HANDLE handle)
        {
            std::scoped_lock lock(g_dirMutex);
            g_dirStates.erase(handle);
        }

        void clearRuntimeState()
        {
            std::scoped_lock lock(g_dirMutex);
            g_dirStates.clear();
            g_mounts.clear();
            g_hookDllAnsi.clear();
        }

        // --- directory enumeration synthesis -------------------------------
        bool wildcardMatch(const std::wstring& name, const std::wstring& pattern)
        {
            // Classic iterative '*' / '?' matcher over lowercase strings.
            std::size_t n = 0;
            std::size_t p = 0;
            std::size_t star = std::wstring::npos;
            std::size_t mark = 0;

            while (n < name.size())
            {
                if (p < pattern.size() && (pattern[p] == name[n] || pattern[p] == L'?'))
                {
                    ++n;
                    ++p;
                }
                else if (p < pattern.size() && pattern[p] == L'*')
                {
                    star = p++;
                    mark = n;
                }
                else if (star != std::wstring::npos)
                {
                    p = star + 1;
                    n = ++mark;
                }
                else
                {
                    return false;
                }
            }

            while (p < pattern.size() && pattern[p] == L'*')
            {
                ++p;
            }
            return p == pattern.size();
        }

        DirChild makeDotEntry(const std::wstring& name)
        {
            DirChild dot;
            dot.name = name;
            dot.isDirectory = true;
            dot.attributes = FILE_ATTRIBUTE_DIRECTORY;
            return dot;
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
                return state.matchAll || wildcardMatch(nameLower, state.pattern);
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
                    if (matches(VfsTree::toLower(child.name)))
                    {
                        state.entries.push_back(child);
                    }
                }
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
                decision = decideOpen(match.mountIndex, match.rel, CreateDisposition, writeAccess);
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

            if (handled && decision.registerMerge && status == StatusSuccess && FileHandle != nullptr)
            {
                registerDirectoryHandle(*FileHandle, decision.mountIndex, decision.relLower);
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
                decision = decideOpen(match.mountIndex, match.rel, /*disposition=*/1 /*FILE_OPEN*/, writeAccess);
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

            if (handled && decision.registerMerge && status == StatusSuccess && FileHandle != nullptr)
            {
                registerDirectoryHandle(*FileHandle, decision.mountIndex, decision.relLower);
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
                    decideOpen(match.mountIndex, match.rel, /*disposition=*/1, /*writeAccess=*/false);
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
                    decideOpen(match.mountIndex, match.rel, /*disposition=*/1, /*writeAccess=*/false);
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
            // Inject the VFS into the child too, so the entire process tree shares
            // the same virtual view (script extenders, launchers, helper tools).
            if (DetourCreateProcessWithDllExW(lpApplicationName, lpCommandLine, lpProcessAttributes,
                    lpThreadAttributes, bInheritHandles, dwCreationFlags, lpEnvironment, lpCurrentDirectory,
                    lpStartupInfo, lpProcessInformation, g_hookDllAnsi.c_str(),
                    reinterpret_cast<PDETOUR_CREATE_PROCESS_ROUTINEW>(Real_CreateProcessW)))
            {
                return TRUE;
            }

            // Injection can fail for a child of a different bitness (this hook DLL
            // is x64-only) or when Detours cannot rewrite the child. Don't take the
            // child down with us: launch it normally, just without virtualization.
            const DWORD injectionError = GetLastError();
            VfsLog::writef(
                L"Child injection failed (error %lu); launching it unvirtualized.", injectionError);
            return Real_CreateProcessW(lpApplicationName, lpCommandLine, lpProcessAttributes,
                lpThreadAttributes, bInheritHandles, dwCreationFlags, lpEnvironment,
                lpCurrentDirectory, lpStartupInfo, lpProcessInformation);
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
            if (DetourCreateProcessWithDllExA(lpApplicationName, lpCommandLine, lpProcessAttributes,
                    lpThreadAttributes, bInheritHandles, dwCreationFlags, lpEnvironment, lpCurrentDirectory,
                    lpStartupInfo, lpProcessInformation, g_hookDllAnsi.c_str(),
                    reinterpret_cast<PDETOUR_CREATE_PROCESS_ROUTINEA>(Real_CreateProcessA)))
            {
                return TRUE;
            }

            const DWORD injectionError = GetLastError();
            VfsLog::writef(
                L"Child injection failed (error %lu); launching it unvirtualized.", injectionError);
            return Real_CreateProcessA(lpApplicationName, lpCommandLine, lpProcessAttributes,
                lpThreadAttributes, bInheritHandles, dwCreationFlags, lpEnvironment,
                lpCurrentDirectory, lpStartupInfo, lpProcessInformation);
        }

        template <typename T>
        void attach(T& real, T hook, const char* name)
        {
            if (real != nullptr)
            {
                if (DetourAttach(reinterpret_cast<PVOID*>(&real), reinterpret_cast<PVOID>(hook)) != NO_ERROR)
                {
                    VfsLog::writef(L"Failed to attach hook: %S", name);
                }
            }
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
        Real_NtClose = reinterpret_cast<NtCloseFn>(GetProcAddress(ntdll, "NtClose"));

        if (DetourTransactionBegin() != NO_ERROR)
        {
            clearRuntimeState();
            return false;
        }
        DetourUpdateThread(GetCurrentThread());

        attach(Real_NtCreateFile, &Hook_NtCreateFile, "NtCreateFile");
        attach(Real_NtOpenFile, &Hook_NtOpenFile, "NtOpenFile");
        attach(Real_NtQueryAttributesFile, &Hook_NtQueryAttributesFile, "NtQueryAttributesFile");
        attach(Real_NtQueryFullAttributesFile, &Hook_NtQueryFullAttributesFile, "NtQueryFullAttributesFile");
        attach(Real_NtQueryDirectoryFile, &Hook_NtQueryDirectoryFile, "NtQueryDirectoryFile");
        attach(Real_NtQueryDirectoryFileEx, &Hook_NtQueryDirectoryFileEx, "NtQueryDirectoryFileEx");
        attach(Real_NtClose, &Hook_NtClose, "NtClose");
        attach(Real_CreateProcessW, &Hook_CreateProcessW, "CreateProcessW");
        attach(Real_CreateProcessA, &Hook_CreateProcessA, "CreateProcessA");

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
        detach(Real_NtClose, &Hook_NtClose);
        detach(Real_CreateProcessW, &Hook_CreateProcessW);
        detach(Real_CreateProcessA, &Hook_CreateProcessA);

        DetourTransactionCommit();

        clearRuntimeState();
    }
}
