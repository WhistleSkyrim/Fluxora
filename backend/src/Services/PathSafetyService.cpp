#include "FluxoraCore/Services/PathSafetyService.hpp"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fluxora
{
    namespace
    {
#ifdef _WIN32
        constexpr std::size_t maxWindowsPathLength = 32767;
        constexpr std::size_t maxWindowsComponentLength = 255;
#else
        constexpr std::size_t maxWindowsPathLength = 4096;
        constexpr std::size_t maxWindowsComponentLength = 255;
#endif

        [[nodiscard]] std::wstring toLower(std::wstring value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character)
            {
#ifdef _WIN32
                return static_cast<wchar_t>(::towlower(character));
#else
                return character >= L'A' && character <= L'Z'
                    ? static_cast<wchar_t>(character - L'A' + L'a')
                    : character;
#endif
            });
            return value;
        }

        [[nodiscard]] std::wstring trimWhitespace(std::wstring value)
        {
            const auto first = value.find_first_not_of(L" \t\r\n");
            if (first == std::wstring::npos)
            {
                return {};
            }

            const auto last = value.find_last_not_of(L" \t\r\n");
            return value.substr(first, last - first + 1);
        }

        [[nodiscard]] std::string toNarrow(std::wstring_view value)
        {
            std::string narrow;
            narrow.reserve(value.size());
            for (const wchar_t character : value)
            {
                narrow.push_back(character <= 0x7F ? static_cast<char>(character) : '?');
            }

            return narrow;
        }

        void addIssue(
            PathSafetyResult& result,
            PathSafetyIssueCode code,
            const std::filesystem::path& path,
            std::wstring message)
        {
            result.issues.push_back(PathSafetyIssue{code, path, std::move(message)});
        }

        [[nodiscard]] bool hasParentTraversal(const std::filesystem::path& path)
        {
            return std::any_of(
                path.begin(),
                path.end(),
                [](const std::filesystem::path& part)
                {
                    return part == L"..";
                });
        }

        [[nodiscard]] bool isRootComponent(
            const std::filesystem::path& path,
            const std::filesystem::path& part)
        {
            return (!path.root_name().empty() && part == path.root_name()) ||
                (!path.root_directory().empty() && part == path.root_directory());
        }

        [[nodiscard]] bool isReservedWindowsName(std::wstring component)
        {
            while (!component.empty() && (component.back() == L' ' || component.back() == L'.'))
            {
                component.pop_back();
            }

            const std::size_t extension = component.find(L'.');
            if (extension != std::wstring::npos)
            {
                component = component.substr(0, extension);
            }

            const std::wstring normalized = toLower(std::move(component));
            if (normalized == L"con" ||
                normalized == L"prn" ||
                normalized == L"aux" ||
                normalized == L"nul")
            {
                return true;
            }

            if (normalized.size() == 4 &&
                (normalized.starts_with(L"com") || normalized.starts_with(L"lpt")) &&
                normalized[3] >= L'1' &&
                normalized[3] <= L'9')
            {
                return true;
            }

            return false;
        }

        void validatePathLength(
            PathSafetyResult& result,
            const std::filesystem::path& path)
        {
            if (path.wstring().size() > maxWindowsPathLength)
            {
                addIssue(
                    result,
                    PathSafetyIssueCode::PathTooLong,
                    path,
                    L"Path is longer than the supported platform limit.");
            }

            for (const std::filesystem::path& part : path)
            {
                if (isRootComponent(path, part))
                {
                    continue;
                }

                if (part.wstring().size() > maxWindowsComponentLength)
                {
                    addIssue(
                        result,
                        PathSafetyIssueCode::ComponentTooLong,
                        path,
                        L"Path contains a component longer than the supported platform limit.");
                    return;
                }
            }
        }

        void validatePathComponents(
            PathSafetyResult& result,
            const std::filesystem::path& path,
            bool skipRootComponents)
        {
            static constexpr std::wstring_view invalidCharacters = L"<>:\"|?*";

            for (const std::filesystem::path& part : path)
            {
                if (skipRootComponents && isRootComponent(path, part))
                {
                    continue;
                }

                const std::wstring component = part.wstring();
                if (component.empty())
                {
                    addIssue(
                        result,
                        PathSafetyIssueCode::EmptyPath,
                        path,
                        L"Path contains an empty component.");
                    continue;
                }

                if (component == L".")
                {
                    addIssue(
                        result,
                        PathSafetyIssueCode::RelativePathRequired,
                        path,
                        L"Path cannot contain current-directory components.");
                    continue;
                }

                if (component == L"..")
                {
                    addIssue(
                        result,
                        PathSafetyIssueCode::ParentTraversal,
                        path,
                        L"Path cannot contain parent-directory traversal.");
                    continue;
                }

                if (component.find_first_of(invalidCharacters) != std::wstring::npos)
                {
                    addIssue(
                        result,
                        PathSafetyIssueCode::UnsafeCharacter,
                        path,
                        L"Path contains a character Windows cannot safely materialize.");
                }

                if (std::any_of(component.begin(), component.end(), [](wchar_t character)
                    {
                        return character >= 0 && character < 32;
                    }))
                {
                    addIssue(
                        result,
                        PathSafetyIssueCode::ControlCharacter,
                        path,
                        L"Path contains a control character.");
                }

                if (component.back() == L'.' || component.back() == L' ')
                {
                    addIssue(
                        result,
                        PathSafetyIssueCode::TrailingSpaceOrDot,
                        path,
                        L"Path components cannot end with a space or dot on Windows.");
                }

                if (isReservedWindowsName(component))
                {
                    addIssue(
                        result,
                        PathSafetyIssueCode::ReservedWindowsName,
                        path,
                        L"Path contains a reserved Windows device name.");
                }
            }
        }

        [[nodiscard]] std::filesystem::path absoluteOrLexical(const std::filesystem::path& path)
        {
            std::error_code error;
            const std::filesystem::path absolute = path.is_absolute()
                ? path
                : std::filesystem::absolute(path, error);
            return error ? path.lexically_normal() : absolute.lexically_normal();
        }

        [[nodiscard]] std::filesystem::path nearestExistingPath(std::filesystem::path path)
        {
            path = absoluteOrLexical(std::move(path));
            while (!path.empty())
            {
                std::error_code error;
                if (std::filesystem::exists(path, error))
                {
                    return path;
                }

                const std::filesystem::path parent = path.parent_path();
                if (parent.empty() || parent == path)
                {
                    break;
                }

                path = parent;
            }

            return {};
        }

        [[nodiscard]] std::wstring comparisonKey(const std::filesystem::path& path)
        {
            std::wstring key = path.lexically_normal().wstring();
            std::replace(key.begin(), key.end(), L'/', L'\\');
            while (key.size() > 1 && (key.back() == L'\\' || key.back() == L'/'))
            {
                key.pop_back();
            }

#ifdef _WIN32
            key = toLower(std::move(key));
#endif
            return key;
        }

        [[nodiscard]] bool sameOrInsideByText(
            const std::filesystem::path& candidate,
            const std::filesystem::path& root)
        {
            const std::wstring candidateKey = comparisonKey(candidate);
            const std::wstring rootKey = comparisonKey(root);
            if (candidateKey.empty() || rootKey.empty())
            {
                return false;
            }
            if (candidateKey == rootKey)
            {
                return true;
            }
            if (candidateKey.size() <= rootKey.size())
            {
                return false;
            }

            const wchar_t separator = candidateKey[rootKey.size()];
            return (separator == L'\\' || separator == L'/') &&
                candidateKey.compare(0, rootKey.size(), rootKey) == 0;
        }

        [[nodiscard]] bool hasKnownRoot(const std::filesystem::path& path)
        {
            if (!path.is_absolute())
            {
                return false;
            }

            const std::filesystem::path root = path.root_path();
            if (root.empty())
            {
                return false;
            }

            std::error_code error;
            return std::filesystem::exists(root, error) && !error;
        }

#ifdef _WIN32
        [[nodiscard]] std::optional<std::wstring> environmentVariable(const wchar_t* name)
        {
            const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
            if (required == 0)
            {
                return std::nullopt;
            }

            std::wstring value(required, L'\0');
            const DWORD actual = GetEnvironmentVariableW(name, value.data(), required);
            if (actual == 0 || actual >= required)
            {
                return std::nullopt;
            }

            value.resize(actual);
            return value;
        }

        template <typename Function>
        [[nodiscard]] std::optional<std::filesystem::path> knownFolder(Function function)
        {
            std::wstring buffer(MAX_PATH, L'\0');
            const UINT length = function(buffer.data(), static_cast<UINT>(buffer.size()));
            if (length == 0 || length >= buffer.size())
            {
                return std::nullopt;
            }

            buffer.resize(length);
            return std::filesystem::path(buffer);
        }
#endif

        [[nodiscard]] std::vector<std::filesystem::path> systemDirectories()
        {
            std::vector<std::filesystem::path> directories;

#ifdef _WIN32
            if (auto path = knownFolder(GetWindowsDirectoryW))
            {
                directories.push_back(path.value());
            }
            if (auto path = knownFolder(GetSystemDirectoryW))
            {
                directories.push_back(path.value());
            }
            for (const wchar_t* name : {L"ProgramFiles", L"ProgramFiles(x86)", L"ProgramData"})
            {
                if (const auto value = environmentVariable(name))
                {
                    directories.emplace_back(value.value());
                }
            }
#else
            directories.emplace_back("/bin");
            directories.emplace_back("/sbin");
            directories.emplace_back("/etc");
            directories.emplace_back("/usr");
            directories.emplace_back("/var");
#endif

            return directories;
        }

        [[nodiscard]] bool isSystemPath(
            const std::filesystem::path& candidate,
            const PathSafetyService& safety)
        {
            for (const std::filesystem::path& systemDirectory : systemDirectories())
            {
                if (!systemDirectory.empty() && safety.isSameOrInside(candidate, systemDirectory))
                {
                    return true;
                }
            }

            return false;
        }

        void validateAbsolutePath(
            PathSafetyResult& result,
            const std::filesystem::path& path,
            const PathSafetyService& safety,
            bool blockSystemPath)
        {
            if (path.empty())
            {
                addIssue(result, PathSafetyIssueCode::EmptyPath, path, L"Path is required.");
                return;
            }

            if (!path.is_absolute())
            {
                addIssue(
                    result,
                    PathSafetyIssueCode::AbsolutePathRequired,
                    path,
                    L"Path must be absolute.");
            }
            else if (!hasKnownRoot(path))
            {
                addIssue(
                    result,
                    PathSafetyIssueCode::UnknownRoot,
                    path,
                    L"Path root does not exist or cannot be inspected.");
            }

            validatePathLength(result, path);
            validatePathComponents(result, path, true);

            if (blockSystemPath && path.is_absolute() && isSystemPath(path, safety))
            {
                addIssue(
                    result,
                    PathSafetyIssueCode::SystemPath,
                    path,
                    L"Writes to system folders are blocked.");
            }
        }

        void validateWritableExistingDirectory(
            PathSafetyResult& result,
            const std::filesystem::path& directory,
            bool requireWritableProbe)
        {
            std::error_code error;
            const std::filesystem::file_status status = std::filesystem::status(directory, error);
            if (error)
            {
                addIssue(
                    result,
                    PathSafetyIssueCode::FilesystemError,
                    directory,
                    L"Directory permissions could not be inspected.");
                return;
            }

            if (!std::filesystem::is_directory(status))
            {
                addIssue(
                    result,
                    PathSafetyIssueCode::NotDirectory,
                    directory,
                    L"Nearest existing write target is not a directory.");
                return;
            }

            const std::filesystem::perms permissions = status.permissions();
            const bool hasWriteBit =
                (permissions & std::filesystem::perms::owner_write) != std::filesystem::perms::none ||
                (permissions & std::filesystem::perms::group_write) != std::filesystem::perms::none ||
                (permissions & std::filesystem::perms::others_write) != std::filesystem::perms::none;
            if (!hasWriteBit)
            {
                addIssue(
                    result,
                    PathSafetyIssueCode::PermissionDenied,
                    directory,
                    L"Directory does not expose writable permissions.");
                return;
            }

            if (!requireWritableProbe)
            {
                return;
            }

            const std::filesystem::path probe =
                directory / std::filesystem::path(L".fluxora-path-safety-probe.tmp");
            std::ofstream file(probe, std::ios::out | std::ios::trunc | std::ios::binary);
            if (!file)
            {
                addIssue(
                    result,
                    PathSafetyIssueCode::PermissionDenied,
                    directory,
                    L"Directory cannot be written by the current process.");
                return;
            }

            file.close();
            std::filesystem::remove(probe, error);
        }

        void validateFreeSpace(
            PathSafetyResult& result,
            const std::filesystem::path& directory,
            std::uintmax_t requiredBytes)
        {
            if (requiredBytes == 0)
            {
                return;
            }

            std::error_code error;
            const std::filesystem::space_info info = std::filesystem::space(directory, error);
            if (error)
            {
                addIssue(
                    result,
                    PathSafetyIssueCode::FilesystemError,
                    directory,
                    L"Available disk space could not be inspected.");
                return;
            }

            result.availableBytes = info.available;
            if (info.available < requiredBytes)
            {
                addIssue(
                    result,
                    PathSafetyIssueCode::InsufficientSpace,
                    directory,
                    L"Not enough free disk space is available for this write.");
            }
        }
    }

    std::wstring PathSafetyResult::message() const
    {
        std::wstring text;
        for (const PathSafetyIssue& issue : issues)
        {
            if (!text.empty())
            {
                text += L" ";
            }

            text += issue.message;
        }

        return text;
    }

    void PathSafetyResult::throwIfUnsafe(std::string_view context) const
    {
        if (safe())
        {
            return;
        }

        const std::wstring reason = message();
        std::string text(context);
        if (!reason.empty())
        {
            text += ": ";
            text += toNarrow(reason);
        }

        throw std::invalid_argument(text);
    }

    PathSafetyResult PathSafetyService::validateRelativePath(
        const std::filesystem::path& path) const
    {
        PathSafetyResult result;
        if (path.empty() || trimWhitespace(path.wstring()).empty())
        {
            addIssue(result, PathSafetyIssueCode::EmptyPath, path, L"Relative path is required.");
            return result;
        }

        if (path.is_absolute() || path.has_root_name() || path.has_root_directory())
        {
            addIssue(
                result,
                PathSafetyIssueCode::RelativePathRequired,
                path,
                L"Path must be relative and must not include a root.");
        }

        if (hasParentTraversal(path))
        {
            addIssue(
                result,
                PathSafetyIssueCode::ParentTraversal,
                path,
                L"Path cannot contain parent-directory traversal.");
        }

        validatePathLength(result, path);
        validatePathComponents(result, path, false);

        const std::filesystem::path normalized = path.lexically_normal();
        if (normalized.empty() || normalized == L".")
        {
            addIssue(result, PathSafetyIssueCode::EmptyPath, path, L"Relative path is required.");
        }
        if (hasParentTraversal(normalized))
        {
            addIssue(
                result,
                PathSafetyIssueCode::ParentTraversal,
                path,
                L"Path cannot escape its allowed root.");
        }

        result.normalizedRelativePath = normalized;
        return result;
    }

    PathSafetyResult PathSafetyService::validateArchiveEntryPath(
        const std::filesystem::path& path,
        bool isDirectory) const
    {
        PathSafetyResult result = validateRelativePath(path);
        if (path.is_absolute() || path.has_root_name() || path.has_root_directory())
        {
            addIssue(
                result,
                PathSafetyIssueCode::RootedArchivePath,
                path,
                L"Archive entries must not contain absolute or rooted paths.");
        }

        (void)isDirectory;
        return result;
    }

    std::wstring PathSafetyService::archiveEntryComparisonKey(
        const std::filesystem::path& path) const
    {
        std::filesystem::path normalized = path.lexically_normal();
        std::wstring key = normalized.generic_wstring();
        while (!key.empty() && key.back() == L'/')
        {
            key.pop_back();
        }

#ifdef _WIN32
        key = toLower(std::move(key));
#endif
        return key;
    }

    PathSafetyResult PathSafetyService::validateContainedPath(
        const std::filesystem::path& allowedRoot,
        const std::filesystem::path& candidate) const
    {
        PathSafetyResult result;
        if (allowedRoot.empty() || candidate.empty())
        {
            addIssue(result, PathSafetyIssueCode::EmptyPath, candidate, L"Allowed root and path are required.");
            return result;
        }

        const std::filesystem::path absoluteRoot = absoluteOrLexical(allowedRoot);
        const std::filesystem::path absoluteCandidate = absoluteOrLexical(candidate);
        validateAbsolutePath(result, absoluteRoot, *this, false);
        validateAbsolutePath(result, absoluteCandidate, *this, false);

        std::error_code rootError;
        const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(absoluteRoot, rootError);
        if (rootError)
        {
            addIssue(
                result,
                PathSafetyIssueCode::FilesystemError,
                allowedRoot,
                L"Allowed root could not be canonicalized.");
            return result;
        }

        const std::filesystem::path canonicalCandidate = canonicalize(absoluteCandidate);
        result.canonicalPath = canonicalCandidate;
        if (!sameOrInsideByText(canonicalCandidate, canonicalRoot))
        {
            const bool lexicalInside = sameOrInsideByText(absoluteCandidate.lexically_normal(), absoluteRoot.lexically_normal());
            addIssue(
                result,
                lexicalInside ? PathSafetyIssueCode::SymlinkEscape : PathSafetyIssueCode::OutsideAllowedRoot,
                candidate,
                lexicalInside
                    ? L"Path escapes its allowed root through a symlink or junction."
                    : L"Path is outside its allowed root.");
        }

        return result;
    }

    PathSafetyResult PathSafetyService::validateDirectoryWriteRoot(
        const std::filesystem::path& directory,
        const PathSafetyWriteOptions& options) const
    {
        PathSafetyResult result;
        const std::filesystem::path absoluteDirectory = absoluteOrLexical(directory);
        validateAbsolutePath(result, absoluteDirectory, *this, true);
        if (!result.safe())
        {
            return result;
        }

        const std::filesystem::path nearest = nearestExistingPath(absoluteDirectory);
        if (nearest.empty())
        {
            addIssue(
                result,
                PathSafetyIssueCode::UnknownRoot,
                directory,
                L"No existing parent directory could be inspected.");
            return result;
        }

        const std::filesystem::path canonicalDirectory = canonicalize(absoluteDirectory);
        result.canonicalPath = canonicalDirectory;
        if (isSystemPath(canonicalDirectory, *this))
        {
            addIssue(
                result,
                PathSafetyIssueCode::SystemPath,
                directory,
                L"Writes to system folders are blocked.");
        }

        validateWritableExistingDirectory(result, nearest, options.requireWritableProbe);
        validateFreeSpace(result, nearest, options.requiredBytes);
        return result;
    }

    PathSafetyResult PathSafetyService::validateWritePath(
        const std::filesystem::path& allowedRoot,
        const std::filesystem::path& targetPath,
        const PathSafetyWriteOptions& options) const
    {
        PathSafetyResult result = validateContainedPath(allowedRoot, targetPath);
        const std::filesystem::path absoluteTarget = absoluteOrLexical(targetPath);
        validateAbsolutePath(result, absoluteTarget, *this, true);

        const std::filesystem::path parent = absoluteTarget.has_filename()
            ? absoluteTarget.parent_path()
            : absoluteTarget;
        const std::filesystem::path nearest = nearestExistingPath(parent);
        if (nearest.empty())
        {
            addIssue(
                result,
                PathSafetyIssueCode::UnknownRoot,
                targetPath,
                L"No existing parent directory could be inspected.");
            return result;
        }

        validateWritableExistingDirectory(result, nearest, options.requireWritableProbe);
        validateFreeSpace(result, nearest, options.requiredBytes);
        return result;
    }

    std::filesystem::path PathSafetyService::canonicalize(
        const std::filesystem::path& path) const
    {
        if (path.empty())
        {
            return {};
        }

        const std::filesystem::path absolute = absoluteOrLexical(path);
        std::error_code error;
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(absolute, error);
        return error ? absolute.lexically_normal() : canonical.lexically_normal();
    }

    bool PathSafetyService::isSameOrInside(
        const std::filesystem::path& candidate,
        const std::filesystem::path& allowedRoot) const
    {
        if (candidate.empty() || allowedRoot.empty())
        {
            return false;
        }

        return sameOrInsideByText(canonicalize(candidate), canonicalize(allowedRoot));
    }
}
