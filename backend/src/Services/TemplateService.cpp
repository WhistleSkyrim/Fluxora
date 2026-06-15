#include "FluxoraCore/Services/TemplateService.hpp"

#include "FluxoraCore/Services/Logger.hpp"

#include <algorithm>
#include <stdexcept>

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
        const auto match = std::find_if(
            games_.begin(),
            games_.end(),
            [id](const BuildTemplate& candidate) { return candidate.id == id; });

        return match == games_.end() ? nullptr : &(*match);
    }

    BuildTemplate TemplateService::resolve(std::wstring_view gameId) const
    {
        const BuildTemplate* game = findGameTemplate(gameId);
        if (game == nullptr)
        {
            throw std::invalid_argument("Unknown game template.");
        }

        // Start from a copy of the base template, then overlay the game on top.
        BuildTemplate resolved = base_;

        resolved.id = game->id;
        resolved.displayName = game->displayName;
        resolved.gameName = game->gameName;
        resolved.summary = game->summary;
        resolved.baseTemplateId = base_.id;
        resolved.isBase = false;

        if (!game->defaultProfileName.empty())
        {
            resolved.defaultProfileName = game->defaultProfileName;
        }
        if (!game->dataDirectory.empty())
        {
            resolved.dataDirectory = game->dataDirectory;
        }
        if (!game->nexusDomain.empty())
        {
            resolved.nexusDomain = game->nexusDomain;
        }

        mergeStrings(resolved.folders, game->folders);
        mergeStrings(resolved.profileFiles, game->profileFiles);
        // Base plugins are intentionally game-only: a Cyberpunk build inherits the
        // base structure but never Skyrim's masters, because they live on the
        // Skyrim template, not the base.
        mergeStrings(resolved.basePlugins, game->basePlugins);
        mergeStrings(resolved.pluginExtensions, game->pluginExtensions);
        mergeStrings(resolved.executables, game->executables);
        mergeCapabilities(resolved.capabilities, game->capabilities);

        if (game->scriptExtender.has_value())
        {
            resolved.scriptExtender = game->scriptExtender;
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
        // Everything every build shares, regardless of game. Adding a new game
        // means adding a data definition below, never a native plugin.
        base_ = BuildTemplate{};
        base_.id = L"base";
        base_.displayName = L"Базовый шаблон";
        base_.gameName = L"";
        base_.summary = L"Общий каркас сборки: рабочие папки, профили и базовые модули мод-менеджера.";
        base_.baseTemplateId = L"";
        base_.defaultProfileName = L"Default";
        base_.dataDirectory = L"Data";
        base_.nexusDomain = L"";
        base_.folders = {L"mods", L"downloads", L"profiles", L"overwrite", L"webcache", L"logs"};
        base_.profileFiles = {L"modlist.txt", L"plugins.txt", L"loadorder.txt"};
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

        // ---- Skyrim Special Edition ---------------------------------------
        BuildTemplate skyrimSe{};
        skyrimSe.id = L"skyrimse";
        skyrimSe.displayName = L"Skyrim Special Edition";
        skyrimSe.gameName = L"Skyrim Special Edition";
        skyrimSe.summary = L"Сборка для Skyrim SE: плагины .esp/.esm/.esl, порядок загрузки, INI-твики и SKSE64.";
        skyrimSe.defaultProfileName = L"Default";
        skyrimSe.dataDirectory = L"Data";
        skyrimSe.nexusDomain = L"skyrimspecialedition";
        skyrimSe.folders = {};
        skyrimSe.profileFiles = {};
        skyrimSe.basePlugins = {
            L"Skyrim.esm",
            L"Update.esm",
            L"Dawnguard.esm",
            L"HearthFires.esm",
            L"Dragonborn.esm",
        };
        skyrimSe.pluginExtensions = {L".esm", L".esp", L".esl"};
        skyrimSe.executables = {};
        skyrimSe.capabilities = {
            {L"plugins", L"Плагины и мастера", L"Управление .esp/.esm/.esl и активными мастер-файлами."},
            {L"load-order", L"Порядок загрузки", L"Сортировка плагинов, совместимая с правилами LOOT."},
            {L"ini-tweaks", L"INI-твики", L"Профильные правки Skyrim.ini и SkyrimPrefs.ini."},
            {L"save-games", L"Сохранения", L"Просмотр сейвов, привязанных к активному профилю."},
            {L"script-extender", L"SKSE64", L"Запуск через Skyrim Script Extender и проверка версии."},
        };
        skyrimSe.scriptExtender = ScriptExtender{
            L"Skyrim Script Extender (SKSE64)",
            L"skse64_loader.exe",
            L"https://skse.silverlock.org/",
        };
        skyrimSe.isBase = false;

        games_.clear();
        games_.push_back(std::move(skyrimSe));
    }
}
