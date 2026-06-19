#include "FluxoraCore/Services/TemplateService.hpp"

#include "FluxoraCore/Services/Logger.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace fluxora
{
    namespace
    {
        // Append items from `source` that are not already present in `target`.
        // The base template provides the common entries; a game template only
        // contributes what is genuinely new, so overlaying never duplicates.
        void mergeStrings(std::vector<std::wstring>& target, const std::vector<std::wstring>& source)
        {
            for (const auto& item : source)
            {
                if (std::find(target.begin(), target.end(), item) == target.end())
                {
                    target.push_back(item);
                }
            }
        }

        // Capabilities merge by id: a game capability with an id already present
        // in the base refines (replaces) the base entry instead of duplicating it.
        void mergeCapabilities(std::vector<TemplateCapability>& target, const std::vector<TemplateCapability>& source)
        {
            for (const auto& capability : source)
            {
                const auto existing = std::find_if(
                    target.begin(),
                    target.end(),
                    [&capability](const TemplateCapability& candidate) { return candidate.id == capability.id; });

                if (existing == target.end())
                {
                    target.push_back(capability);
                }
                else
                {
                    *existing = capability;
                }
            }
        }

        [[nodiscard]] std::vector<std::wstring> extensionValues(const std::vector<NormalizedExtension>& extensions)
        {
            std::vector<std::wstring> values;
            values.reserve(extensions.size());
            for (const NormalizedExtension& extension : extensions)
            {
                values.push_back(extension.value());
            }

            return values;
        }

        [[nodiscard]] std::vector<std::wstring> executableNames(const std::vector<GameExecutableDefinition>& executables)
        {
            std::vector<std::wstring> values;
            values.reserve(executables.size());
            for (const GameExecutableDefinition& executable : executables)
            {
                values.push_back(executable.name.displayName());
            }

            return values;
        }

        void addCapability(
            std::vector<TemplateCapability>& capabilities,
            const CapabilitySet& set,
            GameCapability capability,
            std::wstring id,
            std::wstring displayName,
            std::wstring description)
        {
            if (!set.has(capability))
            {
                return;
            }

            capabilities.push_back(TemplateCapability{
                std::move(id),
                std::move(displayName),
                std::move(description)
            });
        }

        [[nodiscard]] std::wstring scriptExtenderDisplayName(const ExecutableSupportRules* executableRules)
        {
            if (executableRules == nullptr)
            {
                return L"Script Extender";
            }

            const auto match = std::find_if(
                executableRules->executables.begin(),
                executableRules->executables.end(),
                [](const GameExecutableDefinition& executable)
                {
                    return executable.role == GameExecutableRole::ScriptExtender;
                });

            if (match == executableRules->executables.end() || match->displayName.empty())
            {
                return L"Script Extender";
            }

            return match->displayName;
        }

        [[nodiscard]] std::wstring scriptExtenderDescription(const LaunchSupportRules* launchRules)
        {
            if (launchRules != nullptr &&
                launchRules->rules.scriptExtender.has_value() &&
                !launchRules->rules.scriptExtender->name.empty())
            {
                return L"Запуск через " + launchRules->rules.scriptExtender->name + L".";
            }

            return L"Запуск через расширитель скриптов выбранной игры.";
        }

        [[nodiscard]] std::vector<TemplateCapability> templateCapabilitiesFrom(
            const IGameSupport& support,
            const ExecutableSupportRules* executableRules,
            const LaunchSupportRules* launchRules)
        {
            const CapabilitySet& set = support.capabilities();
            std::vector<TemplateCapability> capabilities;
            addCapability(
                capabilities,
                set,
                GameCapability::Plugins,
                L"plugins",
                L"Плагины и мастера",
                L"Управление плагинами и активными мастер-файлами.");
            addCapability(
                capabilities,
                set,
                GameCapability::LoadOrder,
                L"load-order",
                L"Порядок загрузки",
                L"Сортировка плагинов по правилам выбранной игры.");
            addCapability(
                capabilities,
                set,
                GameCapability::IniProfiles,
                L"ini-tweaks",
                L"INI-твики",
                L"Профильные правки конфигурационных файлов игры.");
            addCapability(
                capabilities,
                set,
                GameCapability::SaveProfiles,
                L"save-games",
                L"Сохранения",
                L"Просмотр сейвов, привязанных к активному профилю.");
            addCapability(
                capabilities,
                set,
                GameCapability::ScriptExtender,
                L"script-extender",
                scriptExtenderDisplayName(executableRules),
                scriptExtenderDescription(launchRules));
            addCapability(
                capabilities,
                set,
                GameCapability::RootFiles,
                L"root-files",
                L"Root files",
                L"Поддержка файлов, размещаемых рядом с исполняемым файлом игры.");
            addCapability(
                capabilities,
                set,
                GameCapability::ContentLayoutRules,
                L"content-layout",
                L"Layout rules",
                L"Правила размещения содержимого архива для выбранной игры.");
            return capabilities;
        }

        [[nodiscard]] std::wstring projectedDataDirectory(const ContentLayoutSupportRules* contentLayoutRules)
        {
            return contentLayoutRules == nullptr ? std::wstring() : contentLayoutRules->dataFolder;
        }

        [[nodiscard]] BuildTemplate projectSupport(const IGameSupport& support)
        {
            const GameIdentityRules& identity = support.identity();
            const GameSupportComponents& components = support.components();
            const PluginSupportRules* pluginRules = components.pluginRulesProvider == nullptr
                ? nullptr
                : &components.pluginRulesProvider->pluginRules();
            const ExecutableSupportRules* executableRules = components.executableRulesProvider == nullptr
                ? nullptr
                : &components.executableRulesProvider->executableRules();
            const LaunchSupportRules* launchRules = components.launchRulesProvider == nullptr
                ? nullptr
                : &components.launchRulesProvider->launchRules();
            const ContentLayoutSupportRules* contentLayoutRules = components.contentLayoutRulesProvider == nullptr
                ? nullptr
                : &components.contentLayoutRulesProvider->contentLayoutRules();

            BuildTemplate projected{};
            // Deprecated bridge id still carries the game id because the
            // current UI sends it back as templateId. The bridge exposes
            // UiTemplateId separately for the additive game-definition shape.
            projected.id = identity.id.value();
            projected.displayName = identity.displayName;
            projected.gameName = identity.displayName;
            projected.summary = identity.summary;
            projected.defaultProfileName = identity.defaultProfileName.empty()
                ? L"Default"
                : identity.defaultProfileName;
            projected.dataDirectory = projectedDataDirectory(contentLayoutRules);
            projected.nexusDomain = identity.domains.empty() ? L"" : identity.domains.front();
            projected.folders = {};
            if (pluginRules != nullptr)
            {
                projected.profileFiles = pluginRules->profileFiles;
                projected.basePlugins = pluginRules->basePlugins;
                projected.pluginExtensions = extensionValues(pluginRules->pluginExtensions);
            }
            if (executableRules != nullptr)
            {
                projected.executables = executableNames(executableRules->executables);
            }
            projected.capabilities = templateCapabilitiesFrom(support, executableRules, launchRules);
            if (launchRules != nullptr && launchRules->rules.scriptExtender.has_value())
            {
                const GameScriptExtenderRules& scriptExtender = launchRules->rules.scriptExtender.value();
                projected.scriptExtender = ScriptExtender{
                    scriptExtender.name,
                    scriptExtender.loaderExecutable.displayName(),
                    scriptExtender.website
                };
            }
            projected.isBase = false;
            return projected;
        }
    }

    TemplateService::TemplateService(Logger& logger) noexcept
        : logger_(logger)
    {
    }

    void TemplateService::initialize()
    {
        if (initialized_)
        {
            return;
        }

        registerBuiltInTemplates();
        initialized_ = true;
        logger_.write(LogLevel::Info, "Template service initialized.");
    }

    void TemplateService::shutdown()
    {
        if (!initialized_)
        {
            return;
        }

        games_.clear();
        base_ = BuildTemplate{};
        gameSupport_.replaceDefinitions({});
        logger_.write(LogLevel::Info, "Template service shut down.");
        initialized_ = false;
    }

    const BuildTemplate& TemplateService::baseTemplate() const noexcept
    {
        return base_;
    }

    const std::vector<BuildTemplate>& TemplateService::gameTemplates() const noexcept
    {
        return games_;
    }

    const BuildTemplate* TemplateService::findGameTemplate(std::wstring_view id) const noexcept
    {
        const GameSupportLookupResult lookup = gameSupport_.lookupById(id);
        if (!lookup.supported || lookup.definition == nullptr)
        {
            return nullptr;
        }

        const std::wstring normalized = lookup.definition->id.value();
        const auto match = std::find_if(
            games_.begin(),
            games_.end(),
            [&normalized](const BuildTemplate& candidate) { return candidate.id == normalized; });

        return match == games_.end() ? nullptr : &(*match);
    }

    BuildTemplate TemplateService::resolve(std::wstring_view gameId) const
    {
        const GameSupportLookupResult lookup = gameSupport_.lookupById(gameId);
        if (!lookup.supported || lookup.support == nullptr)
        {
            throw std::invalid_argument("Unknown game template.");
        }

        // Start from a copy of the base template, then overlay the game on top.
        const BuildTemplate game = projectSupport(*lookup.support);
        BuildTemplate resolved = base_;

        resolved.id = game.id;
        resolved.displayName = game.displayName;
        resolved.gameName = game.gameName;
        resolved.summary = game.summary;
        resolved.baseTemplateId = base_.id;
        resolved.isBase = false;

        if (!game.defaultProfileName.empty())
        {
            resolved.defaultProfileName = game.defaultProfileName;
        }
        if (!game.dataDirectory.empty())
        {
            resolved.dataDirectory = game.dataDirectory;
        }
        if (!game.nexusDomain.empty())
        {
            resolved.nexusDomain = game.nexusDomain;
        }

        mergeStrings(resolved.folders, game.folders);
        mergeStrings(resolved.profileFiles, game.profileFiles);
        // Base plugins are intentionally game-only: a build inherits the base
        // folder structure, but its masters come from the selected game module.
        mergeStrings(resolved.basePlugins, game.basePlugins);
        mergeStrings(resolved.pluginExtensions, game.pluginExtensions);
        mergeStrings(resolved.executables, game.executables);
        mergeCapabilities(resolved.capabilities, game.capabilities);

        if (game.scriptExtender.has_value())
        {
            resolved.scriptExtender = game.scriptExtender;
        }

        return resolved;
    }

    bool TemplateService::isInitialized() const noexcept
    {
        return initialized_;
    }

    void TemplateService::registerBuiltInTemplates()
    {
        // ---- Base template -------------------------------------------------
        // Everything every build shares, regardless of game. Game-specific
        // shape comes from bundled definitions and support modules.
        base_ = BuildTemplate{};
        base_.id = L"base";
        base_.displayName = L"Базовый шаблон";
        base_.gameName = L"";
        base_.summary = L"Общий каркас сборки: рабочие папки, профили и базовые модули мод-менеджера.";
        base_.baseTemplateId = L"";
        base_.defaultProfileName = L"Default";
        base_.dataDirectory = L"";
        base_.nexusDomain = L"";
        base_.folders = {L"mods", L"downloads", L"profiles", L"overwrite", L"webcache", L"logs"};
        base_.profileFiles = {L"modlist.txt"};
        base_.basePlugins = {};
        base_.pluginExtensions = {};
        base_.executables = {};
        base_.capabilities = {
            {L"mod-list", L"Список модов", L"Установка модов и управление их приоритетами."},
            {L"profiles", L"Профили", L"Изолированные профили модлиста и порядка загрузки."},
            {L"downloads", L"Загрузки", L"Хранилище скачанных архивов модов."},
            {L"overwrite", L"Overwrite", L"Перехват файлов, созданных игрой и инструментами."},
        };
        base_.scriptExtender = std::nullopt;
        base_.isBase = true;

        gameSupport_.loadEmbeddedDefinitions();
        games_.clear();
        for (const GameDefinition& definition : gameSupport_.definitions())
        {
            const IGameSupport* support = gameSupport_.supportFor(definition);
            if (support == nullptr)
            {
                throw std::runtime_error("Game definition has no registered support module.");
            }

            games_.push_back(projectSupport(*support));
        }
    }
}
