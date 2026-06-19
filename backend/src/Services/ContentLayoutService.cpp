#include "FluxoraCore/Services/ContentLayoutService.hpp"

#include "FluxoraCore/Services/Logger.hpp"
#include "FluxoraCore/Services/PathSafetyService.hpp"

#include <algorithm>
#include <cwctype>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fluxora
{
    namespace
    {
        std::string toUtf8(const std::wstring& value)
        {
#ifdef _WIN32
            if (value.empty())
            {
                return {};
            }

            const int size = WideCharToMultiByte(
                CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            std::string out(static_cast<std::size_t>(size), '\0');
            WideCharToMultiByte(
                CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), size, nullptr, nullptr);
            return out;
#else
            return std::string(value.begin(), value.end());
#endif
        }

        [[nodiscard]] std::wstring toLower(std::wstring value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character)
            {
                return static_cast<wchar_t>(std::towlower(character));
            });
            return value;
        }

        [[nodiscard]] bool equalsIgnoreCase(std::wstring_view left, std::wstring_view right)
        {
            return toLower(std::wstring(left)) == toLower(std::wstring(right));
        }

        [[nodiscard]] bool isFileEntry(const ContentLayoutArchiveEntry& entry) noexcept
        {
            return !entry.isDirectory;
        }

        [[nodiscard]] bool isCancellationRequested(const std::atomic_bool* cancellationRequested) noexcept
        {
            return cancellationRequested != nullptr &&
                cancellationRequested->load(std::memory_order_relaxed);
        }

        void throwIfCancelled(const ContentLayoutAnalysisRequest& request)
        {
            if (isCancellationRequested(request.cancellationRequested))
            {
                throw std::runtime_error("Content layout analysis was canceled.");
            }
        }

        [[nodiscard]] std::wstring firstSegment(const std::filesystem::path& path)
        {
            const auto it = path.begin();
            return it == path.end() ? std::wstring{} : it->wstring();
        }

        [[nodiscard]] std::filesystem::path stripFirstSegment(const std::filesystem::path& path)
        {
            std::filesystem::path stripped;
            bool first = true;
            for (const std::filesystem::path& part : path)
            {
                if (first)
                {
                    first = false;
                    continue;
                }

                stripped /= part;
            }

            return stripped.lexically_normal();
        }

        [[nodiscard]] std::size_t segmentCount(const std::filesystem::path& path)
        {
            return static_cast<std::size_t>(std::distance(path.begin(), path.end()));
        }

        [[nodiscard]] bool hasLeadingSegment(
            const std::filesystem::path& path,
            std::wstring_view segment)
        {
            const std::wstring first = firstSegment(path);
            return !first.empty() && equalsIgnoreCase(first, segment);
        }

        [[nodiscard]] bool pathStartsWith(
            const std::filesystem::path& path,
            const std::filesystem::path& prefix)
        {
            auto pathIt = path.begin();
            auto prefixIt = prefix.begin();
            for (; prefixIt != prefix.end(); ++prefixIt, ++pathIt)
            {
                if (pathIt == path.end() || !equalsIgnoreCase(pathIt->wstring(), prefixIt->wstring()))
                {
                    return false;
                }
            }

            return true;
        }

        [[nodiscard]] std::filesystem::path stripPrefix(
            const std::filesystem::path& path,
            const std::filesystem::path& prefix)
        {
            std::filesystem::path stripped;
            auto pathIt = path.begin();
            auto prefixIt = prefix.begin();
            for (; prefixIt != prefix.end() && pathIt != path.end(); ++prefixIt, ++pathIt)
            {
            }
            for (; pathIt != path.end(); ++pathIt)
            {
                stripped /= *pathIt;
            }

            return stripped.lexically_normal();
        }

        [[nodiscard]] std::optional<GameRelativePath> tryParseSafeRelativePath(
            const std::filesystem::path& path)
        {
            const PathSafetyService safety;
            const PathSafetyResult validation = safety.validateRelativePath(path);
            if (!validation.safe())
            {
                return std::nullopt;
            }

            const GameTypeParseResult<GameRelativePath> parsed =
                GameRelativePath::parse(validation.normalizedRelativePath);
            if (!parsed)
            {
                return std::nullopt;
            }

            return parsed.value();
        }

        [[nodiscard]] std::wstring normalizedRelativeKey(const GameRelativePath& path)
        {
            return path.comparisonKey();
        }

        [[nodiscard]] std::wstring normalizedTargetKey(
            PlacementTarget target,
            const GameRelativePath& path)
        {
            return std::to_wstring(static_cast<int>(target)) + L"|" + normalizedRelativeKey(path);
        }

        [[nodiscard]] std::wstring layoutCacheKey(const ContentLayoutAnalysisRequest& request)
        {
            if (request.archiveContentHash.empty() || request.gameDefinitionVersion.empty())
            {
                return {};
            }

            std::wstring key = request.selectedGameId.value();
            key.append(L"|definitionVersion=");
            key.append(request.gameDefinitionVersion);
            key.append(L"|archive=");
            key.append(request.archiveContentHash);
            key.append(L"|capabilities=");
            key.append(std::to_wstring(request.selectedGameCapabilities.bits()));
            key.append(L"|installMode=");
            key.append(std::to_wstring(static_cast<int>(request.installMode)));
            key.append(L"|fomod=");
            key.append(request.hasFomodOutput ? L"1" : L"0");
            key.append(L"|subfolder=");
            if (request.userSelectedSubfolder.has_value())
            {
                key.append(normalizePathComparisonKey(
                    request.userSelectedSubfolder.value(),
                    PathCaseSensitivity::CaseInsensitive));
            }

            return key;
        }

        [[nodiscard]] std::map<std::wstring, PlacementPlan>& layoutAnalysisCache()
        {
            static std::map<std::wstring, PlacementPlan> cache;
            return cache;
        }

        [[nodiscard]] std::mutex& layoutAnalysisCacheMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        [[nodiscard]] NormalizedExtension normalizedExtension(const std::filesystem::path& path)
        {
            if (path.extension().empty())
            {
                return {};
            }

            const GameTypeParseResult<NormalizedExtension> parsed =
                NormalizedExtension::parse(path.extension().wstring());
            return parsed ? parsed.value() : NormalizedExtension{};
        }

        [[nodiscard]] bool extensionIn(
            const NormalizedExtension& extension,
            const std::vector<NormalizedExtension>& candidates,
            const std::set<std::wstring>& candidateKeys)
        {
            if (extension.value().empty())
            {
                return false;
            }

            if (!candidateKeys.empty())
            {
                return candidateKeys.contains(extension.value());
            }

            return std::find(candidates.begin(), candidates.end(), extension) != candidates.end();
        }

        [[nodiscard]] bool hasExtension(
            const std::filesystem::path& path,
            std::wstring_view extension)
        {
            return equalsIgnoreCase(path.extension().wstring(), extension);
        }

        [[nodiscard]] bool hasAnyExtension(
            const std::filesystem::path& path,
            const std::vector<std::wstring_view>& extensions)
        {
            return std::any_of(
                extensions.begin(),
                extensions.end(),
                [&path](std::wstring_view extension)
                {
                    return hasExtension(path, extension);
                });
        }

        [[nodiscard]] bool isScriptExtenderLoader(
            const std::filesystem::path& path,
            const ContentLayoutSupportRules& rules)
        {
            const std::wstring fileName = path.filename().wstring();
            const std::wstring key = toAsciiLower(trimAscii(fileName));
            if (!rules.scriptExtenderLoaderKeys.empty())
            {
                return rules.scriptExtenderLoaderKeys.contains(key);
            }

            return std::any_of(
                rules.scriptExtenderLoaders.begin(),
                rules.scriptExtenderLoaders.end(),
                [&fileName](const ExecutableName& candidate)
                {
                    return equalsIgnoreCase(fileName, candidate.displayName());
                });
        }

        [[nodiscard]] bool isDocumentationPath(const std::filesystem::path& path)
        {
            const std::wstring fileName = toLower(path.filename().wstring());
            const std::wstring first = toLower(firstSegment(path));
            return first == L"docs" ||
                first == L"documentation" ||
                fileName.starts_with(L"readme") ||
                fileName.starts_with(L"changelog") ||
                fileName.starts_with(L"license") ||
                fileName == L"copying" ||
                hasAnyExtension(path, {L".txt", L".md", L".rtf", L".pdf"});
        }

        [[nodiscard]] bool isScreenshotPath(const std::filesystem::path& path)
        {
            const std::wstring first = toLower(firstSegment(path));
            return first == L"screenshots" ||
                (segmentCount(path) == 1 && hasAnyExtension(path, {L".png", L".jpg", L".jpeg", L".webp", L".bmp"}));
        }

        [[nodiscard]] bool isConfigPath(const std::filesystem::path& path)
        {
            return hasAnyExtension(path, {L".json", L".xml", L".toml", L".yaml", L".yml", L".cfg", L".conf"});
        }

        [[nodiscard]] bool isSavePath(const std::filesystem::path& path)
        {
            const std::wstring first = toLower(firstSegment(path));
            return first == L"saves" || hasAnyExtension(path, {L".ess", L".skse", L".fos"});
        }

        [[nodiscard]] bool isScriptExtenderDataPath(
            const std::filesystem::path& path,
            const ContentLayoutSupportRules& rules)
        {
            return std::any_of(
                rules.scriptExtenderDataPaths.begin(),
                rules.scriptExtenderDataPaths.end(),
                [&path](const std::filesystem::path& scriptExtenderPath)
                {
                    return pathStartsWith(path, scriptExtenderPath);
                });
        }

        [[nodiscard]] bool isKnownGameDataDirectory(
            const std::filesystem::path& path,
            const ContentLayoutSupportRules& rules)
        {
            const std::wstring first = toLower(firstSegment(path));
            if (!rules.gameDataDirectoryKeys.empty())
            {
                return rules.gameDataDirectoryKeys.contains(first);
            }

            return std::any_of(
                rules.gameDataDirectories.begin(),
                rules.gameDataDirectories.end(),
                [&first](std::wstring_view candidate)
                {
                    return equalsIgnoreCase(first, candidate);
                });
        }

        [[nodiscard]] ContentLayoutClassification classifyDataRelativePath(
            const std::filesystem::path& path,
            const ContentLayoutSupportRules& rules)
        {
            const NormalizedExtension extension = normalizedExtension(path);
            if (extensionIn(extension, rules.pluginExtensions, rules.pluginExtensionKeys))
            {
                return ContentLayoutClassification::Plugin;
            }
            if (extensionIn(extension, rules.archiveExtensions, rules.archiveExtensionKeys))
            {
                return ContentLayoutClassification::Archive;
            }
            if (isScriptExtenderDataPath(path, rules) && hasExtension(path, L".dll"))
            {
                return ContentLayoutClassification::ScriptExtender;
            }
            if (hasExtension(path, L".ini"))
            {
                return ContentLayoutClassification::Ini;
            }
            if (isSavePath(path))
            {
                return ContentLayoutClassification::Save;
            }
            if (isDocumentationPath(path))
            {
                return ContentLayoutClassification::Documentation;
            }
            if (isScreenshotPath(path))
            {
                return ContentLayoutClassification::Screenshots;
            }
            if (isConfigPath(path))
            {
                return ContentLayoutClassification::Config;
            }
            if (hasAnyExtension(path, {L".exe", L".dll", L".com", L".bat", L".cmd", L".ps1"}))
            {
                return ContentLayoutClassification::ToolExecutable;
            }
            if (isKnownGameDataDirectory(path, rules))
            {
                return ContentLayoutClassification::GameData;
            }

            return ContentLayoutClassification::Unknown;
        }

        [[nodiscard]] bool isRecognizedInstallContent(ContentLayoutClassification classification)
        {
            switch (classification)
            {
            case ContentLayoutClassification::GameData:
            case ContentLayoutClassification::GameRoot:
            case ContentLayoutClassification::Plugin:
            case ContentLayoutClassification::Archive:
            case ContentLayoutClassification::ScriptExtender:
            case ContentLayoutClassification::Config:
            case ContentLayoutClassification::Ini:
            case ContentLayoutClassification::Save:
                return true;
            case ContentLayoutClassification::ToolExecutable:
            case ContentLayoutClassification::Documentation:
            case ContentLayoutClassification::Screenshots:
            case ContentLayoutClassification::Unknown:
            case ContentLayoutClassification::Unsafe:
                return false;
            }

            return false;
        }

        [[nodiscard]] bool isWarningClassification(ContentLayoutClassification classification)
        {
            return classification == ContentLayoutClassification::Unknown ||
                classification == ContentLayoutClassification::ToolExecutable;
        }

        [[nodiscard]] std::wstring targetDisplayName(PlacementTarget target)
        {
            switch (target)
            {
            case PlacementTarget::GameRoot:
                return L"game root";
            case PlacementTarget::Data:
                return L"game data";
            case PlacementTarget::Profile:
                return L"profile";
            case PlacementTarget::Overwrite:
                return L"overwrite";
            case PlacementTarget::Blocked:
                return L"blocked";
            }

            return L"unknown";
        }

        [[nodiscard]] std::wstring classificationDisplayName(ContentLayoutClassification classification)
        {
            switch (classification)
            {
            case ContentLayoutClassification::GameData:
                return L"game data";
            case ContentLayoutClassification::GameRoot:
                return L"game root";
            case ContentLayoutClassification::Plugin:
                return L"plugin";
            case ContentLayoutClassification::Archive:
                return L"archive";
            case ContentLayoutClassification::ScriptExtender:
                return L"script extender";
            case ContentLayoutClassification::Config:
                return L"configuration";
            case ContentLayoutClassification::Ini:
                return L"INI";
            case ContentLayoutClassification::Save:
                return L"save";
            case ContentLayoutClassification::ToolExecutable:
                return L"tool executable";
            case ContentLayoutClassification::Documentation:
                return L"documentation";
            case ContentLayoutClassification::Screenshots:
                return L"screenshot";
            case ContentLayoutClassification::Unknown:
                return L"unknown";
            case ContentLayoutClassification::Unsafe:
                return L"unsafe";
            }

            return L"unknown";
        }

        [[nodiscard]] std::string contentLayoutDecision(const PlacementPlan& plan)
        {
            if (!plan.summary.supported)
            {
                return "unsupported";
            }
            if (plan.summary.hasBlockers)
            {
                return "blocked";
            }

            return "place";
        }

        void logLayoutDiagnostics(
            const Logger* logger,
            const PlacementPlan& plan,
            std::wstring_view definitionVersion,
            bool fromCache)
        {
            if (logger == nullptr)
            {
                return;
            }

            logger->writeOperation(
                plan.canInstall() ? LogLevel::Info : LogLevel::Warning,
                "ContentLayout",
                    "contentLayoutDecision=" + contentLayoutDecision(plan) +
                    ", cached=" + std::to_string(fromCache ? 1 : 0) +
                    ", selectedGameId=\"" + toUtf8(plan.gameId.value()) + "\"" +
                    ", definitionVersion=\"" +
                    toUtf8(std::wstring(definitionVersion.begin(), definitionVersion.end())) + "\"" +
                    ", entries=" + std::to_string(plan.summary.totalEntries) +
                    ", planned=" + std::to_string(plan.summary.plannedEntries) +
                    ", gameData=" + std::to_string(plan.summary.gameDataEntries) +
                    ", gameRoot=" + std::to_string(plan.summary.gameRootEntries) +
                    ", plugins=" + std::to_string(plan.summary.pluginEntries) +
                    ", archives=" + std::to_string(plan.summary.archiveEntries) +
                    ", scriptExtender=" + std::to_string(plan.summary.scriptExtenderEntries) +
                    ", unknown=" + std::to_string(plan.summary.unknownEntries) +
                    ", unsafe=" + std::to_string(plan.summary.unsafeEntries) +
                    ", blockers=" + std::to_string(plan.summary.hasBlockers ? 1 : 0) + ".");

            for (const ValidationFinding& finding : plan.validationFindings)
            {
                std::string message =
                    "contentLayoutFinding=" +
                    (finding.blocksInstall ? std::string("blocker") : std::string("non-blocking")) +
                    ", classification=\"" + toUtf8(classificationDisplayName(finding.classification)) + "\"" +
                    ", message=\"" + toUtf8(finding.message) + "\"";
                if (finding.path.has_value())
                {
                    message += ", source=\"" + toUtf8(finding.path->path().generic_wstring()) + "\"";
                }
                logger->writeOperation(
                    finding.blocksInstall ? LogLevel::Warning : LogLevel::Info,
                    "ContentLayout",
                    message + ".");
            }

            constexpr std::size_t maxLoggedEntries = 200;
            const std::size_t loggedEntries = (std::min)(plan.entries.size(), maxLoggedEntries);
            for (std::size_t index = 0; index < loggedEntries; ++index)
            {
                const PlacementPlanEntry& entry = plan.entries[index];
                logger->writeOperation(
                    entry.target == PlacementTarget::Blocked ? LogLevel::Warning : LogLevel::Info,
                    "ContentLayout",
                    "contentLayoutEntry index=" + std::to_string(index) +
                        ", source=\"" + toUtf8(entry.sourcePath.path().generic_wstring()) + "\"" +
                        ", target=\"" + toUtf8(targetDisplayName(entry.target)) + "\"" +
                        ", targetPath=\"" + toUtf8(entry.targetRelativePath.path().generic_wstring()) + "\"" +
                        ", classification=\"" + toUtf8(classificationDisplayName(entry.classification)) + "\"" +
                        ", reason=\"" + toUtf8(entry.explanation) + "\".");
            }
            if (plan.entries.size() > maxLoggedEntries)
            {
                logger->writeOperation(
                    LogLevel::Warning,
                    "ContentLayout",
                    "contentLayoutEntryLogTruncated entries=" + std::to_string(plan.entries.size()) +
                        ", logged=" + std::to_string(loggedEntries) + ".");
            }
        }

        void addFinding(
            PlacementPlan& plan,
            HealthSeverity severity,
            std::optional<GameRelativePath> path,
            ContentLayoutClassification classification,
            std::wstring message,
            bool blocksInstall)
        {
            plan.validationFindings.push_back(ValidationFinding{
                severity,
                std::move(path),
                classification,
                std::move(message),
                blocksInstall
            });

            if (blocksInstall)
            {
                plan.summary.hasBlockers = true;
            }
            if (severity == HealthSeverity::Warning)
            {
                plan.summary.hasWarnings = true;
            }
        }

        void countEntry(ContentLayoutSummary& summary, const PlacementPlanEntry& entry)
        {
            if (entry.target != PlacementTarget::Blocked)
            {
                ++summary.plannedEntries;
            }

            switch (entry.classification)
            {
            case ContentLayoutClassification::GameData:
                ++summary.gameDataEntries;
                break;
            case ContentLayoutClassification::GameRoot:
                ++summary.gameRootEntries;
                break;
            case ContentLayoutClassification::Plugin:
                ++summary.pluginEntries;
                break;
            case ContentLayoutClassification::Archive:
                ++summary.archiveEntries;
                break;
            case ContentLayoutClassification::ScriptExtender:
                ++summary.scriptExtenderEntries;
                break;
            case ContentLayoutClassification::Unknown:
                ++summary.unknownEntries;
                break;
            case ContentLayoutClassification::Unsafe:
                ++summary.unsafeEntries;
                break;
            case ContentLayoutClassification::Config:
            case ContentLayoutClassification::Ini:
            case ContentLayoutClassification::Save:
            case ContentLayoutClassification::ToolExecutable:
            case ContentLayoutClassification::Documentation:
            case ContentLayoutClassification::Screenshots:
                break;
            }
        }

        [[nodiscard]] std::vector<PlacementTarget> safeOverrideTargets(
            ContentLayoutClassification classification,
            const CapabilitySet& capabilities,
            PlacementTarget currentTarget)
        {
            std::vector<PlacementTarget> targets;
            const auto add = [&targets](PlacementTarget target)
            {
                if (std::find(targets.begin(), targets.end(), target) == targets.end())
                {
                    targets.push_back(target);
                }
            };

            if (currentTarget != PlacementTarget::Blocked)
            {
                add(currentTarget);
            }

            if (classification == ContentLayoutClassification::Unknown ||
                classification == ContentLayoutClassification::Documentation ||
                classification == ContentLayoutClassification::Screenshots ||
                classification == ContentLayoutClassification::Config ||
                classification == ContentLayoutClassification::Ini)
            {
                add(PlacementTarget::Data);
            }

            if ((classification == ContentLayoutClassification::GameRoot ||
                 classification == ContentLayoutClassification::ScriptExtender) &&
                capabilities.has(GameCapability::RootFiles))
            {
                add(PlacementTarget::GameRoot);
            }

            return targets;
        }

        [[nodiscard]] std::wstring rootFileWrapperDirectory(const ContentLayoutSupportRules& rules)
        {
            return rules.rootFileWrapperDirectory.empty() ? std::wstring{} : rules.rootFileWrapperDirectory;
        }

        [[nodiscard]] std::filesystem::path placementPath(const PlacementPlanEntry& entry, const PlacementPlan& plan)
        {
            switch (entry.target)
            {
            case PlacementTarget::Data:
                return entry.targetRelativePath.path();
            case PlacementTarget::GameRoot:
                if (plan.rootFileWrapperDirectory.empty())
                {
                    throw std::runtime_error("Content layout root wrapper directory is not defined.");
                }
                return std::filesystem::path(plan.rootFileWrapperDirectory) / entry.targetRelativePath.path();
            case PlacementTarget::Profile:
                return std::filesystem::path(L"profile") / entry.targetRelativePath.path();
            case PlacementTarget::Overwrite:
                return std::filesystem::path(L"overwrite") / entry.targetRelativePath.path();
            case PlacementTarget::Blocked:
                break;
            }

            throw std::runtime_error("Blocked content cannot be materialized.");
        }

        [[nodiscard]] std::filesystem::path uniqueSiblingPath(
            const std::filesystem::path& directory,
            std::wstring_view suffix)
        {
            const std::filesystem::path parent = directory.parent_path();
            const std::wstring stem = directory.filename().wstring();
            std::filesystem::path candidate = parent / std::filesystem::path(stem + std::wstring(suffix));
            for (int index = 2; std::filesystem::exists(candidate); ++index)
            {
                candidate = parent / std::filesystem::path(
                    stem + std::wstring(suffix) + L"-" + std::to_wstring(index));
            }

            return candidate;
        }

        [[nodiscard]] bool isRegularFile(const std::filesystem::path& path)
        {
            std::error_code error;
            return std::filesystem::exists(path, error) && std::filesystem::is_regular_file(path, error);
        }

        void copyPlannedFiles(
            const std::filesystem::path& sourceRoot,
            const std::filesystem::path& destinationRoot,
            const PlacementPlan& plan)
        {
            const PathSafetyService safety;
            for (const PlacementPlanEntry& entry : plan.entries)
            {
                if (entry.target == PlacementTarget::Blocked)
                {
                    throw std::runtime_error("Content layout contains blocked entries.");
                }

                const std::filesystem::path source = sourceRoot / entry.sourcePath.path();
                safety.validateContainedPath(sourceRoot, source).throwIfUnsafe("Content layout source path is unsafe");
                if (!isRegularFile(source))
                {
                    throw std::runtime_error("Content layout source file is missing.");
                }

                const std::filesystem::path destination = destinationRoot / placementPath(entry, plan);
                std::error_code sizeError;
                const std::uintmax_t bytes = std::filesystem::file_size(source, sizeError);
                safety.validateWritePath(
                    destinationRoot,
                    destination,
                    PathSafetyWriteOptions{sizeError ? 0 : bytes, false})
                    .throwIfUnsafe("Content layout destination path is unsafe");
                std::filesystem::create_directories(destination.parent_path());
                std::filesystem::copy_file(
                    source,
                    destination,
                    std::filesystem::copy_options::overwrite_existing);
            }
        }
    }

    PlacementPlan ContentLayoutService::analyze(const ContentLayoutAnalysisRequest& request) const
    {
        throwIfCancelled(request);
        const std::wstring cacheKey = layoutCacheKey(request);
        if (!cacheKey.empty())
        {
            std::lock_guard<std::mutex> lock(layoutAnalysisCacheMutex());
            const auto cached = layoutAnalysisCache().find(cacheKey);
            if (cached != layoutAnalysisCache().end())
            {
                logLayoutDiagnostics(request.logger, cached->second, request.gameDefinitionVersion, true);
                return cached->second;
            }
        }

        const auto cachePlan = [&request, &cacheKey](PlacementPlan plan)
        {
            logLayoutDiagnostics(request.logger, plan, request.gameDefinitionVersion, false);
            if (!cacheKey.empty())
            {
                std::lock_guard<std::mutex> lock(layoutAnalysisCacheMutex());
                layoutAnalysisCache()[cacheKey] = plan;
                while (layoutAnalysisCache().size() > 64)
                {
                    layoutAnalysisCache().erase(layoutAnalysisCache().begin());
                }
            }

            return plan;
        };

        PlacementPlan plan;
        plan.gameId = request.selectedGameId;
        plan.gameDisplayName = request.selectedGameDisplayName;

        if (!request.selectedGameCapabilities.has(GameCapability::ContentLayoutRules) ||
            request.rulesProvider == nullptr)
        {
            addFinding(
                plan,
                HealthSeverity::Blocker,
                std::nullopt,
                ContentLayoutClassification::Unsafe,
                L"The selected game does not provide content layout rules.",
                true);
            plan.userExplanation.summary = L"Fluxora cannot place this archive because the selected game has no layout rules.";
            return cachePlan(std::move(plan));
        }

        plan.summary.supported = true;
        const ContentLayoutSupportRules& rules = request.rulesProvider->contentLayoutRules();
        plan.rootFileWrapperDirectory = rootFileWrapperDirectory(rules);
        if (rules.dataFolder.empty())
        {
            addFinding(
                plan,
                HealthSeverity::Blocker,
                std::nullopt,
                ContentLayoutClassification::Unsafe,
                L"The selected game layout rules do not define a data folder.",
                true);
            plan.userExplanation.summary = L"Fluxora cannot place this archive because the game definition is incomplete.";
            return cachePlan(std::move(plan));
        }

        std::optional<GameRelativePath> selectedSubfolder;
        if (request.userSelectedSubfolder.has_value())
        {
            selectedSubfolder = tryParseSafeRelativePath(request.userSelectedSubfolder.value());
            if (!selectedSubfolder.has_value())
            {
                addFinding(
                    plan,
                    HealthSeverity::Blocker,
                    std::nullopt,
                    ContentLayoutClassification::Unsafe,
                    L"The selected archive subfolder is not a safe relative path.",
                    true);
                plan.userExplanation.summary = L"Fluxora blocked the selected archive subfolder because it is unsafe.";
                return cachePlan(std::move(plan));
            }
        }

        struct SafeEntry
        {
            GameRelativePath sourcePath;
            std::filesystem::path analysisPath;
        };

        std::vector<SafeEntry> files;
        files.reserve(request.archiveFileTree.size());
        bool skippedForSubfolder = false;
        for (const ContentLayoutArchiveEntry& entry : request.archiveFileTree)
        {
            throwIfCancelled(request);
            if (!isFileEntry(entry))
            {
                continue;
            }

            std::optional<GameRelativePath> sourcePath = tryParseSafeRelativePath(entry.relativePath);
            if (!sourcePath.has_value())
            {
                ++plan.summary.unsafeEntries;
                addFinding(
                    plan,
                    HealthSeverity::Blocker,
                    std::nullopt,
                    ContentLayoutClassification::Unsafe,
                    L"Archive entry is not a safe relative path.",
                    true);
                continue;
            }

            std::filesystem::path analysisPath = sourcePath->path();
            if (selectedSubfolder.has_value())
            {
                if (!pathStartsWith(analysisPath, selectedSubfolder->path()))
                {
                    skippedForSubfolder = true;
                    continue;
                }

                analysisPath = stripPrefix(analysisPath, selectedSubfolder->path());
                if (analysisPath.empty())
                {
                    continue;
                }
            }

            files.push_back(SafeEntry{sourcePath.value(), analysisPath});
        }

        if (files.empty())
        {
            addFinding(
                plan,
                HealthSeverity::Blocker,
                std::nullopt,
                ContentLayoutClassification::Unknown,
                skippedForSubfolder
                    ? L"The selected archive subfolder does not contain installable files."
                    : L"The archive does not contain installable files.",
                true);
            plan.userExplanation.summary = L"Fluxora did not find files it can place.";
            return cachePlan(std::move(plan));
        }

        const bool hasDataWrapper = std::any_of(
            files.begin(),
            files.end(),
            [&rules](const SafeEntry& entry)
            {
                return hasLeadingSegment(entry.analysisPath, rules.dataFolder) &&
                    segmentCount(entry.analysisPath) > 1;
            });

        std::size_t recognizedContent = 0;
        for (const SafeEntry& file : files)
        {
            throwIfCancelled(request);
            ++plan.summary.totalEntries;
            std::filesystem::path sourceForClassification = file.analysisPath;
            PlacementTarget target = PlacementTarget::Data;
            ContentArea area = ContentArea::Data;
            std::wstring reason;

            if (!plan.rootFileWrapperDirectory.empty() &&
                hasLeadingSegment(sourceForClassification, plan.rootFileWrapperDirectory) &&
                segmentCount(sourceForClassification) > 1)
            {
                if (!rules.supportsRootFiles || !request.selectedGameCapabilities.has(GameCapability::RootFiles))
                {
                    addFinding(
                        plan,
                        HealthSeverity::Blocker,
                        std::optional<GameRelativePath>{file.sourcePath},
                        ContentLayoutClassification::GameRoot,
                        L"Archive contains game-root files, but the selected game does not allow root placement.",
                        true);
                    continue;
                }
                if (plan.rootFileWrapperDirectory.empty())
                {
                    addFinding(
                        plan,
                        HealthSeverity::Blocker,
                        std::optional<GameRelativePath>{file.sourcePath},
                        ContentLayoutClassification::GameRoot,
                        L"Archive contains game-root files, but the selected game does not define a root file wrapper.",
                        true);
                    continue;
                }

                sourceForClassification = stripFirstSegment(sourceForClassification);
                target = PlacementTarget::GameRoot;
                area = ContentArea::GameRoot;
            }
            else if (isScriptExtenderLoader(sourceForClassification, rules) &&
                segmentCount(sourceForClassification) == 1)
            {
                if (!rules.supportsRootFiles || !request.selectedGameCapabilities.has(GameCapability::RootFiles))
                {
                    addFinding(
                        plan,
                        HealthSeverity::Blocker,
                        std::optional<GameRelativePath>{file.sourcePath},
                        ContentLayoutClassification::ScriptExtender,
                        L"Archive contains a script extender loader, but the selected game does not allow root placement.",
                        true);
                    continue;
                }
                if (plan.rootFileWrapperDirectory.empty())
                {
                    addFinding(
                        plan,
                        HealthSeverity::Blocker,
                        std::optional<GameRelativePath>{file.sourcePath},
                        ContentLayoutClassification::ScriptExtender,
                        L"Archive contains a script extender loader, but the selected game does not define a root file wrapper.",
                        true);
                    continue;
                }

                target = PlacementTarget::GameRoot;
                area = ContentArea::GameRoot;
            }
            else if (hasDataWrapper &&
                hasLeadingSegment(sourceForClassification, rules.dataFolder) &&
                segmentCount(sourceForClassification) > 1)
            {
                sourceForClassification = stripFirstSegment(sourceForClassification);
                reason = L"Fluxora stripped the archive's Data folder because the selected game already mounts mod roots as data.";
            }

            std::optional<GameRelativePath> targetPath = tryParseSafeRelativePath(sourceForClassification);
            if (!targetPath.has_value())
            {
                ++plan.summary.unsafeEntries;
                addFinding(
                    plan,
                    HealthSeverity::Blocker,
                    std::optional<GameRelativePath>{file.sourcePath},
                    ContentLayoutClassification::Unsafe,
                    L"Archive entry would produce an unsafe target path.",
                    true);
                continue;
            }

            ContentLayoutClassification classification =
                target == PlacementTarget::GameRoot
                    ? ContentLayoutClassification::GameRoot
                    : classifyDataRelativePath(sourceForClassification, rules);
            if (target == PlacementTarget::GameRoot && isScriptExtenderLoader(sourceForClassification, rules))
            {
                classification = ContentLayoutClassification::ScriptExtender;
            }
            else if (target == PlacementTarget::GameRoot &&
                hasAnyExtension(sourceForClassification, {L".exe", L".dll", L".com", L".bat", L".cmd", L".ps1"}))
            {
                classification = ContentLayoutClassification::ToolExecutable;
            }

            if (target == PlacementTarget::Data &&
                classification == ContentLayoutClassification::ToolExecutable &&
                !isScriptExtenderDataPath(sourceForClassification, rules))
            {
                target = PlacementTarget::Blocked;
                addFinding(
                    plan,
                    HealthSeverity::Blocker,
                    std::optional<GameRelativePath>{file.sourcePath},
                    classification,
                    L"Archive contains an unexpected executable or DLL outside a known script-extender data folder.",
                    true);
            }
            else if (isWarningClassification(classification))
            {
                addFinding(
                    plan,
                    HealthSeverity::Warning,
                    std::optional<GameRelativePath>{file.sourcePath},
                    classification,
                    L"Archive contains content Fluxora cannot confidently classify.",
                    false);
            }

            if (classification == ContentLayoutClassification::ScriptExtender &&
                isScriptExtenderDataPath(sourceForClassification, rules))
            {
                reason = L"Script extender plugin content belongs under the game's data layout.";
            }
            else if (classification == ContentLayoutClassification::Plugin)
            {
                reason = L"Plugin extension matches the selected game's plugin rules.";
            }
            else if (classification == ContentLayoutClassification::Archive)
            {
                reason = L"Archive extension matches the selected game's archive rules.";
            }
            else if (classification == ContentLayoutClassification::ScriptExtender)
            {
                reason = L"Script extender loader matches the selected game's executable rules.";
            }
            else if (reason.empty())
            {
                reason = L"Fluxora placed this entry according to the selected game's content layout rules.";
            }

            PlacementPlanEntry entry{
                file.sourcePath,
                target,
                area,
                targetPath.value(),
                classification,
                reason,
                false,
                {}
            };
            entry.safeManualTargets = safeOverrideTargets(
                classification,
                request.selectedGameCapabilities,
                target);
            entry.manualOverrideAllowed = !entry.safeManualTargets.empty() &&
                classification != ContentLayoutClassification::ToolExecutable;
            countEntry(plan.summary, entry);
            if (isRecognizedInstallContent(classification))
            {
                ++recognizedContent;
            }

            if (entry.manualOverrideAllowed &&
                entry.safeManualTargets.size() > 1)
            {
                plan.manualOverrideOptions.push_back(ManualOverrideOption{
                    entry.sourcePath,
                    entry.safeManualTargets,
                    L"Only targets allowed by the selected game's layout rules are available."
                });
            }

            plan.entries.push_back(std::move(entry));
        }

        std::map<std::wstring, GameRelativePath> targetPaths;
        for (const PlacementPlanEntry& entry : plan.entries)
        {
            throwIfCancelled(request);
            if (entry.target == PlacementTarget::Blocked)
            {
                continue;
            }

            const std::wstring key = normalizedTargetKey(entry.target, entry.targetRelativePath);
            if (targetPaths.contains(key))
            {
                addFinding(
                    plan,
                    HealthSeverity::Blocker,
                    std::optional<GameRelativePath>{entry.sourcePath},
                    entry.classification,
                    L"Multiple archive entries resolve to the same placement target.",
                    true);
            }
            else
            {
                targetPaths.emplace(key, entry.targetRelativePath);
            }
        }

        if (recognizedContent == 0 && !plan.summary.hasBlockers)
        {
            addFinding(
                plan,
                HealthSeverity::Blocker,
                std::nullopt,
                ContentLayoutClassification::Unknown,
                L"Fluxora could not recognize any installable game content in this archive.",
                true);
        }

        plan.userExplanation.summary =
            plan.summary.hasBlockers
                ? L"Fluxora found content layout blockers that must be resolved before install."
                : std::wstring(L"Fluxora built a content placement plan for ") +
                    (plan.gameDisplayName.empty() ? plan.gameId.value() : plan.gameDisplayName) + L".";
        plan.userExplanation.details.reserve(plan.entries.size());
        for (const PlacementPlanEntry& entry : plan.entries)
        {
            throwIfCancelled(request);
            plan.userExplanation.details.push_back(
                entry.sourcePath.path().generic_wstring() + L" -> " +
                targetDisplayName(entry.target) + L": " +
                classificationDisplayName(entry.classification) + L". " +
                entry.explanation);
        }

        (void)request.installMode;
        (void)request.hasFomodOutput;
        return cachePlan(std::move(plan));
    }

    PlacementPlan ContentLayoutService::analyzeDirectory(
        const std::filesystem::path& directory,
        const ContentLayoutAnalysisRequest& request) const
    {
        throwIfCancelled(request);
        if (directory.empty() || !std::filesystem::exists(directory) || !std::filesystem::is_directory(directory))
        {
            throw std::invalid_argument("Content layout directory is required.");
        }

        ContentLayoutAnalysisRequest directoryRequest = request;
        directoryRequest.archiveFileTree.clear();

        const PathSafetyService safety;
        std::error_code error;
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::recursive_directory_iterator(
                 directory,
                 std::filesystem::directory_options::skip_permission_denied,
                 error))
        {
            throwIfCancelled(directoryRequest);
            if (error)
            {
                break;
            }

            if (!entry.is_regular_file(error) && !entry.is_directory(error))
            {
                continue;
            }

            safety.validateContainedPath(directory, entry.path())
                .throwIfUnsafe("Extracted content path is unsafe");

            directoryRequest.archiveFileTree.push_back(ContentLayoutArchiveEntry{
                std::filesystem::relative(entry.path(), directory),
                entry.is_directory(error)
            });
        }

        std::sort(
            directoryRequest.archiveFileTree.begin(),
            directoryRequest.archiveFileTree.end(),
            [](const ContentLayoutArchiveEntry& left, const ContentLayoutArchiveEntry& right)
            {
                return normalizePathComparisonKey(left.relativePath, PathCaseSensitivity::CaseInsensitive) <
                    normalizePathComparisonKey(right.relativePath, PathCaseSensitivity::CaseInsensitive);
            });

        return analyze(directoryRequest);
    }

    void ContentLayoutService::applyPlanToDirectory(
        const std::filesystem::path& directory,
        const PlacementPlan& plan) const
    {
        if (directory.empty() || !std::filesystem::exists(directory) || !std::filesystem::is_directory(directory))
        {
            throw std::invalid_argument("Content layout directory is required.");
        }
        if (!plan.canInstall())
        {
            throw std::runtime_error("Content layout contains blockers.");
        }

        const std::filesystem::path normalizedDirectory =
            std::filesystem::absolute(directory).lexically_normal();
        PathSafetyService().validateDirectoryWriteRoot(normalizedDirectory)
            .throwIfUnsafe("Content layout directory is unsafe");
        const std::filesystem::path temporaryDirectory =
            uniqueSiblingPath(normalizedDirectory, L".layout");
        std::filesystem::create_directories(temporaryDirectory);

        try
        {
            copyPlannedFiles(normalizedDirectory, temporaryDirectory, plan);
            std::filesystem::remove_all(normalizedDirectory);
            std::filesystem::rename(temporaryDirectory, normalizedDirectory);
        }
        catch (const std::exception&)
        {
            std::error_code cleanupError;
            std::filesystem::remove_all(temporaryDirectory, cleanupError);
            throw;
        }
    }
}
