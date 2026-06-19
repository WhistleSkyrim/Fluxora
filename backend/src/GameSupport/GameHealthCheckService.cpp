#include "FluxoraCore/GameSupport/GameHealthCheckService.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <mutex>
#include <system_error>
#include <utility>

namespace fluxora
{
    namespace
    {
        [[nodiscard]] bool pathExists(const std::filesystem::path& path)
        {
            std::error_code error;
            return std::filesystem::exists(path, error) && !error;
        }

        [[nodiscard]] bool isDirectory(const std::filesystem::path& path)
        {
            std::error_code error;
            return std::filesystem::exists(path, error) && std::filesystem::is_directory(path, error) && !error;
        }

        [[nodiscard]] bool isRegularFile(const std::filesystem::path& path)
        {
            std::error_code error;
            return std::filesystem::exists(path, error) && std::filesystem::is_regular_file(path, error) && !error;
        }

        [[nodiscard]] std::filesystem::path canonicalOrAbsolute(const std::filesystem::path& path)
        {
            if (path.empty())
            {
                return {};
            }

            std::error_code error;
            std::filesystem::path resolved = std::filesystem::weakly_canonical(path, error);
            if (!error && !resolved.empty())
            {
                return resolved.lexically_normal();
            }

            error.clear();
            resolved = std::filesystem::absolute(path, error);
            return error ? path.lexically_normal() : resolved.lexically_normal();
        }

        [[nodiscard]] const HealthSupportRules* healthRulesFor(const IGameSupport* support);
        [[nodiscard]] const ContentLayoutSupportRules* contentLayoutRulesFor(const IGameSupport* support);
        [[nodiscard]] const ExecutableSupportRules* executableRulesFor(const IGameSupport* support);

        [[nodiscard]] std::wstring filesystemEntryFingerprint(
            const std::filesystem::path& path,
            std::wstring_view label)
        {
            std::error_code error;
            const bool exists = std::filesystem::exists(path, error) && !error;
            if (!exists)
            {
                return std::wstring(label) + L":missing";
            }

            error.clear();
            const auto timestamp = std::filesystem::last_write_time(path, error);
            const auto timestampCount = error ? 0 : timestamp.time_since_epoch().count();
            error.clear();
            const bool regular = std::filesystem::is_regular_file(path, error) && !error;
            std::uintmax_t size = 0;
            if (regular)
            {
                error.clear();
                size = std::filesystem::file_size(path, error);
                if (error)
                {
                    size = 0;
                }
            }

            return std::wstring(label) +
                L":mtime=" + std::to_wstring(timestampCount) +
                L":size=" + std::to_wstring(size) +
                L":kind=" + (regular ? std::wstring(L"file") : std::wstring(L"other"));
        }

        [[nodiscard]] const GameExecutableDefinition* primaryExecutableFor(const IGameSupport* support)
        {
            const ExecutableSupportRules* rules = executableRulesFor(support);
            if (rules == nullptr)
            {
                return nullptr;
            }

            if (rules->roles.primary.has_value())
            {
                const ExecutableName& primary = rules->roles.primary.value();
                const auto match = std::find_if(
                    rules->executables.begin(),
                    rules->executables.end(),
                    [&primary](const GameExecutableDefinition& executable)
                    {
                        return executable.name == primary;
                    });
                if (match != rules->executables.end())
                {
                    return &*match;
                }
            }

            const auto primaryByRole = std::find_if(
                rules->executables.begin(),
                rules->executables.end(),
                [](const GameExecutableDefinition& executable)
                {
                    return executable.role == GameExecutableRole::Primary;
                });
            return primaryByRole == rules->executables.end() ? nullptr : &*primaryByRole;
        }

        [[nodiscard]] std::wstring primaryExecutableFingerprint(
            const IGameSupport* support,
            const std::filesystem::path& installPath)
        {
            const GameExecutableDefinition* executable = primaryExecutableFor(support);
            if (executable == nullptr)
            {
                return L"primary-executable=<none>";
            }

            const std::filesystem::path executablePath =
                installPath / std::filesystem::path(executable->name.displayName());
            std::error_code error;
            const bool exists = std::filesystem::exists(executablePath, error) && !error;
            if (!exists)
            {
                return L"primary-executable=" + executable->name.normalizedName() + L":missing";
            }

            return L"primary-executable=" + executable->name.normalizedName() +
                L":" + filesystemEntryFingerprint(executablePath, L"file");
        }

        [[nodiscard]] std::wstring requiredFilesFingerprint(
            const IGameSupport* support,
            const std::filesystem::path& installPath)
        {
            const HealthSupportRules* rules = healthRulesFor(support);
            if (rules == nullptr)
            {
                return L"required-files=<none>";
            }

            std::wstring fingerprint = L"required-files=";
            for (const std::wstring& requiredFile : rules->requiredFiles)
            {
                const std::filesystem::path relative(requiredFile);
                fingerprint.append(normalizePathComparisonKey(relative, PathCaseSensitivity::CaseInsensitive));
                fingerprint.push_back(L':');
                fingerprint.append(filesystemEntryFingerprint(installPath / relative, L"file"));
                fingerprint.push_back(L';');
            }

            return fingerprint;
        }

        [[nodiscard]] std::wstring dataFolderFingerprint(
            const IGameSupport* support,
            const GameDefinition* definition,
            const std::filesystem::path& installPath)
        {
            std::wstring dataFolder;
            if (const ContentLayoutSupportRules* rules = contentLayoutRulesFor(support); rules != nullptr)
            {
                dataFolder = rules->dataFolder;
            }
            if (dataFolder.empty() && definition != nullptr)
            {
                dataFolder = definition->dataFolder;
            }
            if (dataFolder.empty())
            {
                return L"data-folder=<none>";
            }

            const std::filesystem::path relative(dataFolder);
            return L"data-folder=" +
                normalizePathComparisonKey(relative, PathCaseSensitivity::CaseInsensitive) +
                L":" +
                filesystemEntryFingerprint(installPath / relative, L"directory");
        }

        [[nodiscard]] std::wstring healthCacheKey(const GameHealthCheckRequest& request)
        {
            if (request.support == nullptr || request.definition == nullptr || request.installPath.empty())
            {
                return {};
            }

            const std::filesystem::path installPath = canonicalOrAbsolute(request.installPath);
            if (installPath.empty())
            {
                return {};
            }

            std::wstring key = normalizePathComparisonKey(installPath, PathCaseSensitivity::CaseInsensitive);
            key.append(L"|game=");
            key.append(request.definition->id.value());
            key.append(L"|definitionVersion=");
            key.append(request.definition->definitionVersion);
            key.push_back(L'|');
            key.append(primaryExecutableFingerprint(request.support, installPath));
            key.push_back(L'|');
            key.append(requiredFilesFingerprint(request.support, installPath));
            key.push_back(L'|');
            key.append(dataFolderFingerprint(request.support, request.definition, installPath));
            return key;
        }

        [[nodiscard]] std::map<std::wstring, GameHealthCheckResult>& healthSummaryCache()
        {
            static std::map<std::wstring, GameHealthCheckResult> cache;
            return cache;
        }

        [[nodiscard]] std::mutex& healthSummaryCacheMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        void addFinding(
            GameHealthCheckResult& result,
            HealthSeverity severity,
            std::wstring code,
            std::wstring message,
            std::filesystem::path path = {},
            bool critical = false)
        {
            result.findings.push_back(GameHealthFinding{
                severity,
                std::move(code),
                std::move(message),
                std::move(path),
                critical || severity == HealthSeverity::Blocker
            });
        }

        void addUnique(std::vector<std::wstring>& values, std::wstring value)
        {
            if (value.empty())
            {
                return;
            }

            if (std::find(values.begin(), values.end(), value) == values.end())
            {
                values.push_back(std::move(value));
            }
        }

        [[nodiscard]] const HealthSupportRules* healthRulesFor(const IGameSupport* support)
        {
            if (support == nullptr)
            {
                return nullptr;
            }

            const GameSupportComponents& components = support->components();
            return components.healthProvider == nullptr
                ? nullptr
                : &components.healthProvider->healthRules();
        }

        [[nodiscard]] const ContentLayoutSupportRules* contentLayoutRulesFor(const IGameSupport* support)
        {
            if (support == nullptr)
            {
                return nullptr;
            }

            const GameSupportComponents& components = support->components();
            return components.contentLayoutRulesProvider == nullptr
                ? nullptr
                : &components.contentLayoutRulesProvider->contentLayoutRules();
        }

        [[nodiscard]] const ExecutableSupportRules* executableRulesFor(const IGameSupport* support)
        {
            if (support == nullptr)
            {
                return nullptr;
            }

            const GameSupportComponents& components = support->components();
            return components.executableRulesProvider == nullptr
                ? nullptr
                : &components.executableRulesProvider->executableRules();
        }

        void checkRequiredFiles(
            GameHealthCheckResult& result,
            const IGameSupport* support,
            const std::filesystem::path& installPath)
        {
            const HealthSupportRules* rules = healthRulesFor(support);
            if (rules == nullptr)
            {
                addFinding(
                    result,
                    HealthSeverity::Blocker,
                    L"health-provider-missing",
                    L"Game support does not expose health rules.",
                    {},
                    true);
                return;
            }

            for (const std::wstring& requiredFile : rules->requiredFiles)
            {
                const std::filesystem::path requiredPath = installPath / std::filesystem::path(requiredFile);
                if (pathExists(requiredPath))
                {
                    addUnique(result.matchedFiles, requiredFile);
                    continue;
                }

                addUnique(result.missingFiles, requiredFile);
                addFinding(
                    result,
                    HealthSeverity::Blocker,
                    L"missing-required-file",
                    std::wstring(L"Required game file is missing: ") + requiredFile,
                    requiredPath,
                    true);
            }
        }

        void checkDataFolder(
            GameHealthCheckResult& result,
            const IGameSupport* support,
            const GameDefinition* definition,
            const std::filesystem::path& installPath)
        {
            std::wstring dataFolder;
            if (const ContentLayoutSupportRules* contentLayout = contentLayoutRulesFor(support);
                contentLayout != nullptr)
            {
                dataFolder = contentLayout->dataFolder;
            }
            if (dataFolder.empty() && definition != nullptr)
            {
                dataFolder = definition->dataFolder;
            }
            if (dataFolder.empty())
            {
                addFinding(
                    result,
                    HealthSeverity::Info,
                    L"data-folder-not-defined",
                    L"Game definition does not define a data folder.");
                return;
            }

            const std::filesystem::path dataPath = installPath / std::filesystem::path(dataFolder);
            if (!isDirectory(dataPath))
            {
                addFinding(
                    result,
                    HealthSeverity::Blocker,
                    L"missing-data-folder",
                    std::wstring(L"Game data folder is missing: ") + dataFolder,
                    dataPath,
                    true);
            }
        }

        void checkExecutables(
            GameHealthCheckResult& result,
            const IGameSupport* support,
            const std::filesystem::path& installPath)
        {
            const ExecutableSupportRules* rules = executableRulesFor(support);
            if (rules == nullptr)
            {
                addFinding(
                    result,
                    HealthSeverity::Blocker,
                    L"executable-rules-missing",
                    L"Game support does not expose executable rules.",
                    {},
                    true);
                return;
            }

            for (const GameExecutableDefinition& executable : rules->executables)
            {
                const std::filesystem::path executablePath =
                    installPath / std::filesystem::path(executable.name.displayName());
                if (isRegularFile(executablePath))
                {
                    addUnique(result.matchedFiles, executable.name.displayName());
                    continue;
                }

                if (executable.role == GameExecutableRole::Primary)
                {
                    addUnique(result.missingFiles, executable.name.displayName());
                    addFinding(
                        result,
                        HealthSeverity::Blocker,
                        L"missing-primary-executable",
                        std::wstring(L"Primary game executable is missing: ") + executable.name.displayName(),
                        executablePath,
                        true);
                }
                else
                {
                    result.warnings.push_back(
                        std::wstring(L"Optional executable is missing: ") + executable.name.displayName());
                    addFinding(
                        result,
                        HealthSeverity::Warning,
                        L"missing-optional-executable",
                        std::wstring(L"Optional executable is missing: ") + executable.name.displayName(),
                        executablePath,
                        false);
                }
            }
        }

        void checkDefinitionCompatibility(
            GameHealthCheckResult& result,
            const IGameSupport* support)
        {
            if (support == nullptr)
            {
                return;
            }

            const GameSupportComponents& components = support->components();
            if (components.detectionProvider == nullptr)
            {
                addFinding(
                    result,
                    HealthSeverity::Blocker,
                    L"detection-provider-missing",
                    L"Game support does not expose detection rules.",
                    {},
                    true);
            }
            if (components.identityProvider == nullptr)
            {
                addFinding(
                    result,
                    HealthSeverity::Blocker,
                    L"identity-provider-missing",
                    L"Game support does not expose identity rules.",
                    {},
                    true);
            }
            if (support->capabilities().has(GameCapability::Plugins) &&
                components.pluginRulesProvider == nullptr)
            {
                addFinding(
                    result,
                    HealthSeverity::Blocker,
                    L"plugin-rules-missing",
                    L"Game declares plugin support but no plugin rules provider is available.",
                    {},
                    true);
            }
            if (support->capabilities().has(GameCapability::ContentLayoutRules) &&
                components.contentLayoutRulesProvider == nullptr)
            {
                addFinding(
                    result,
                    HealthSeverity::Blocker,
                    L"content-layout-rules-missing",
                    L"Game declares content layout support but no content layout provider is available.",
                    {},
                    true);
            }
            if (support->capabilities().has(GameCapability::ScriptExtender) &&
                components.launchRulesProvider != nullptr &&
                !components.launchRulesProvider->launchRules().rules.scriptExtender.has_value())
            {
                result.warnings.push_back(L"Script extender capability is enabled without script extender launch rules.");
                addFinding(
                    result,
                    HealthSeverity::Warning,
                    L"script-extender-rules-missing",
                    L"Script extender capability is enabled without script extender launch rules.",
                    {},
                    false);
            }
        }

        [[nodiscard]] HealthStatus summarizeStatus(const GameHealthCheckResult& result)
        {
            const bool hasBlocker = std::any_of(
                result.findings.begin(),
                result.findings.end(),
                [](const GameHealthFinding& finding)
                {
                    return finding.severity == HealthSeverity::Blocker || finding.critical;
                });
            if (hasBlocker)
            {
                if (std::any_of(
                        result.findings.begin(),
                        result.findings.end(),
                        [](const GameHealthFinding& finding)
                        {
                            return finding.code == L"unsupported-game";
                        }))
                {
                    return HealthStatus::Unsupported;
                }
                if (std::any_of(
                        result.findings.begin(),
                        result.findings.end(),
                        [](const GameHealthFinding& finding)
                        {
                            return finding.code == L"unknown-health";
                        }))
                {
                    return HealthStatus::Unknown;
                }

                return HealthStatus::Broken;
            }

            const bool hasWarning = std::any_of(
                result.findings.begin(),
                result.findings.end(),
                [](const GameHealthFinding& finding)
                {
                    return finding.severity == HealthSeverity::Warning;
                });
            return hasWarning ? HealthStatus::Warning : HealthStatus::Healthy;
        }

        [[nodiscard]] std::wstring summarize(const GameHealthCheckResult& result)
        {
            switch (result.status)
            {
            case HealthStatus::Healthy:
                return L"Game install is healthy.";
            case HealthStatus::Warning:
                return L"Game install is usable with warnings.";
            case HealthStatus::Partial:
                return L"Game install is partially supported.";
            case HealthStatus::Unsupported:
                return L"Game is unsupported.";
            case HealthStatus::Broken:
                return L"Game install is broken and cannot be used.";
            case HealthStatus::Unknown:
                return L"Game health is unknown and requires explicit action.";
            }

            return L"Game health is unknown.";
        }
    }

    bool GameHealthCheckResult::hasBlockers() const noexcept
    {
        return std::any_of(
            findings.begin(),
            findings.end(),
            [](const GameHealthFinding& finding)
            {
                return finding.severity == HealthSeverity::Blocker || finding.critical;
            });
    }

    bool GameHealthCheckResult::allowsAutomation() const noexcept
    {
        return !hasBlockers() &&
            status != HealthStatus::Broken &&
            status != HealthStatus::Unsupported &&
            status != HealthStatus::Unknown;
    }

    GameHealthCheckResult GameHealthCheckService::check(const GameHealthCheckRequest& request) const
    {
        const std::wstring cacheKey = healthCacheKey(request);
        if (!cacheKey.empty())
        {
            std::lock_guard<std::mutex> lock(healthSummaryCacheMutex());
            const auto cached = healthSummaryCache().find(cacheKey);
            if (cached != healthSummaryCache().end())
            {
                return cached->second;
            }
        }

        const auto cacheResult = [&cacheKey](GameHealthCheckResult result)
        {
            if (!cacheKey.empty())
            {
                std::lock_guard<std::mutex> lock(healthSummaryCacheMutex());
                healthSummaryCache()[cacheKey] = result;
                while (healthSummaryCache().size() > 128)
                {
                    healthSummaryCache().erase(healthSummaryCache().begin());
                }
            }

            return result;
        };

        GameHealthCheckResult result;
        if (request.support != nullptr)
        {
            result.gameId = request.support->identity().id;
            result.displayName = request.support->identity().displayName;
        }
        else if (request.definition != nullptr)
        {
            result.gameId = request.definition->id;
            result.displayName = request.definition->displayName;
        }

        if (request.support == nullptr || request.definition == nullptr)
        {
            addFinding(
                result,
                HealthSeverity::Blocker,
                L"unsupported-game",
                L"Game support is not available for this install path.",
                {},
                true);
            result.status = HealthStatus::Unsupported;
            result.summary = summarize(result);
            return cacheResult(std::move(result));
        }

        const std::filesystem::path installPath = canonicalOrAbsolute(request.installPath);
        if (installPath.empty())
        {
            addFinding(
                result,
                HealthSeverity::Blocker,
                L"unknown-health",
                L"Game install path is unknown.",
                {},
                true);
            result.status = HealthStatus::Unknown;
            result.summary = summarize(result);
            return cacheResult(std::move(result));
        }

        if (!isDirectory(installPath))
        {
            addFinding(
                result,
                HealthSeverity::Blocker,
                L"install-path-missing",
                L"Game install path does not exist or is not a directory.",
                installPath,
                true);
            result.status = HealthStatus::Broken;
            result.summary = summarize(result);
            return cacheResult(std::move(result));
        }

        checkDefinitionCompatibility(result, request.support);
        checkRequiredFiles(result, request.support, installPath);
        checkDataFolder(result, request.support, request.definition, installPath);
        checkExecutables(result, request.support, installPath);

        if (!result.matchedFiles.empty())
        {
            addFinding(
                result,
                HealthSeverity::Info,
                L"game-version-unavailable",
                L"Game version check is not available for this support module.");
            addFinding(
                result,
                HealthSeverity::Info,
                L"executable-signature-unavailable",
                L"Executable signature check is not available for this support module.");
        }

        result.status = summarizeStatus(result);
        result.summary = summarize(result);
        return cacheResult(std::move(result));
    }

    GameHealthCheckResult GameHealthCheckService::check(const GameDetectionResult& detection) const
    {
        return check(GameHealthCheckRequest{
            detection.support,
            detection.definition,
            detection.canonicalInstallPath.empty() ? detection.selectedInstallPath : detection.canonicalInstallPath
        });
    }

    std::wstring GameHealthCheckService::healthStatusName(HealthStatus status)
    {
        switch (status)
        {
        case HealthStatus::Healthy:
            return L"healthy";
        case HealthStatus::Warning:
            return L"warning";
        case HealthStatus::Partial:
            return L"partial";
        case HealthStatus::Unsupported:
            return L"unsupported";
        case HealthStatus::Broken:
            return L"broken";
        case HealthStatus::Unknown:
            return L"unknown";
        }

        return L"unknown";
    }

    std::wstring GameHealthCheckService::healthSeverityName(HealthSeverity severity)
    {
        switch (severity)
        {
        case HealthSeverity::Blocker:
            return L"blocker";
        case HealthSeverity::Warning:
            return L"warning";
        case HealthSeverity::Info:
            return L"info";
        }

        return L"info";
    }
}
