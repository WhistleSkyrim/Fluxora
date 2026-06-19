#pragma once

#include "FluxoraCore/GameSupport/IGameSupport.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fluxora
{
    enum class GameSupportLookupStatus
    {
        Supported,
        Unsupported,
        Ambiguous
    };

    enum class GameSupportLookupMode
    {
        Id,
        DomainHint,
        ExecutableName,
        InstallPath,
        ManualSelection,
        Unsupported
    };

    struct GameSupportLookupResult
    {
        bool supported{false};
        GameSupportLookupStatus status{GameSupportLookupStatus::Unsupported};
        GameSupportLookupMode mode{GameSupportLookupMode::Unsupported};
        DetectionConfidence confidence{DetectionConfidence::None};
        const IGameSupport* support{nullptr};
        const GameDefinition* definition{nullptr};
        std::vector<std::wstring> matchedHints;
        std::wstring message;

        [[nodiscard]] static GameSupportLookupResult found(
            const IGameSupport& supportModule,
            const GameDefinition& definition,
            GameSupportLookupMode mode,
            DetectionConfidence confidence,
            std::vector<std::wstring> matchedHints = {})
        {
            GameSupportLookupResult result;
            result.supported = true;
            result.status = GameSupportLookupStatus::Supported;
            result.mode = mode;
            result.confidence = confidence;
            result.support = &supportModule;
            result.definition = &definition;
            result.matchedHints = std::move(matchedHints);
            return result;
        }

        [[nodiscard]] static GameSupportLookupResult unsupported(std::wstring reason)
        {
            GameSupportLookupResult result;
            result.message = std::move(reason);
            return result;
        }

        [[nodiscard]] static GameSupportLookupResult ambiguous(
            GameSupportLookupMode mode,
            std::wstring reason,
            DetectionConfidence confidence,
            std::vector<std::wstring> matchedHints = {})
        {
            GameSupportLookupResult result;
            result.status = GameSupportLookupStatus::Ambiguous;
            result.mode = mode;
            result.confidence = confidence;
            result.matchedHints = std::move(matchedHints);
            result.message = std::move(reason);
            return result;
        }
    };

    class GameSupportRegistry final
    {
    public:
        GameSupportRegistry() = default;
        explicit GameSupportRegistry(std::vector<GameDefinition> definitions);

        [[nodiscard]] static const GameSupportRegistry& embedded();

        void loadEmbeddedDefinitions();
        void replaceDefinitions(std::vector<GameDefinition> definitions);

        [[nodiscard]] const std::vector<GameDefinition>& definitions() const noexcept;
        [[nodiscard]] std::vector<const IGameSupport*> supportModules() const;
        [[nodiscard]] const IGameSupport* supportFor(const GameDefinition& definition) const noexcept;
        [[nodiscard]] GameSupportLookupResult lookupById(const GameId& id) const;
        [[nodiscard]] GameSupportLookupResult lookupById(std::wstring_view id) const;
        [[nodiscard]] GameSupportLookupResult lookupByDomainHint(std::wstring_view hint) const;
        [[nodiscard]] GameSupportLookupResult lookupByDomain(std::wstring_view domain) const;
        [[nodiscard]] GameSupportLookupResult lookupByExecutableName(const ExecutableName& executableName) const;
        [[nodiscard]] GameSupportLookupResult lookupByExecutableName(std::wstring_view executableName) const;
        [[nodiscard]] GameSupportLookupResult lookupByInstallPath(const std::filesystem::path& installPath) const;
        [[nodiscard]] GameSupportLookupResult lookupByManualSelection(const GameId& id) const;
        [[nodiscard]] GameSupportLookupResult lookupByManualSelection(std::wstring_view id) const;

    private:
        void registerBundledSupportModules();

        std::vector<GameDefinition> definitions_;
        std::vector<std::unique_ptr<IGameSupport>> supportModules_;
    };
}
