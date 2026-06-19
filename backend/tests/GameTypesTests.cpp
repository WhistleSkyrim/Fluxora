#include "FluxoraCore/GameSupport/GameTypes.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <type_traits>

namespace fluxora::tests
{
    static_assert(!std::is_same_v<GameId, UiTemplateId>);
    static_assert(!std::is_constructible_v<UiTemplateId, GameId>);
    static_assert(!std::is_constructible_v<GameId, UiTemplateId>);
    static_assert(!std::is_convertible_v<GameId, UiTemplateId>);
    static_assert(!std::is_convertible_v<UiTemplateId, GameId>);
    static_assert(!std::is_same_v<ProjectRelativePath, GameRelativePath>);

    TEST(GameTypesTests, NormalizedExtensionComparesCaseInsensitively)
    {
        const NormalizedExtension upper = NormalizedExtension::parse(L".ESP").value();
        const NormalizedExtension mixed = NormalizedExtension::parse(L".Esp").value();
        const NormalizedExtension bare = NormalizedExtension::parse(L"esp").value();

        EXPECT_EQ(upper.value(), L".esp");
        EXPECT_EQ(upper, mixed);
        EXPECT_EQ(mixed, bare);
    }

    TEST(GameTypesTests, ParsingReturnsTypedErrors)
    {
        const GameTypeParseResult<GameId> missingGameId = GameId::parse(L"   ");
        ASSERT_FALSE(missingGameId);
        EXPECT_EQ(missingGameId.error().code, GameTypeErrorCode::EmptyValue);

        const GameTypeParseResult<NormalizedExtension> missingExtensionSuffix =
            NormalizedExtension::parse(L".");
        ASSERT_FALSE(missingExtensionSuffix);
        EXPECT_EQ(missingExtensionSuffix.error().code, GameTypeErrorCode::MissingExtensionSuffix);

        const GameTypeParseResult<NormalizedExtension> extensionWithSeparator =
            NormalizedExtension::parse(L"Data/.esp");
        ASSERT_FALSE(extensionWithSeparator);
        EXPECT_EQ(extensionWithSeparator.error().code, GameTypeErrorCode::PathSeparator);
    }

    TEST(GameTypesTests, GameIdAndUiTemplateIdNormalizeButStayDistinctTypes)
    {
        const GameId gameId = GameId::parse(L" SkyrimSE ").value();
        const UiTemplateId templateId = UiTemplateId::parse(L" SkyrimSE ").value();

        EXPECT_EQ(gameId.value(), L"skyrimse");
        EXPECT_EQ(templateId.value(), L"skyrimse");
    }

    TEST(GameTypesTests, ExecutableNamePreservesDisplayAndComparesNormalized)
    {
        const ExecutableName display = ExecutableName::parse(L" SkyrimSE.exe ").value();
        const ExecutableName lower = ExecutableName::parse(L"skyrimse.exe").value();

        EXPECT_EQ(display.displayName(), L"SkyrimSE.exe");
        EXPECT_EQ(display.normalizedName(), L"skyrimse.exe");
        EXPECT_EQ(display, lower);
        EXPECT_EQ(ExecutableName::parse(L"Data/SkyrimSE.exe").error().code, GameTypeErrorCode::PathSeparator);
    }

    TEST(GameTypesTests, GameRelativePathComparesEquivalentCaseInsensitivePaths)
    {
        const GameRelativePath data = GameRelativePath::parse(L"Data").value();
        const GameRelativePath lower = GameRelativePath::parse(L"data").value();
        const GameRelativePath equivalent = GameRelativePath::parse(L".\\Data\\..\\Data\\").value();

        EXPECT_EQ(data, lower);
        EXPECT_EQ(data, equivalent);
        EXPECT_EQ(data.comparisonKey(), L"data");
    }

    TEST(GameTypesTests, RelativePathCanBeCaseSensitiveWhenNeeded)
    {
        const GameRelativePath data = GameRelativePath::parse(
            L"Data",
            PathCaseSensitivity::CaseSensitive).value();
        const GameRelativePath lower = GameRelativePath::parse(
            L"data",
            PathCaseSensitivity::CaseSensitive).value();

        EXPECT_NE(data, lower);
    }

    TEST(GameTypesTests, RelativePathsRejectRootedAndEscapingValues)
    {
        const std::filesystem::path absolute = std::filesystem::current_path().root_path() / L"Fluxora";
        const GameTypeParseResult<ProjectRelativePath> rooted = ProjectRelativePath::parse(absolute);
        ASSERT_FALSE(rooted);
        EXPECT_EQ(rooted.error().code, GameTypeErrorCode::RelativePathRequired);

        const GameTypeParseResult<GameRelativePath> escaping = GameRelativePath::parse(L"..\\Data");
        ASSERT_FALSE(escaping);
        EXPECT_EQ(escaping.error().code, GameTypeErrorCode::ParentTraversal);
    }

    TEST(GameTypesTests, AbsoluteCanonicalPathComparesEquivalentCaseInsensitivePaths)
    {
        const std::filesystem::path root = std::filesystem::current_path().root_path();
        ASSERT_FALSE(root.empty());

        const AbsoluteCanonicalPath first = AbsoluteCanonicalPath::parse(
            root / L"Games" / L"Skyrim Special Edition" / L"Data" / L".." / L"Data").value();
        const AbsoluteCanonicalPath second = AbsoluteCanonicalPath::parse(
            root / L"games" / L"skyrim special edition" / L"data").value();

        EXPECT_EQ(first, second);
    }

    TEST(GameTypesTests, AbsoluteCanonicalPathRejectsRelativePath)
    {
        const GameTypeParseResult<AbsoluteCanonicalPath> relative =
            AbsoluteCanonicalPath::parse(L"Data");

        ASSERT_FALSE(relative);
        EXPECT_EQ(relative.error().code, GameTypeErrorCode::AbsolutePathRequired);
    }

    TEST(GameTypesTests, EnumParsersReturnTypedValuesAndErrors)
    {
        EXPECT_EQ(parseContentArea(L"Data").value(), ContentArea::Data);
        EXPECT_EQ(parsePlacementTarget(L"game-root").value(), PlacementTarget::GameRoot);
        EXPECT_EQ(parseHealthStatus(L"Partial").value(), HealthStatus::Partial);
        EXPECT_EQ(parseHealthSeverity(L"blocker").value(), HealthSeverity::Blocker);

        const GameTypeParseResult<HealthStatus> unsupported = parseHealthStatus(L"fine");
        ASSERT_FALSE(unsupported);
        EXPECT_EQ(unsupported.error().code, GameTypeErrorCode::UnsupportedValue);
    }
}
