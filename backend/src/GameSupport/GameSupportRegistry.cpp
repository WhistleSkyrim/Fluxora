#include "FluxoraCore/GameSupport/GameSupportRegistry.hpp"

#include "FluxoraCore/GameSupport/DefinitionBackedGameSupport.hpp"
#include "FluxoraCore/GameSupport/GameDefinitionLoader.hpp"
#include "FluxoraCore/GameSupport/SkyrimSpecialEditionSupport.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <set>
#include <stdexcept>
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

        [[nodiscard]] bool pathExists(const std::filesystem::path& path)
        {
            std::error_code error;
            const bool exists = std::filesystem::exists(path, error);
            return !error && exists;
        }

        [[nodiscard]] std::wstring firstPathSegment(std::wstring value)
        {
            while (!value.empty() && value.front() == L'/')
            {
                value.erase(value.begin());
            }

            const std::size_t slash = value.find(L'/');
            if (slash != std::wstring::npos)
            {
                value.erase(slash);
            }

            return value;
        }

        [[nodiscard]] std::wstring normalizeDomainHint(std::wstring_view hint)
        {
            std::wstring normalized = normalizedKey(hint);
            std::replace(normalized.begin(), normalized.end(), L'\\', L'/');

            const std::size_t query = normalized.find_first_of(L"?#");
            if (query != std::wstring::npos)
            {
                normalized.erase(query);
            }

            for (std::wstring_view prefix : {L"https://", L"http://"})
            {
                if (normalized.rfind(prefix, 0) == 0)
                {
                    normalized.erase(0, prefix.size());
                    break;
                }
            }

            if (normalized.rfind(L"www.", 0) == 0)
            {
                normalized.erase(0, 4);
            }

            const std::size_t slash = normalized.find(L'/');
            if (slash != std::wstring::npos)
            {
                const std::wstring host = normalized.substr(0, slash);
                std::wstring path = normalized.substr(slash + 1);
                if (host == L"nexusmods.com" || host == L"www.nexusmods.com" || host.ends_with(L".nexusmods.com"))
                {
                    if (path.rfind(L"games/", 0) == 0)
                    {
                        path.erase(0, 6);
                    }

                    return firstPathSegment(std::move(path));
                }
            }

            return normalized;
        }

        [[nodiscard]] const DefinitionBackedGameSupport* asDefinitionBackedSupport(const IGameSupport* support)
        {
            return dynamic_cast<const DefinitionBackedGameSupport*>(support);
        }

        [[nodiscard]] GameSupportLookupResult resultForSingleMatch(
            const IGameSupport& support,
            GameSupportLookupMode mode,
            DetectionConfidence confidence,
            std::vector<std::wstring> matchedHints = {})
        {
            const DefinitionBackedGameSupport* concrete = asDefinitionBackedSupport(&support);
            if (concrete == nullptr)
            {
                return GameSupportLookupResult::unsupported(L"Game support module is not backed by a definition.");
            }

            return GameSupportLookupResult::found(
                support,
                concrete->definition(),
                mode,
                confidence,
                std::move(matchedHints));
        }

        [[nodiscard]] GameSupportLookupResult resultForMatches(
            const std::vector<const IGameSupport*>& matches,
            GameSupportLookupMode mode,
            DetectionConfidence confidence,
            std::wstring unsupportedMessage,
            std::vector<std::wstring> matchedHints = {})
        {
            if (matches.empty())
            {
                return GameSupportLookupResult::unsupported(std::move(unsupportedMessage));
            }
            if (matches.size() > 1)
            {
                return GameSupportLookupResult::ambiguous(
                    mode,
                    L"Game support lookup is ambiguous.",
                    confidence,
                    std::move(matchedHints));
            }

            return resultForSingleMatch(*matches.front(), mode, confidence, std::move(matchedHints));
        }

        [[nodiscard]] std::optional<DetectionConfidence> installPathConfidence(
            const IGameSupport& support,
            const std::filesystem::path& installPath,
            std::vector<std::wstring>& matchedHints)
        {
            const GameSupportComponents& components = support.components();
            if (components.detectionProvider == nullptr || components.executableRulesProvider == nullptr)
            {
                return std::nullopt;
            }

            DetectionConfidence confidence = DetectionConfidence::None;
            const GameDetectionRules& detection = components.detectionProvider->detectionRules();
            const ExecutableSupportRules& executableRules = components.executableRulesProvider->executableRules();
            const std::wstring folderName = installPath.lexically_normal().filename().wstring();
            if (!folderName.empty())
            {
                for (const std::wstring& candidate : detection.folderNames)
                {
                    if (equalsNormalized(candidate, folderName))
                    {
                        confidence = std::max(confidence, DetectionConfidence::Medium);
                        matchedHints.push_back(L"folder:" + candidate);
                        break;
                    }
                }
            }

            bool allRequiredFilesExist = !detection.requiredFiles.empty();
            for (const std::wstring& requiredFile : detection.requiredFiles)
            {
                if (pathExists(installPath / std::filesystem::path(requiredFile)))
                {
                    matchedHints.push_back(L"required:" + requiredFile);
                }
                else
                {
                    allRequiredFilesExist = false;
                }
            }
            if (allRequiredFilesExist)
            {
                confidence = std::max(confidence, DetectionConfidence::High);
            }

            auto executableExists = [&installPath, &matchedHints](const ExecutableName& executable)
            {
                if (pathExists(installPath / executable.displayName()))
                {
                    matchedHints.push_back(L"executable:" + executable.displayName());
                    return true;
                }

                return false;
            };

            bool foundExecutable = false;
            for (const ExecutableName& executable : detection.executableNames)
            {
                foundExecutable = executableExists(executable) || foundExecutable;
            }
            for (const GameExecutableDefinition& executable : executableRules.executables)
            {
                foundExecutable = executableExists(executable.name) || foundExecutable;
            }
            if (foundExecutable)
            {
                confidence = std::max(confidence, DetectionConfidence::High);
            }

            if (confidence == DetectionConfidence::None)
            {
                return std::nullopt;
            }

            return confidence;
        }
    }

    GameSupportRegistry::GameSupportRegistry(std::vector<GameDefinition> definitions)
    {
        replaceDefinitions(std::move(definitions));
    }

    const GameSupportRegistry& GameSupportRegistry::embedded()
    {
        static const GameSupportRegistry registry = []
        {
            GameSupportRegistry loaded;
            loaded.loadEmbeddedDefinitions();
            return loaded;
        }();

        return registry;
    }

    void GameSupportRegistry::loadEmbeddedDefinitions()
    {
        replaceDefinitions(GameDefinitionLoader::loadEmbeddedDefinitions());
    }

    void GameSupportRegistry::replaceDefinitions(std::vector<GameDefinition> definitions)
    {
        definitions_ = std::move(definitions);
        registerBundledSupportModules();
    }

    const std::vector<GameDefinition>& GameSupportRegistry::definitions() const noexcept
    {
        return definitions_;
    }

    std::vector<const IGameSupport*> GameSupportRegistry::supportModules() const
    {
        std::vector<const IGameSupport*> modules;
        modules.reserve(supportModules_.size());
        for (const std::unique_ptr<IGameSupport>& support : supportModules_)
        {
            modules.push_back(support.get());
        }

        return modules;
    }

    const IGameSupport* GameSupportRegistry::supportFor(const GameDefinition& definition) const noexcept
    {
        for (std::size_t index = 0; index < definitions_.size() && index < supportModules_.size(); ++index)
        {
            if (&definitions_[index] == &definition || definitions_[index].id == definition.id)
            {
                return supportModules_[index].get();
            }
        }

        return nullptr;
    }

    GameSupportLookupResult GameSupportRegistry::lookupById(const GameId& id) const
    {
        std::vector<const IGameSupport*> matches;
        for (const std::unique_ptr<IGameSupport>& support : supportModules_)
        {
            if (support->identity().id == id)
            {
                matches.push_back(support.get());
            }
        }

        return resultForMatches(
            matches,
            GameSupportLookupMode::Id,
            DetectionConfidence::Explicit,
            L"Game id is not supported.",
            {id.value()});
    }

    GameSupportLookupResult GameSupportRegistry::lookupById(std::wstring_view id) const
    {
        const GameTypeParseResult<GameId> parsed = GameId::parse(id);
        if (!parsed)
        {
            return GameSupportLookupResult::unsupported(L"Game id is invalid or unsupported.");
        }

        return lookupById(parsed.value());
    }

    GameSupportLookupResult GameSupportRegistry::lookupByDomainHint(std::wstring_view hint) const
    {
        const std::wstring normalized = normalizeDomainHint(hint);
        if (normalized.empty())
        {
            return GameSupportLookupResult::unsupported(L"Game domain is not supported.");
        }

        std::vector<const IGameSupport*> matches;
        for (const std::unique_ptr<IGameSupport>& support : supportModules_)
        {
            const GameSupportComponents& components = support->components();
            if (components.detectionProvider == nullptr)
            {
                continue;
            }

            const GameIdentityRules& identity = support->identity();
            if (std::find(identity.domains.begin(), identity.domains.end(), normalized) != identity.domains.end())
            {
                matches.push_back(support.get());
                continue;
            }

            const GameDetectionRules& detection = components.detectionProvider->detectionRules();
            if (std::find(detection.domains.begin(), detection.domains.end(), normalized) != detection.domains.end())
            {
                matches.push_back(support.get());
            }
        }

        return resultForMatches(
            matches,
            GameSupportLookupMode::DomainHint,
            DetectionConfidence::High,
            L"Game domain is not supported.",
            {normalized});
    }

    GameSupportLookupResult GameSupportRegistry::lookupByDomain(std::wstring_view domain) const
    {
        return lookupByDomainHint(domain);
    }

    GameSupportLookupResult GameSupportRegistry::lookupByExecutableName(const ExecutableName& executableName) const
    {
        std::vector<const IGameSupport*> matches;
        for (const std::unique_ptr<IGameSupport>& support : supportModules_)
        {
            const GameSupportComponents& components = support->components();
            if (components.executableRulesProvider == nullptr || components.detectionProvider == nullptr)
            {
                continue;
            }

            const ExecutableSupportRules& executableRules = components.executableRulesProvider->executableRules();
            const auto executableMatch = std::find_if(
                executableRules.executables.begin(),
                executableRules.executables.end(),
                [&executableName](const GameExecutableDefinition& executable)
                {
                    return executable.name == executableName;
                });
            if (executableMatch != executableRules.executables.end())
            {
                matches.push_back(support.get());
                continue;
            }

            const GameDetectionRules& detection = components.detectionProvider->detectionRules();
            if (std::find(detection.executableNames.begin(), detection.executableNames.end(), executableName) !=
                detection.executableNames.end())
            {
                matches.push_back(support.get());
            }
        }

        return resultForMatches(
            matches,
            GameSupportLookupMode::ExecutableName,
            DetectionConfidence::High,
            L"Executable name is not supported.",
            {executableName.displayName()});
    }

    GameSupportLookupResult GameSupportRegistry::lookupByExecutableName(std::wstring_view executableName) const
    {
        const GameTypeParseResult<ExecutableName> parsed = ExecutableName::parse(executableName);
        if (!parsed)
        {
            return GameSupportLookupResult::unsupported(L"Executable name is invalid or unsupported.");
        }

        return lookupByExecutableName(parsed.value());
    }

    GameSupportLookupResult GameSupportRegistry::lookupByInstallPath(const std::filesystem::path& installPath) const
    {
        if (installPath.empty())
        {
            return GameSupportLookupResult::unsupported(L"Install path is not supported.");
        }

        DetectionConfidence bestConfidence = DetectionConfidence::None;
        std::vector<const IGameSupport*> matches;
        std::vector<std::wstring> bestHints;
        for (const std::unique_ptr<IGameSupport>& support : supportModules_)
        {
            std::vector<std::wstring> hints;
            const std::optional<DetectionConfidence> confidence = installPathConfidence(*support, installPath, hints);
            if (!confidence.has_value())
            {
                continue;
            }

            if (confidence.value() > bestConfidence)
            {
                bestConfidence = confidence.value();
                matches.clear();
                matches.push_back(support.get());
                bestHints = std::move(hints);
            }
            else if (confidence.value() == bestConfidence)
            {
                matches.push_back(support.get());
                bestHints.insert(bestHints.end(), hints.begin(), hints.end());
            }
        }

        return resultForMatches(
            matches,
            GameSupportLookupMode::InstallPath,
            bestConfidence,
            L"Install path is not supported.",
            std::move(bestHints));
    }

    GameSupportLookupResult GameSupportRegistry::lookupByManualSelection(const GameId& id) const
    {
        GameSupportLookupResult result = lookupById(id);
        if (result.supported)
        {
            result.mode = GameSupportLookupMode::ManualSelection;
            result.confidence = DetectionConfidence::Explicit;
            result.matchedHints = {id.value()};
        }

        return result;
    }

    GameSupportLookupResult GameSupportRegistry::lookupByManualSelection(std::wstring_view id) const
    {
        const GameTypeParseResult<GameId> parsed = GameId::parse(id);
        if (!parsed)
        {
            return GameSupportLookupResult::unsupported(L"Manual game selection is invalid or unsupported.");
        }

        return lookupByManualSelection(parsed.value());
    }

    void GameSupportRegistry::registerBundledSupportModules()
    {
        supportModules_.clear();
        supportModules_.reserve(definitions_.size());

        std::set<GameId> ids;
        for (const GameDefinition& definition : definitions_)
        {
            if (!ids.insert(definition.id).second)
            {
                throw std::runtime_error("Duplicate game support module id.");
            }
        }

        for (const GameDefinition& definition : definitions_)
        {
            if (SkyrimSpecialEditionSupport::supportsDefinition(definition))
            {
                supportModules_.push_back(std::make_unique<SkyrimSpecialEditionSupport>(definition));
            }
            else
            {
                supportModules_.push_back(std::make_unique<DefinitionBackedGameSupport>(definition));
            }
        }
    }
}
