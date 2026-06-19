#include "FluxoraCore/Services/PathSafetyService.hpp"
#include "TestFilesystem.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fluxora::tests
{
    namespace
    {
        [[nodiscard]] bool hasIssue(
            const PathSafetyResult& result,
            PathSafetyIssueCode code)
        {
            return std::any_of(
                result.issues.begin(),
                result.issues.end(),
                [code](const PathSafetyIssue& issue)
                {
                    return issue.code == code;
                });
        }
    }

    TEST(PathSafetyServiceTests, ArchiveEntryRejectsTraversalAndRootedPaths)
    {
        const PathSafetyService safety;

        const PathSafetyResult traversal = safety.validateArchiveEntryPath(L"Data/../escaped.txt");
        EXPECT_FALSE(traversal.safe());
        EXPECT_TRUE(hasIssue(traversal, PathSafetyIssueCode::ParentTraversal));

#ifdef _WIN32
        const PathSafetyResult absolute = safety.validateArchiveEntryPath(L"C:/Windows/win.ini");
        EXPECT_FALSE(absolute.safe());
        EXPECT_TRUE(hasIssue(absolute, PathSafetyIssueCode::RootedArchivePath));

        const PathSafetyResult rooted = safety.validateArchiveEntryPath(L"/Windows/win.ini");
        EXPECT_FALSE(rooted.safe());
        EXPECT_TRUE(hasIssue(rooted, PathSafetyIssueCode::RootedArchivePath));
#else
        const PathSafetyResult absolute = safety.validateArchiveEntryPath(L"/etc/passwd");
        EXPECT_FALSE(absolute.safe());
        EXPECT_TRUE(hasIssue(absolute, PathSafetyIssueCode::RootedArchivePath));
#endif

        EXPECT_TRUE(safety.validateArchiveEntryPath(L"Data/SkyUI_SE.esp").safe());
    }

    TEST(PathSafetyServiceTests, RelativePathRejectsReservedWindowsNamesAndUnsafeComponents)
    {
        const PathSafetyService safety;
        const std::vector<std::filesystem::path> unsafePaths{
            L"Data/CON.txt",
            L"Data/NUL",
            L"Data/AUX.ini",
            L"Data/COM1",
            L"Data/LPT9/readme.txt",
            L"Data/trailing-dot.",
            L"Data/trailing-space "
        };

        for (const std::filesystem::path& path : unsafePaths)
        {
            const PathSafetyResult result = safety.validateRelativePath(path);
            EXPECT_FALSE(result.safe()) << path.wstring();
        }
    }

    TEST(PathSafetyServiceTests, LongPathValidationAllowsLongValidPathsAndRejectsOversizedComponents)
    {
        const PathSafetyService safety;
        const std::wstring validLongSegment(180, L'a');
        const std::wstring validFileName(90, L'b');
        EXPECT_TRUE(safety.validateRelativePath(
            std::filesystem::path(validLongSegment) / std::filesystem::path(validFileName + L".txt")).safe());

        const std::wstring tooLongSegment(256, L'c');
        const PathSafetyResult tooLong =
            safety.validateRelativePath(std::filesystem::path(tooLongSegment) / L"file.txt");
        EXPECT_FALSE(tooLong.safe());
        EXPECT_TRUE(hasIssue(tooLong, PathSafetyIssueCode::ComponentTooLong));

        TempDirectory temp;
        const std::filesystem::path root = temp.path() / L"project";
        std::filesystem::create_directories(root);
        const std::filesystem::path longTarget =
            root / std::filesystem::path(validLongSegment) / std::filesystem::path(validFileName + L".txt");
        EXPECT_TRUE(safety.validateWritePath(root, longTarget).safe());
    }

    TEST(PathSafetyServiceTests, WritePathValidatesFreeDiskSpace)
    {
        TempDirectory temp;
        const std::filesystem::path root = temp.path() / L"project";
        std::filesystem::create_directories(root);

        const PathSafetyResult result = PathSafetyService().validateWritePath(
            root,
            root / L"huge.bin",
            PathSafetyWriteOptions{(std::numeric_limits<std::uintmax_t>::max)(), false});

        EXPECT_FALSE(result.safe());
        EXPECT_TRUE(hasIssue(result, PathSafetyIssueCode::InsufficientSpace));
    }

    TEST(PathSafetyServiceTests, WritePathBlocksSymlinkOrJunctionEscape)
    {
        TempDirectory temp;
        const std::filesystem::path project = temp.path() / L"project";
        const std::filesystem::path outside = temp.path() / L"outside";
        const std::filesystem::path link = project / L"mods-link";
        std::filesystem::create_directories(project);
        std::filesystem::create_directories(outside);

        std::error_code linkError;
        std::filesystem::create_directory_symlink(outside, link, linkError);
#ifdef _WIN32
        if (linkError)
        {
            std::filesystem::remove(link);
            if (!createDirectoryJunction(outside, link, linkError))
            {
                GTEST_SKIP() << "Directory symlink or junction creation is not available: "
                             << linkError.message();
            }
        }
#else
        if (linkError)
        {
            GTEST_SKIP() << "Directory symlink creation is not available in this test environment: "
                         << linkError.message();
        }
#endif

        const PathSafetyResult result =
            PathSafetyService().validateWritePath(project, link / L"escaped.txt");

        EXPECT_FALSE(result.safe());
        EXPECT_TRUE(hasIssue(result, PathSafetyIssueCode::SymlinkEscape));

        std::filesystem::remove(link);
    }

#ifdef _WIN32
    TEST(PathSafetyServiceTests, DirectoryWriteRootBlocksWindowsSystemFolders)
    {
        std::wstring windows(MAX_PATH, L'\0');
        const UINT length = GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size()));
        if (length == 0 || length >= windows.size())
        {
            GTEST_SKIP() << "Windows directory could not be resolved.";
        }
        windows.resize(length);

        const PathSafetyResult result =
            PathSafetyService().validateDirectoryWriteRoot(std::filesystem::path(windows));

        EXPECT_FALSE(result.safe());
        EXPECT_TRUE(hasIssue(result, PathSafetyIssueCode::SystemPath));
    }
#endif
}
