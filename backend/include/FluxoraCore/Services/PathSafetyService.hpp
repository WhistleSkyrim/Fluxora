#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fluxora
{
    enum class PathSafetyIssueCode
    {
        EmptyPath,
        AbsolutePathRequired,
        RelativePathRequired,
        ParentTraversal,
        RootedArchivePath,
        UnsafeCharacter,
        ControlCharacter,
        TrailingSpaceOrDot,
        ReservedWindowsName,
        UnknownRoot,
        SystemPath,
        PathTooLong,
        ComponentTooLong,
        OutsideAllowedRoot,
        SymlinkEscape,
        NotDirectory,
        PermissionDenied,
        InsufficientSpace,
        FilesystemError
    };

    struct PathSafetyIssue
    {
        PathSafetyIssueCode code{PathSafetyIssueCode::FilesystemError};
        std::filesystem::path path;
        std::wstring message;
    };

    struct PathSafetyResult
    {
        std::filesystem::path canonicalPath;
        std::filesystem::path normalizedRelativePath;
        std::uintmax_t availableBytes{0};
        std::vector<PathSafetyIssue> issues;

        [[nodiscard]] bool safe() const noexcept
        {
            return issues.empty();
        }

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return safe();
        }

        [[nodiscard]] std::wstring message() const;
        void throwIfUnsafe(std::string_view context) const;
    };

    struct PathSafetyWriteOptions
    {
        std::uintmax_t requiredBytes{0};
        bool requireWritableProbe{false};
    };

    class PathSafetyService final
    {
    public:
        [[nodiscard]] PathSafetyResult validateRelativePath(
            const std::filesystem::path& path) const;

        [[nodiscard]] PathSafetyResult validateArchiveEntryPath(
            const std::filesystem::path& path,
            bool isDirectory = false) const;

        [[nodiscard]] std::wstring archiveEntryComparisonKey(
            const std::filesystem::path& path) const;

        [[nodiscard]] PathSafetyResult validateContainedPath(
            const std::filesystem::path& allowedRoot,
            const std::filesystem::path& candidate) const;

        [[nodiscard]] PathSafetyResult validateDirectoryWriteRoot(
            const std::filesystem::path& directory,
            const PathSafetyWriteOptions& options = {}) const;

        [[nodiscard]] PathSafetyResult validateWritePath(
            const std::filesystem::path& allowedRoot,
            const std::filesystem::path& targetPath,
            const PathSafetyWriteOptions& options = {}) const;

        [[nodiscard]] std::filesystem::path canonicalize(
            const std::filesystem::path& path) const;

        [[nodiscard]] bool isSameOrInside(
            const std::filesystem::path& candidate,
            const std::filesystem::path& allowedRoot) const;
    };
}
