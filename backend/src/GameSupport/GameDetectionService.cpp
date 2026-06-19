#include "FluxoraCore/GameSupport/GameDetectionService.hpp"

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <system_error>
#include <utility>

namespace fluxora
{
    namespace
    {
        [[nodiscard]] std::wstring normalizedKey(std::wstring_view value)
        {
            return toAsciiLower(trimAscii(value));
        }

        [[nodiscard]] bool equalsNormalized(std::wstring_view left, std::wstring_view right)
        {
            return normalizedKey(left) == normalizedKey(right);
        }

        [[nodiscard]] bool containsNormalized(std::wstring_view value, std::wstring_view needle)
        {
            const std::wstring normalizedValue = normalizedKey(value);
            const std::wstring normalizedNeedle = normalizedKey(needle);
            return !normalizedNeedle.empty() &&
                normalizedValue.find(normalizedNeedle) != std::wstring::npos;
        }

        [[nodiscard]] bool pathExists(const std::filesystem::path& path)
        {
            std::error_code error;
            return std::filesystem::exists(path, error) && !error;
        }

        [[nodiscard]] bool isRegularFile(const std::filesystem::path& path)
        {
            std::error_code error;
            return std::filesystem::exists(path, error) && std::filesystem::is_regular_file(path, error) && !error;
        }

        [[nodiscard]] bool hasExecutableExtension(const std::filesystem::path& path)
        {
            return equalsNormalized(path.extension().wstring(), L".exe");
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

        [[nodiscard]] std::wstring definitionVersionSignature(const GameSupportRegistry& registry)
        {
            std::wstring signature;
            for (const GameDefinition& definition : registry.definitions())
            {
                signature.append(definition.id.value());
                signature.push_back(L'=');
                signature.append(definition.definitionVersion);
                signature.push_back(L';');
            }

            return signature;
        }

        [[nodiscard]] std::wstring detectionPathCacheKey(
            const GameDetectionRequest& request,
            const GameSupportRegistry& registry)
        {
            if (request.installPath.empty() ||
                !request.storeHints.empty() ||
                !request.executablePaths.empty() ||
                !request.domainHints.empty() ||
                !request.nameHints.empty())
            {
                return {};
            }

            const std::filesystem::path path = canonicalOrAbsolute(request.installPath);
            if (path.empty())
            {
                return {};
            }

            std::wstring key = normalizePathComparisonKey(path, PathCaseSensitivity::CaseInsensitive);
            key.append(L"|manual=");
            key.append(request.manualGameId.has_value() ? request.manualGameId->value() : std::wstring(L"<auto>"));
            key.append(L"|definitions=");
            key.append(definitionVersionSignature(registry));
            return key;
        }

        [[nodiscard]] std::map<std::wstring, GameDetectionResult>& detectionResultCache()
        {
            static std::map<std::wstring, GameDetectionResult> cache;
            return cache;
        }

        [[nodiscard]] std::mutex& detectionResultCacheMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        void clearSupportPointers(GameDetectionCandidate& candidate) noexcept
        {
            candidate.support = nullptr;
            candidate.definition = nullptr;
        }

        void clearSupportPointers(GameDetectionResult& result) noexcept
        {
            result.support = nullptr;
            result.definition = nullptr;
            for (GameDetectionCandidate& candidate : result.ambiguousCandidates)
            {
                clearSupportPointers(candidate);
            }
        }

        void attachSupportPointers(
            GameDetectionCandidate& candidate,
            const GameSupportRegistry& registry)
        {
            if (candidate.gameId.empty())
            {
                return;
            }

            const GameSupportLookupResult lookup = registry.lookupById(candidate.gameId);
            if (lookup.supported)
            {
                candidate.support = lookup.support;
                candidate.definition = lookup.definition;
            }
        }

        [[nodiscard]] GameDetectionResult attachSupportPointers(
            GameDetectionResult result,
            const GameSupportRegistry& registry)
        {
            if (!result.gameId.empty())
            {
                const GameSupportLookupResult lookup = registry.lookupById(result.gameId);
                if (lookup.supported)
                {
                    result.support = lookup.support;
                    result.definition = lookup.definition;
                }
            }
            for (GameDetectionCandidate& candidate : result.ambiguousCandidates)
            {
                attachSupportPointers(candidate, registry);
            }

            return result;
        }

        void addUnique(std::vector<std::wstring>& values, std::wstring value)
        {
            if (value.empty())
            {
                return;
            }

            const auto duplicate = std::find_if(
                values.begin(),
                values.end(),
                [&value](const std::wstring& existing)
                {
                    return equalsNormalized(existing, value);
                });
            if (duplicate == values.end())
            {
                values.push_back(std::move(value));
            }
        }

        [[nodiscard]] std::filesystem::path installPathFromSelection(const std::filesystem::path& path)
        {
            const std::filesystem::path selected = canonicalOrAbsolute(path);
            if (isRegularFile(selected) && hasExecutableExtension(selected))
            {
                return canonicalOrAbsolute(selected.parent_path());
            }

            return selected;
        }

        [[nodiscard]] std::filesystem::path executableFromSelection(const std::filesystem::path& path)
        {
            const std::filesystem::path selected = canonicalOrAbsolute(path);
            if (isRegularFile(selected) && hasExecutableExtension(selected))
            {
                return selected.filename();
            }

            return {};
        }

        [[nodiscard]] const HealthSupportRules* healthRulesFor(const IGameSupport& support)
        {
            const GameSupportComponents& components = support.components();
            if (components.healthProvider == nullptr)
            {
                return nullptr;
            }

            return &components.healthProvider->healthRules();
        }

        [[nodiscard]] const GameDetectionRules* detectionRulesFor(const IGameSupport& support)
        {
            const GameSupportComponents& components = support.components();
            if (components.detectionProvider == nullptr)
            {
                return nullptr;
            }

            return &components.detectionProvider->detectionRules();
        }

        [[nodiscard]] const ExecutableSupportRules* executableRulesFor(const IGameSupport& support)
        {
            const GameSupportComponents& components = support.components();
            if (components.executableRulesProvider == nullptr)
            {
                return nullptr;
            }

            return &components.executableRulesProvider->executableRules();
        }

        [[nodiscard]] bool executableExists(
            const std::filesystem::path& installPath,
            const ExecutableName& executable)
        {
            return pathExists(installPath / std::filesystem::path(executable.displayName()));
        }

        void addRequiredFileEvidence(
            GameDetectionCandidate& candidate,
            const IGameSupport& support,
            const std::filesystem::path& installPath)
        {
            const HealthSupportRules* rules = healthRulesFor(support);
            if (rules == nullptr)
            {
                candidate.warnings.push_back(L"Game support has no health provider.");
                return;
            }

            for (const std::wstring& requiredFile : rules->requiredFiles)
            {
                const std::filesystem::path requiredPath = installPath / std::filesystem::path(requiredFile);
                if (pathExists(requiredPath))
                {
                    addUnique(candidate.matchedFiles, requiredFile);
                }
                else
                {
                    addUnique(candidate.missingFiles, requiredFile);
                }
            }
        }

        [[nodiscard]] bool hasRequiredFileMatch(const GameDetectionCandidate& candidate)
        {
            return !candidate.matchedFiles.empty();
        }

        [[nodiscard]] bool hasAllRequiredFiles(const GameDetectionCandidate& candidate, const IGameSupport& support)
        {
            const HealthSupportRules* rules = healthRulesFor(support);
            return rules != nullptr &&
                !rules->requiredFiles.empty() &&
                candidate.missingFiles.empty() &&
                candidate.matchedFiles.size() == rules->requiredFiles.size();
        }

        [[nodiscard]] bool hasExecutableMatch(
            GameDetectionCandidate& candidate,
            const IGameSupport& support,
            const std::filesystem::path& installPath)
        {
            bool matched = false;
            if (const GameDetectionRules* detection = detectionRulesFor(support); detection != nullptr)
            {
                for (const ExecutableName& executable : detection->executableNames)
                {
                    if (executableExists(installPath, executable))
                    {
                        matched = true;
                        addUnique(candidate.matchedFiles, executable.displayName());
                        if (candidate.selectedExecutable.empty())
                        {
                            candidate.selectedExecutable = executable.displayName();
                        }
                    }
                }
            }

            if (const ExecutableSupportRules* executables = executableRulesFor(support); executables != nullptr)
            {
                for (const GameExecutableDefinition& executable : executables->executables)
                {
                    if (executableExists(installPath, executable.name))
                    {
                        matched = true;
                        addUnique(candidate.matchedFiles, executable.name.displayName());
                        if (candidate.selectedExecutable.empty() ||
                            executable.role == GameExecutableRole::Primary)
                        {
                            candidate.selectedExecutable = executable.name.displayName();
                        }
                    }
                }
            }

            return matched;
        }

        [[nodiscard]] bool hasFolderAliasMatch(
            const IGameSupport& support,
            const std::filesystem::path& installPath)
        {
            const std::wstring folderName = installPath.lexically_normal().filename().wstring();
            if (folderName.empty())
            {
                return false;
            }

            const GameIdentityRules& identity = support.identity();
            for (const std::wstring& alias : identity.installFolderAliases)
            {
                if (equalsNormalized(alias, folderName))
                {
                    return true;
                }
            }

            if (const GameDetectionRules* detection = detectionRulesFor(support); detection != nullptr)
            {
                for (const std::wstring& alias : detection->folderNames)
                {
                    if (equalsNormalized(alias, folderName))
                    {
                        return true;
                    }
                }
            }

            return false;
        }

        [[nodiscard]] GameDetectionCandidate makePathCandidate(
            const IGameSupport& support,
            const GameDefinition& definition,
            const std::filesystem::path& selectedPath,
            DetectionSource source,
            DetectionConfidence baseConfidence)
        {
            const std::filesystem::path installPath = installPathFromSelection(selectedPath);
            GameDetectionCandidate candidate;
            candidate.gameId = support.identity().id;
            candidate.displayName = support.identity().displayName;
            candidate.source = source;
            candidate.confidence = baseConfidence;
            candidate.selectedInstallPath = installPath;
            candidate.canonicalInstallPath = canonicalOrAbsolute(installPath);
            candidate.selectedExecutable = executableFromSelection(selectedPath);
            candidate.support = &support;
            candidate.definition = &definition;

            addRequiredFileEvidence(candidate, support, installPath);

            const bool executableMatched = hasExecutableMatch(candidate, support, installPath);
            const bool folderMatched = hasFolderAliasMatch(support, installPath);
            if (source == DetectionSource::RequiredFiles && hasAllRequiredFiles(candidate, support))
            {
                candidate.confidence = std::max(candidate.confidence, DetectionConfidence::High);
            }
            else if (hasAllRequiredFiles(candidate, support) || executableMatched)
            {
                candidate.confidence = std::max(candidate.confidence, DetectionConfidence::High);
            }
            else if (folderMatched || hasRequiredFileMatch(candidate))
            {
                candidate.confidence = std::max(candidate.confidence, DetectionConfidence::Medium);
            }

            if (candidate.confidence == DetectionConfidence::None)
            {
                candidate.confidence = DetectionConfidence::Low;
            }

            return candidate;
        }

        [[nodiscard]] std::vector<GameDetectionCandidate> pathCandidates(
            const GameSupportRegistry& registry,
            const std::filesystem::path& selectedPath,
            DetectionSource source,
            DetectionConfidence baseConfidence)
        {
            std::vector<GameDetectionCandidate> candidates;
            if (selectedPath.empty())
            {
                return candidates;
            }

            for (const GameDefinition& definition : registry.definitions())
            {
                const IGameSupport* support = registry.supportFor(definition);
                if (support == nullptr)
                {
                    continue;
                }

                GameDetectionCandidate candidate =
                    makePathCandidate(*support, definition, selectedPath, source, baseConfidence);

                bool sourceMatched = false;
                if (source == DetectionSource::RequiredFiles)
                {
                    sourceMatched = hasAllRequiredFiles(candidate, *support);
                }
                else if (source == DetectionSource::ExecutableName)
                {
                    sourceMatched = !candidate.selectedExecutable.empty() ||
                        std::any_of(
                            candidate.matchedFiles.begin(),
                            candidate.matchedFiles.end(),
                            [](const std::wstring& file)
                            {
                                return equalsNormalized(std::filesystem::path(file).extension().wstring(), L".exe");
                            });
                }
                else if (source == DetectionSource::InstallFolderAlias)
                {
                    sourceMatched = hasFolderAliasMatch(*support, candidate.selectedInstallPath);
                }
                else
                {
                    sourceMatched = candidate.confidence >= DetectionConfidence::Medium;
                }

                if (sourceMatched)
                {
                    candidates.push_back(std::move(candidate));
                }
            }

            return candidates;
        }

        [[nodiscard]] GameDetectionResult bestResult(
            std::vector<GameDetectionCandidate> candidates,
            DetectionSource ambiguousSource)
        {
            std::vector<GameDetectionCandidate> mergedCandidates;
            for (GameDetectionCandidate& candidate : candidates)
            {
                auto existing = std::find_if(
                    mergedCandidates.begin(),
                    mergedCandidates.end(),
                    [&candidate](const GameDetectionCandidate& current)
                    {
                        return current.gameId == candidate.gameId;
                    });
                if (existing == mergedCandidates.end())
                {
                    mergedCandidates.push_back(std::move(candidate));
                    continue;
                }

                existing->confidence = std::max(existing->confidence, candidate.confidence);
                if (existing->selectedInstallPath.empty())
                {
                    existing->selectedInstallPath = candidate.selectedInstallPath;
                }
                if (existing->canonicalInstallPath.empty())
                {
                    existing->canonicalInstallPath = candidate.canonicalInstallPath;
                }
                if (existing->selectedExecutable.empty())
                {
                    existing->selectedExecutable = candidate.selectedExecutable;
                }
                for (std::wstring file : candidate.matchedFiles)
                {
                    addUnique(existing->matchedFiles, std::move(file));
                }
                for (std::wstring file : candidate.missingFiles)
                {
                    addUnique(existing->missingFiles, std::move(file));
                }
                for (std::wstring warning : candidate.warnings)
                {
                    addUnique(existing->warnings, std::move(warning));
                }
            }
            candidates = std::move(mergedCandidates);

            if (candidates.empty())
            {
                GameDetectionResult result;
                result.source = DetectionSource::Unsupported;
                result.warnings.push_back(L"Game could not be detected.");
                return result;
            }

            std::sort(
                candidates.begin(),
                candidates.end(),
                [](const GameDetectionCandidate& left, const GameDetectionCandidate& right)
                {
                    return left.confidence > right.confidence;
                });

            const DetectionConfidence bestConfidence = candidates.front().confidence;
            std::vector<GameDetectionCandidate> best;
            for (GameDetectionCandidate& candidate : candidates)
            {
                if (candidate.confidence == bestConfidence)
                {
                    best.push_back(std::move(candidate));
                }
            }

            if (best.size() > 1)
            {
                GameDetectionResult result;
                result.source = DetectionSource::Ambiguous;
                result.confidence = bestConfidence;
                result.ambiguousCandidates = std::move(best);
                result.warnings.push_back(
                    std::wstring(L"Game detection is ambiguous for source ") +
                    GameDetectionService::detectionSourceName(ambiguousSource) +
                    L".");
                return result;
            }

            GameDetectionResult result;
            result.detected = true;
            result.gameId = best.front().gameId;
            result.displayName = best.front().displayName;
            result.source = best.front().source;
            result.confidence = best.front().confidence;
            result.selectedInstallPath = best.front().selectedInstallPath;
            result.canonicalInstallPath = best.front().canonicalInstallPath;
            result.selectedExecutable = best.front().selectedExecutable;
            result.matchedFiles = std::move(best.front().matchedFiles);
            result.missingFiles = std::move(best.front().missingFiles);
            result.warnings = std::move(best.front().warnings);
            result.support = best.front().support;
            result.definition = best.front().definition;
            return result;
        }

        [[nodiscard]] bool identityMatchesHint(const IGameSupport& support, std::wstring_view hint)
        {
            if (hint.empty())
            {
                return false;
            }

            const GameIdentityRules& identity = support.identity();
            if (equalsNormalized(identity.id.value(), hint) ||
                containsNormalized(hint, identity.displayName) ||
                containsNormalized(identity.displayName, hint))
            {
                return true;
            }

            for (const std::wstring& alias : identity.aliases)
            {
                if (containsNormalized(hint, alias) || containsNormalized(alias, hint))
                {
                    return true;
                }
            }

            for (const std::wstring& alias : identity.installFolderAliases)
            {
                if (containsNormalized(hint, alias) || containsNormalized(alias, hint))
                {
                    return true;
                }
            }

            return false;
        }

        [[nodiscard]] GameDetectionResult detectNameHints(
            const GameSupportRegistry& registry,
            const std::vector<std::wstring>& hints)
        {
            std::vector<GameDetectionCandidate> candidates;
            for (const std::wstring& hint : hints)
            {
                for (const GameDefinition& definition : registry.definitions())
                {
                    const IGameSupport* support = registry.supportFor(definition);
                    if (support == nullptr || !identityMatchesHint(*support, hint))
                    {
                        continue;
                    }

                    GameDetectionCandidate candidate;
                    candidate.gameId = support->identity().id;
                    candidate.displayName = support->identity().displayName;
                    candidate.source = DetectionSource::InstallFolderAlias;
                    candidate.confidence = DetectionConfidence::Medium;
                    candidate.warnings.push_back(
                        std::wstring(L"Detected from game name or alias hint: ") + hint);
                    candidate.support = support;
                    candidate.definition = &definition;
                    candidates.push_back(std::move(candidate));
                }
            }

            return bestResult(std::move(candidates), DetectionSource::InstallFolderAlias);
        }
    }

    GameDetectionService::GameDetectionService(const GameSupportRegistry& registry) noexcept
        : registry_(registry)
    {
    }

    GameDetectionResult GameDetectionService::detect(const GameDetectionRequest& request) const
    {
        const std::wstring cacheKey = detectionPathCacheKey(request, registry_);
        if (!cacheKey.empty())
        {
            std::lock_guard<std::mutex> lock(detectionResultCacheMutex());
            const auto cached = detectionResultCache().find(cacheKey);
            if (cached != detectionResultCache().end())
            {
                return attachSupportPointers(cached->second, registry_);
            }
        }

        const auto cacheResult = [this, &cacheKey](GameDetectionResult result)
        {
            if (!cacheKey.empty())
            {
                GameDetectionResult cached = result;
                clearSupportPointers(cached);
                std::lock_guard<std::mutex> lock(detectionResultCacheMutex());
                detectionResultCache()[cacheKey] = std::move(cached);
                while (detectionResultCache().size() > 64)
                {
                    detectionResultCache().erase(detectionResultCache().begin());
                }
            }

            return result;
        };

        if (request.manualGameId.has_value())
        {
            const GameSupportLookupResult lookup = registry_.lookupByManualSelection(request.manualGameId.value());
            if (!lookup.supported || lookup.support == nullptr || lookup.definition == nullptr)
            {
                return cacheResult(unsupported(L"Manual game selection is not supported."));
            }

            return cacheResult(fromCandidate(makePathCandidate(
                *lookup.support,
                *lookup.definition,
                request.installPath,
                DetectionSource::ManualPath,
                DetectionConfidence::Explicit)));
        }

        for (DetectionSource storeSource : {DetectionSource::SteamHint, DetectionSource::GogHint, DetectionSource::EpicHint})
        {
            std::vector<GameDetectionCandidate> candidates;
            for (const GameDetectionStoreHint& hint : request.storeHints)
            {
                if (hint.source != storeSource)
                {
                    continue;
                }

                std::vector<GameDetectionCandidate> fromPath =
                    pathCandidates(registry_, hint.installPath, storeSource, DetectionConfidence::Medium);
                candidates.insert(
                    candidates.end(),
                    std::make_move_iterator(fromPath.begin()),
                    std::make_move_iterator(fromPath.end()));
            }

            GameDetectionResult result = bestResult(std::move(candidates), storeSource);
            if (result.detected || result.source == DetectionSource::Ambiguous)
            {
                return cacheResult(std::move(result));
            }
        }

        std::vector<GameDetectionCandidate> executableCandidates;
        std::vector<std::filesystem::path> executablePaths = request.executablePaths;
        if (!request.installPath.empty())
        {
            executablePaths.push_back(request.installPath);
        }
        for (const std::filesystem::path& executablePath : executablePaths)
        {
            if (executablePath.empty())
            {
                continue;
            }

            const std::filesystem::path normalized = canonicalOrAbsolute(executablePath);
            const std::filesystem::path fileName = normalized.filename();
            if (fileName.empty() || !hasExecutableExtension(fileName))
            {
                continue;
            }

            const GameSupportLookupResult lookup = registry_.lookupByExecutableName(fileName.wstring());
            if (!lookup.supported || lookup.support == nullptr || lookup.definition == nullptr)
            {
                continue;
            }

            GameDetectionCandidate candidate = makePathCandidate(
                *lookup.support,
                *lookup.definition,
                normalized,
                DetectionSource::ExecutableName,
                DetectionConfidence::High);
            candidate.selectedExecutable = fileName;
            addUnique(candidate.matchedFiles, fileName.wstring());
            executableCandidates.push_back(std::move(candidate));
        }
        std::vector<GameDetectionCandidate> installPathExecutableCandidates =
            pathCandidates(
                registry_,
                request.installPath,
                DetectionSource::ExecutableName,
                DetectionConfidence::High);
        executableCandidates.insert(
            executableCandidates.end(),
            std::make_move_iterator(installPathExecutableCandidates.begin()),
            std::make_move_iterator(installPathExecutableCandidates.end()));
        GameDetectionResult byExecutable =
            bestResult(std::move(executableCandidates), DetectionSource::ExecutableName);
        if (byExecutable.detected || byExecutable.source == DetectionSource::Ambiguous)
        {
            return cacheResult(std::move(byExecutable));
        }

        GameDetectionResult byFolder = bestResult(
            pathCandidates(
                registry_,
                request.installPath,
                DetectionSource::InstallFolderAlias,
                DetectionConfidence::Medium),
            DetectionSource::InstallFolderAlias);
        if (byFolder.detected || byFolder.source == DetectionSource::Ambiguous)
        {
            return cacheResult(std::move(byFolder));
        }

        GameDetectionResult byName = detectNameHints(registry_, request.nameHints);
        if (byName.detected || byName.source == DetectionSource::Ambiguous)
        {
            return cacheResult(std::move(byName));
        }

        std::vector<GameDetectionCandidate> domainCandidates;
        for (const std::wstring& hint : request.domainHints)
        {
            const GameSupportLookupResult lookup = registry_.lookupByDomainHint(hint);
            if (!lookup.supported || lookup.support == nullptr || lookup.definition == nullptr)
            {
                continue;
            }

            GameDetectionCandidate candidate;
            candidate.gameId = lookup.support->identity().id;
            candidate.displayName = lookup.support->identity().displayName;
            candidate.source = DetectionSource::NexusDomainHint;
            candidate.confidence = lookup.confidence;
            candidate.support = lookup.support;
            candidate.definition = lookup.definition;
            candidate.warnings.push_back(std::wstring(L"Detected from domain hint: ") + hint);
            domainCandidates.push_back(std::move(candidate));
        }
        GameDetectionResult byDomain =
            bestResult(std::move(domainCandidates), DetectionSource::NexusDomainHint);
        if (byDomain.detected || byDomain.source == DetectionSource::Ambiguous)
        {
            return cacheResult(std::move(byDomain));
        }

        GameDetectionResult byRequiredFiles = bestResult(
            pathCandidates(
                registry_,
                request.installPath,
                DetectionSource::RequiredFiles,
                DetectionConfidence::Low),
            DetectionSource::RequiredFiles);
        if (byRequiredFiles.detected || byRequiredFiles.source == DetectionSource::Ambiguous)
        {
            return cacheResult(std::move(byRequiredFiles));
        }

        return cacheResult(unsupported(L"Game could not be detected from path, store hints, executable names, domains, or required files."));
    }

    std::wstring GameDetectionService::detectionSourceName(DetectionSource source)
    {
        switch (source)
        {
        case DetectionSource::None:
            return L"none";
        case DetectionSource::ManualPath:
            return L"manual-path";
        case DetectionSource::SteamHint:
            return L"steam";
        case DetectionSource::GogHint:
            return L"gog";
        case DetectionSource::EpicHint:
            return L"epic";
        case DetectionSource::ExecutableName:
            return L"executable-name";
        case DetectionSource::InstallFolderAlias:
            return L"install-folder-alias";
        case DetectionSource::NexusDomainHint:
            return L"nexus-domain";
        case DetectionSource::RequiredFiles:
            return L"required-files";
        case DetectionSource::Unsupported:
            return L"unsupported";
        case DetectionSource::Ambiguous:
            return L"ambiguous";
        }

        return L"unknown";
    }

    std::wstring GameDetectionService::detectionConfidenceName(DetectionConfidence confidence)
    {
        switch (confidence)
        {
        case DetectionConfidence::None:
            return L"none";
        case DetectionConfidence::Low:
            return L"low";
        case DetectionConfidence::Medium:
            return L"medium";
        case DetectionConfidence::High:
            return L"high";
        case DetectionConfidence::Explicit:
            return L"explicit";
        }

        return L"unknown";
    }

    GameDetectionResult GameDetectionService::unsupported(std::wstring warning) const
    {
        GameDetectionResult result;
        result.source = DetectionSource::Unsupported;
        result.warnings.push_back(std::move(warning));
        return result;
    }

    GameDetectionResult GameDetectionService::fromCandidate(GameDetectionCandidate candidate) const
    {
        GameDetectionResult result;
        result.detected = true;
        result.gameId = candidate.gameId;
        result.displayName = std::move(candidate.displayName);
        result.source = candidate.source;
        result.confidence = candidate.confidence;
        result.selectedInstallPath = std::move(candidate.selectedInstallPath);
        result.canonicalInstallPath = std::move(candidate.canonicalInstallPath);
        result.selectedExecutable = std::move(candidate.selectedExecutable);
        result.matchedFiles = std::move(candidate.matchedFiles);
        result.missingFiles = std::move(candidate.missingFiles);
        result.warnings = std::move(candidate.warnings);
        result.support = candidate.support;
        result.definition = candidate.definition;
        return result;
    }
}
