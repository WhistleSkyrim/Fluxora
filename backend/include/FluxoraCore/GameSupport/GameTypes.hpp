#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace fluxora
{
    [[nodiscard]] inline wchar_t asciiLower(wchar_t value) noexcept
    {
        if (value >= L'A' && value <= L'Z')
        {
            return static_cast<wchar_t>(value - L'A' + L'a');
        }

        return value;
    }

    [[nodiscard]] inline std::wstring trimAscii(std::wstring_view value)
    {
        std::size_t start = 0;
        while (start < value.size() &&
            (value[start] == L' ' || value[start] == L'\t' || value[start] == L'\r' || value[start] == L'\n'))
        {
            ++start;
        }

        std::size_t end = value.size();
        while (end > start &&
            (value[end - 1] == L' ' || value[end - 1] == L'\t' || value[end - 1] == L'\r' || value[end - 1] == L'\n'))
        {
            --end;
        }

        return std::wstring(value.substr(start, end - start));
    }

    [[nodiscard]] inline std::wstring toAsciiLower(std::wstring_view value)
    {
        std::wstring lowered(value);
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), asciiLower);
        return lowered;
    }

    enum class GameTypeErrorCode
    {
        EmptyValue,
        UnsupportedCharacter,
        PathSeparator,
        MissingExtensionSuffix,
        AbsolutePathRequired,
        RelativePathRequired,
        ParentTraversal,
        UnsupportedValue
    };

    struct GameTypeParseError
    {
        GameTypeErrorCode code{GameTypeErrorCode::UnsupportedValue};
        std::string message;
    };

    template <typename T>
    class GameTypeParseResult final
    {
    public:
        [[nodiscard]] static GameTypeParseResult success(T value)
        {
            GameTypeParseResult result;
            result.value_ = std::move(value);
            return result;
        }

        [[nodiscard]] static GameTypeParseResult failure(GameTypeErrorCode code, std::string message)
        {
            GameTypeParseResult result;
            result.error_ = GameTypeParseError{code, std::move(message)};
            return result;
        }

        [[nodiscard]] bool hasValue() const noexcept
        {
            return value_.has_value();
        }

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return hasValue();
        }

        [[nodiscard]] const T& value() const
        {
            if (!value_.has_value())
            {
                throw std::logic_error("Tried to access a missing parsed value.");
            }

            return value_.value();
        }

        [[nodiscard]] const GameTypeParseError& error() const
        {
            if (!error_.has_value())
            {
                throw std::logic_error("Tried to access a missing parse error.");
            }

            return error_.value();
        }

        [[nodiscard]] T valueOrThrow() const
        {
            if (!value_.has_value())
            {
                throw std::invalid_argument(error_.has_value() ? error_->message : "Value parsing failed.");
            }

            return value_.value();
        }

    private:
        std::optional<T> value_;
        std::optional<GameTypeParseError> error_;
    };

    enum class PathCaseSensitivity
    {
        CaseSensitive,
        CaseInsensitive
    };

    [[nodiscard]] inline std::wstring normalizePathComparisonKey(
        const std::filesystem::path& path,
        PathCaseSensitivity caseSensitivity)
    {
        std::wstring key = path.lexically_normal().generic_wstring();
        while (key.size() > 1 && key.back() == L'/' && !(key.size() == 3 && key[1] == L':'))
        {
            key.pop_back();
        }

        if (caseSensitivity == PathCaseSensitivity::CaseInsensitive)
        {
            key = toAsciiLower(key);
        }

        return key;
    }

    [[nodiscard]] inline bool containsParentTraversal(const std::filesystem::path& path)
    {
        return std::any_of(
            path.begin(),
            path.end(),
            [](const std::filesystem::path& segment)
            {
                return segment == L"..";
            });
    }

    class GameId final
    {
    public:
        GameId() = default;

        [[nodiscard]] static GameTypeParseResult<GameId> parse(std::wstring_view value)
        {
            const std::wstring normalized = toAsciiLower(trimAscii(value));
            if (normalized.empty())
            {
                return GameTypeParseResult<GameId>::failure(
                    GameTypeErrorCode::EmptyValue,
                    "Game id is required.");
            }

            for (const wchar_t character : normalized)
            {
                const bool valid =
                    (character >= L'a' && character <= L'z') ||
                    (character >= L'0' && character <= L'9') ||
                    character == L'-' ||
                    character == L'_';
                if (!valid)
                {
                    return GameTypeParseResult<GameId>::failure(
                        GameTypeErrorCode::UnsupportedCharacter,
                        "Game id contains an unsupported character.");
                }
            }

            return GameTypeParseResult<GameId>::success(GameId(normalized));
        }

        [[nodiscard]] static GameId parseOrThrow(std::wstring_view value)
        {
            return parse(value).valueOrThrow();
        }

        [[nodiscard]] const std::wstring& value() const noexcept
        {
            return value_;
        }

        [[nodiscard]] bool empty() const noexcept
        {
            return value_.empty();
        }

        friend bool operator==(const GameId& left, const GameId& right) noexcept
        {
            return left.value_ == right.value_;
        }

        friend bool operator<(const GameId& left, const GameId& right) noexcept
        {
            return left.value_ < right.value_;
        }

    private:
        explicit GameId(std::wstring value)
            : value_(std::move(value))
        {
        }

        std::wstring value_;
    };

    class UiTemplateId final
    {
    public:
        UiTemplateId() = default;

        [[nodiscard]] static GameTypeParseResult<UiTemplateId> parse(std::wstring_view value)
        {
            const std::wstring normalized = toAsciiLower(trimAscii(value));
            if (normalized.empty())
            {
                return GameTypeParseResult<UiTemplateId>::failure(
                    GameTypeErrorCode::EmptyValue,
                    "UI template id is required.");
            }

            for (const wchar_t character : normalized)
            {
                const bool valid =
                    (character >= L'a' && character <= L'z') ||
                    (character >= L'0' && character <= L'9') ||
                    character == L'-' ||
                    character == L'_';
                if (!valid)
                {
                    return GameTypeParseResult<UiTemplateId>::failure(
                        GameTypeErrorCode::UnsupportedCharacter,
                        "UI template id contains an unsupported character.");
                }
            }

            return GameTypeParseResult<UiTemplateId>::success(UiTemplateId(normalized));
        }

        [[nodiscard]] static UiTemplateId parseOrThrow(std::wstring_view value)
        {
            return parse(value).valueOrThrow();
        }

        [[nodiscard]] const std::wstring& value() const noexcept
        {
            return value_;
        }

        friend bool operator==(const UiTemplateId& left, const UiTemplateId& right) noexcept
        {
            return left.value_ == right.value_;
        }

        friend bool operator<(const UiTemplateId& left, const UiTemplateId& right) noexcept
        {
            return left.value_ < right.value_;
        }

    private:
        explicit UiTemplateId(std::wstring value)
            : value_(std::move(value))
        {
        }

        std::wstring value_;
    };

    class NormalizedExtension final
    {
    public:
        NormalizedExtension() = default;

        [[nodiscard]] static GameTypeParseResult<NormalizedExtension> parse(std::wstring_view value)
        {
            std::wstring normalized = toAsciiLower(trimAscii(value));
            if (normalized.empty())
            {
                return GameTypeParseResult<NormalizedExtension>::failure(
                    GameTypeErrorCode::EmptyValue,
                    "File extension is required.");
            }
            if (normalized.front() != L'.')
            {
                normalized.insert(normalized.begin(), L'.');
            }
            if (normalized.size() == 1)
            {
                return GameTypeParseResult<NormalizedExtension>::failure(
                    GameTypeErrorCode::MissingExtensionSuffix,
                    "File extension must include a suffix.");
            }
            if (normalized.find_first_of(L"\\/") != std::wstring::npos)
            {
                return GameTypeParseResult<NormalizedExtension>::failure(
                    GameTypeErrorCode::PathSeparator,
                    "File extension cannot contain path separators.");
            }

            return GameTypeParseResult<NormalizedExtension>::success(NormalizedExtension(normalized));
        }

        [[nodiscard]] static NormalizedExtension parseOrThrow(std::wstring_view value)
        {
            return parse(value).valueOrThrow();
        }

        [[nodiscard]] const std::wstring& value() const noexcept
        {
            return value_;
        }

        friend bool operator==(const NormalizedExtension& left, const NormalizedExtension& right) noexcept
        {
            return left.value_ == right.value_;
        }

        friend bool operator<(const NormalizedExtension& left, const NormalizedExtension& right) noexcept
        {
            return left.value_ < right.value_;
        }

    private:
        explicit NormalizedExtension(std::wstring value)
            : value_(std::move(value))
        {
        }

        std::wstring value_;
    };

    class ExecutableName final
    {
    public:
        ExecutableName() = default;

        [[nodiscard]] static GameTypeParseResult<ExecutableName> parse(std::wstring_view value)
        {
            std::wstring preserved = trimAscii(value);
            if (preserved.empty())
            {
                return GameTypeParseResult<ExecutableName>::failure(
                    GameTypeErrorCode::EmptyValue,
                    "Executable name is required.");
            }
            if (preserved.find_first_of(L"\\/") != std::wstring::npos)
            {
                return GameTypeParseResult<ExecutableName>::failure(
                    GameTypeErrorCode::PathSeparator,
                    "Executable name cannot contain path separators.");
            }

            return GameTypeParseResult<ExecutableName>::success(
                ExecutableName(std::move(preserved), toAsciiLower(preserved)));
        }

        [[nodiscard]] static ExecutableName parseOrThrow(std::wstring_view value)
        {
            return parse(value).valueOrThrow();
        }

        [[nodiscard]] const std::wstring& displayName() const noexcept
        {
            return displayName_;
        }

        [[nodiscard]] const std::wstring& normalizedName() const noexcept
        {
            return normalizedName_;
        }

        friend bool operator==(const ExecutableName& left, const ExecutableName& right) noexcept
        {
            return left.normalizedName_ == right.normalizedName_;
        }

        friend bool operator<(const ExecutableName& left, const ExecutableName& right) noexcept
        {
            return left.normalizedName_ < right.normalizedName_;
        }

    private:
        ExecutableName(std::wstring displayName, std::wstring normalizedName)
            : displayName_(std::move(displayName)),
              normalizedName_(std::move(normalizedName))
        {
        }

        std::wstring displayName_;
        std::wstring normalizedName_;
    };

    enum class GameCapability : std::uint32_t
    {
        Plugins = 1u << 0,
        LoadOrder = 1u << 1,
        RootFiles = 1u << 2,
        Archives = 1u << 3,
        ScriptExtender = 1u << 4,
        IniProfiles = 1u << 5,
        SaveProfiles = 1u << 6,
        GameSpecificVfs = 1u << 7,
        ContentLayoutRules = 1u << 8
    };

    class CapabilitySet final
    {
    public:
        void enable(GameCapability capability) noexcept
        {
            bits_ |= static_cast<std::uint32_t>(capability);
        }

        [[nodiscard]] bool has(GameCapability capability) const noexcept
        {
            return (bits_ & static_cast<std::uint32_t>(capability)) != 0;
        }

        [[nodiscard]] std::uint32_t bits() const noexcept
        {
            return bits_;
        }

    private:
        std::uint32_t bits_{0};
    };

    class AbsoluteCanonicalPath final
    {
    public:
        AbsoluteCanonicalPath() = default;

        [[nodiscard]] static GameTypeParseResult<AbsoluteCanonicalPath> parse(
            const std::filesystem::path& value,
            PathCaseSensitivity caseSensitivity = PathCaseSensitivity::CaseInsensitive)
        {
            if (value.empty())
            {
                return GameTypeParseResult<AbsoluteCanonicalPath>::failure(
                    GameTypeErrorCode::EmptyValue,
                    "Absolute path is required.");
            }
            if (!value.is_absolute())
            {
                return GameTypeParseResult<AbsoluteCanonicalPath>::failure(
                    GameTypeErrorCode::AbsolutePathRequired,
                    "Path must be absolute.");
            }

            const std::filesystem::path normalized = value.lexically_normal();
            return GameTypeParseResult<AbsoluteCanonicalPath>::success(
                AbsoluteCanonicalPath(normalized, normalizePathComparisonKey(normalized, caseSensitivity)));
        }

        [[nodiscard]] static AbsoluteCanonicalPath parseOrThrow(
            const std::filesystem::path& value,
            PathCaseSensitivity caseSensitivity = PathCaseSensitivity::CaseInsensitive)
        {
            return parse(value, caseSensitivity).valueOrThrow();
        }

        [[nodiscard]] const std::filesystem::path& path() const noexcept
        {
            return path_;
        }

        [[nodiscard]] const std::wstring& comparisonKey() const noexcept
        {
            return comparisonKey_;
        }

        friend bool operator==(const AbsoluteCanonicalPath& left, const AbsoluteCanonicalPath& right) noexcept
        {
            return left.comparisonKey_ == right.comparisonKey_;
        }

        friend bool operator<(const AbsoluteCanonicalPath& left, const AbsoluteCanonicalPath& right) noexcept
        {
            return left.comparisonKey_ < right.comparisonKey_;
        }

    private:
        AbsoluteCanonicalPath(std::filesystem::path path, std::wstring comparisonKey)
            : path_(std::move(path)),
              comparisonKey_(std::move(comparisonKey))
        {
        }

        std::filesystem::path path_;
        std::wstring comparisonKey_;
    };

    class ProjectRelativePath final
    {
    public:
        ProjectRelativePath() = default;

        [[nodiscard]] static GameTypeParseResult<ProjectRelativePath> parse(
            const std::filesystem::path& value,
            PathCaseSensitivity caseSensitivity = PathCaseSensitivity::CaseInsensitive)
        {
            if (value.empty())
            {
                return GameTypeParseResult<ProjectRelativePath>::failure(
                    GameTypeErrorCode::EmptyValue,
                    "Project-relative path is required.");
            }
            if (value.is_absolute() || value.has_root_name() || value.has_root_directory())
            {
                return GameTypeParseResult<ProjectRelativePath>::failure(
                    GameTypeErrorCode::RelativePathRequired,
                    "Project-relative path cannot be rooted.");
            }

            const std::filesystem::path normalized = value.lexically_normal();
            if (normalized.empty() || normalized == L".")
            {
                return GameTypeParseResult<ProjectRelativePath>::failure(
                    GameTypeErrorCode::EmptyValue,
                    "Project-relative path is required.");
            }
            if (containsParentTraversal(normalized))
            {
                return GameTypeParseResult<ProjectRelativePath>::failure(
                    GameTypeErrorCode::ParentTraversal,
                    "Project-relative path cannot escape the project root.");
            }

            return GameTypeParseResult<ProjectRelativePath>::success(
                ProjectRelativePath(normalized, normalizePathComparisonKey(normalized, caseSensitivity)));
        }

        [[nodiscard]] static ProjectRelativePath parseOrThrow(
            const std::filesystem::path& value,
            PathCaseSensitivity caseSensitivity = PathCaseSensitivity::CaseInsensitive)
        {
            return parse(value, caseSensitivity).valueOrThrow();
        }

        [[nodiscard]] const std::filesystem::path& path() const noexcept
        {
            return path_;
        }

        [[nodiscard]] const std::wstring& comparisonKey() const noexcept
        {
            return comparisonKey_;
        }

        friend bool operator==(const ProjectRelativePath& left, const ProjectRelativePath& right) noexcept
        {
            return left.comparisonKey_ == right.comparisonKey_;
        }

        friend bool operator<(const ProjectRelativePath& left, const ProjectRelativePath& right) noexcept
        {
            return left.comparisonKey_ < right.comparisonKey_;
        }

    private:
        ProjectRelativePath(std::filesystem::path path, std::wstring comparisonKey)
            : path_(std::move(path)),
              comparisonKey_(std::move(comparisonKey))
        {
        }

        std::filesystem::path path_;
        std::wstring comparisonKey_;
    };

    class GameRelativePath final
    {
    public:
        GameRelativePath() = default;

        [[nodiscard]] static GameTypeParseResult<GameRelativePath> parse(
            const std::filesystem::path& value,
            PathCaseSensitivity caseSensitivity = PathCaseSensitivity::CaseInsensitive)
        {
            if (value.empty())
            {
                return GameTypeParseResult<GameRelativePath>::failure(
                    GameTypeErrorCode::EmptyValue,
                    "Game-relative path is required.");
            }
            if (value.is_absolute() || value.has_root_name() || value.has_root_directory())
            {
                return GameTypeParseResult<GameRelativePath>::failure(
                    GameTypeErrorCode::RelativePathRequired,
                    "Game-relative path cannot be rooted.");
            }

            const std::filesystem::path normalized = value.lexically_normal();
            if (normalized.empty() || normalized == L".")
            {
                return GameTypeParseResult<GameRelativePath>::failure(
                    GameTypeErrorCode::EmptyValue,
                    "Game-relative path is required.");
            }
            if (containsParentTraversal(normalized))
            {
                return GameTypeParseResult<GameRelativePath>::failure(
                    GameTypeErrorCode::ParentTraversal,
                    "Game-relative path cannot escape the game root.");
            }

            return GameTypeParseResult<GameRelativePath>::success(
                GameRelativePath(normalized, normalizePathComparisonKey(normalized, caseSensitivity)));
        }

        [[nodiscard]] static GameRelativePath parseOrThrow(
            const std::filesystem::path& value,
            PathCaseSensitivity caseSensitivity = PathCaseSensitivity::CaseInsensitive)
        {
            return parse(value, caseSensitivity).valueOrThrow();
        }

        [[nodiscard]] const std::filesystem::path& path() const noexcept
        {
            return path_;
        }

        [[nodiscard]] const std::wstring& comparisonKey() const noexcept
        {
            return comparisonKey_;
        }

        friend bool operator==(const GameRelativePath& left, const GameRelativePath& right) noexcept
        {
            return left.comparisonKey_ == right.comparisonKey_;
        }

        friend bool operator<(const GameRelativePath& left, const GameRelativePath& right) noexcept
        {
            return left.comparisonKey_ < right.comparisonKey_;
        }

    private:
        GameRelativePath(std::filesystem::path path, std::wstring comparisonKey)
            : path_(std::move(path)),
              comparisonKey_(std::move(comparisonKey))
        {
        }

        std::filesystem::path path_;
        std::wstring comparisonKey_;
    };

    enum class ContentArea
    {
        GameRoot,
        Data,
        Profile,
        Ini,
        Saves,
        Overwrite
    };

    enum class PlacementTarget
    {
        GameRoot,
        Data,
        Profile,
        Overwrite,
        Blocked
    };

    enum class LaunchTrackingKind
    {
        DirectProcess,
        ExpectedChildProcess
    };

    enum class GameExecutableWorkingDirectoryKind
    {
        ExecutableDirectory,
        GameRoot
    };

    enum class HealthStatus
    {
        Healthy,
        Warning,
        Partial,
        Unsupported,
        Broken,
        Unknown
    };

    enum class HealthSeverity
    {
        Blocker,
        Warning,
        Info
    };

    enum class DetectionConfidence
    {
        None = 0,
        Low = 1,
        Medium = 2,
        High = 3,
        Explicit = 4
    };

    [[nodiscard]] inline GameTypeParseResult<ContentArea> parseContentArea(std::wstring_view value)
    {
        const std::wstring normalized = toAsciiLower(trimAscii(value));
        if (normalized.empty())
        {
            return GameTypeParseResult<ContentArea>::failure(
                GameTypeErrorCode::EmptyValue,
                "Content area is required.");
        }
        if (normalized == L"game-root" || normalized == L"gameroot" || normalized == L"root")
        {
            return GameTypeParseResult<ContentArea>::success(ContentArea::GameRoot);
        }
        if (normalized == L"data")
        {
            return GameTypeParseResult<ContentArea>::success(ContentArea::Data);
        }
        if (normalized == L"profile")
        {
            return GameTypeParseResult<ContentArea>::success(ContentArea::Profile);
        }
        if (normalized == L"ini")
        {
            return GameTypeParseResult<ContentArea>::success(ContentArea::Ini);
        }
        if (normalized == L"saves")
        {
            return GameTypeParseResult<ContentArea>::success(ContentArea::Saves);
        }
        if (normalized == L"overwrite")
        {
            return GameTypeParseResult<ContentArea>::success(ContentArea::Overwrite);
        }

        return GameTypeParseResult<ContentArea>::failure(
            GameTypeErrorCode::UnsupportedValue,
            "Content area is unsupported.");
    }

    [[nodiscard]] inline GameTypeParseResult<PlacementTarget> parsePlacementTarget(std::wstring_view value)
    {
        const std::wstring normalized = toAsciiLower(trimAscii(value));
        if (normalized.empty())
        {
            return GameTypeParseResult<PlacementTarget>::failure(
                GameTypeErrorCode::EmptyValue,
                "Placement target is required.");
        }
        if (normalized == L"game-root" || normalized == L"gameroot" || normalized == L"root")
        {
            return GameTypeParseResult<PlacementTarget>::success(PlacementTarget::GameRoot);
        }
        if (normalized == L"data")
        {
            return GameTypeParseResult<PlacementTarget>::success(PlacementTarget::Data);
        }
        if (normalized == L"profile")
        {
            return GameTypeParseResult<PlacementTarget>::success(PlacementTarget::Profile);
        }
        if (normalized == L"overwrite")
        {
            return GameTypeParseResult<PlacementTarget>::success(PlacementTarget::Overwrite);
        }
        if (normalized == L"blocked")
        {
            return GameTypeParseResult<PlacementTarget>::success(PlacementTarget::Blocked);
        }

        return GameTypeParseResult<PlacementTarget>::failure(
            GameTypeErrorCode::UnsupportedValue,
            "Placement target is unsupported.");
    }

    [[nodiscard]] inline GameTypeParseResult<LaunchTrackingKind> parseLaunchTrackingKind(std::wstring_view value)
    {
        const std::wstring normalized = toAsciiLower(trimAscii(value));
        if (normalized.empty())
        {
            return GameTypeParseResult<LaunchTrackingKind>::failure(
                GameTypeErrorCode::EmptyValue,
                "Launch tracking kind is required.");
        }
        if (normalized == L"direct" || normalized == L"direct-process" || normalized == L"directprocess")
        {
            return GameTypeParseResult<LaunchTrackingKind>::success(LaunchTrackingKind::DirectProcess);
        }
        if (normalized == L"expected-child-process" ||
            normalized == L"expectedchildprocess" ||
            normalized == L"child-process" ||
            normalized == L"childprocess")
        {
            return GameTypeParseResult<LaunchTrackingKind>::success(LaunchTrackingKind::ExpectedChildProcess);
        }

        return GameTypeParseResult<LaunchTrackingKind>::failure(
            GameTypeErrorCode::UnsupportedValue,
            "Launch tracking kind is unsupported.");
    }

    [[nodiscard]] inline std::wstring launchTrackingKindName(LaunchTrackingKind value)
    {
        switch (value)
        {
        case LaunchTrackingKind::DirectProcess:
            return L"directProcess";
        case LaunchTrackingKind::ExpectedChildProcess:
            return L"expectedChildProcess";
        }

        return L"directProcess";
    }

    [[nodiscard]] inline GameTypeParseResult<GameExecutableWorkingDirectoryKind> parseGameExecutableWorkingDirectoryKind(
        std::wstring_view value)
    {
        const std::wstring normalized = toAsciiLower(trimAscii(value));
        if (normalized.empty())
        {
            return GameTypeParseResult<GameExecutableWorkingDirectoryKind>::failure(
                GameTypeErrorCode::EmptyValue,
                "Executable working directory kind is required.");
        }
        if (normalized == L"executable-directory" ||
            normalized == L"executabledirectory" ||
            normalized == L"executable-dir" ||
            normalized == L"executabledir")
        {
            return GameTypeParseResult<GameExecutableWorkingDirectoryKind>::success(
                GameExecutableWorkingDirectoryKind::ExecutableDirectory);
        }
        if (normalized == L"game-root" || normalized == L"gameroot" || normalized == L"game")
        {
            return GameTypeParseResult<GameExecutableWorkingDirectoryKind>::success(
                GameExecutableWorkingDirectoryKind::GameRoot);
        }

        return GameTypeParseResult<GameExecutableWorkingDirectoryKind>::failure(
            GameTypeErrorCode::UnsupportedValue,
            "Executable working directory kind is unsupported.");
    }

    [[nodiscard]] inline GameTypeParseResult<HealthStatus> parseHealthStatus(std::wstring_view value)
    {
        const std::wstring normalized = toAsciiLower(trimAscii(value));
        if (normalized.empty())
        {
            return GameTypeParseResult<HealthStatus>::failure(
                GameTypeErrorCode::EmptyValue,
                "Health status is required.");
        }
        if (normalized == L"healthy")
        {
            return GameTypeParseResult<HealthStatus>::success(HealthStatus::Healthy);
        }
        if (normalized == L"warning")
        {
            return GameTypeParseResult<HealthStatus>::success(HealthStatus::Warning);
        }
        if (normalized == L"partial")
        {
            return GameTypeParseResult<HealthStatus>::success(HealthStatus::Partial);
        }
        if (normalized == L"unsupported")
        {
            return GameTypeParseResult<HealthStatus>::success(HealthStatus::Unsupported);
        }
        if (normalized == L"broken")
        {
            return GameTypeParseResult<HealthStatus>::success(HealthStatus::Broken);
        }
        if (normalized == L"unknown")
        {
            return GameTypeParseResult<HealthStatus>::success(HealthStatus::Unknown);
        }

        return GameTypeParseResult<HealthStatus>::failure(
            GameTypeErrorCode::UnsupportedValue,
            "Health status is unsupported.");
    }

    [[nodiscard]] inline GameTypeParseResult<HealthSeverity> parseHealthSeverity(std::wstring_view value)
    {
        const std::wstring normalized = toAsciiLower(trimAscii(value));
        if (normalized.empty())
        {
            return GameTypeParseResult<HealthSeverity>::failure(
                GameTypeErrorCode::EmptyValue,
                "Health severity is required.");
        }
        if (normalized == L"blocker")
        {
            return GameTypeParseResult<HealthSeverity>::success(HealthSeverity::Blocker);
        }
        if (normalized == L"warning")
        {
            return GameTypeParseResult<HealthSeverity>::success(HealthSeverity::Warning);
        }
        if (normalized == L"info")
        {
            return GameTypeParseResult<HealthSeverity>::success(HealthSeverity::Info);
        }

        return GameTypeParseResult<HealthSeverity>::failure(
            GameTypeErrorCode::UnsupportedValue,
            "Health severity is unsupported.");
    }
}
