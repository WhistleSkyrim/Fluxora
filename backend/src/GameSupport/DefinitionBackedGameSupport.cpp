#include "FluxoraCore/GameSupport/DefinitionBackedGameSupport.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <utility>

namespace fluxora
{
    namespace
    {
        template <typename T>
        void pushUnique(std::vector<T>& values, const T& value)
        {
            if (std::find(values.begin(), values.end(), value) == values.end())
            {
                values.push_back(value);
            }
        }

        void pushUnique(std::vector<std::wstring>& values, const std::wstring& value)
        {
            if (std::find(values.begin(), values.end(), value) == values.end())
            {
                values.push_back(value);
            }
        }

        [[nodiscard]] std::wstring normalizedKey(std::wstring_view value)
        {
            return toAsciiLower(trimAscii(value));
        }

        void addKey(std::set<std::wstring>& keys, std::wstring_view value)
        {
            const std::wstring key = normalizedKey(value);
            if (!key.empty())
            {
                keys.insert(key);
            }
        }

        void addExecutableKey(std::set<std::wstring>& keys, const ExecutableName& executable)
        {
            addKey(keys, executable.normalizedName());
        }

        void addExtensionKey(std::set<std::wstring>& keys, const NormalizedExtension& extension)
        {
            if (!extension.value().empty())
            {
                keys.insert(extension.value());
            }
        }

        void addPathKey(std::set<std::wstring>& keys, const std::filesystem::path& path)
        {
            const std::wstring key = toAsciiLower(path.lexically_normal().generic_wstring());
            if (!key.empty())
            {
                keys.insert(key);
            }
        }

        [[nodiscard]] std::wstring profileFileNamed(
            const std::vector<std::wstring>& profileFiles,
            std::wstring_view fileName)
        {
            const std::wstring requested = normalizedKey(fileName);
            const auto match = std::find_if(
                profileFiles.begin(),
                profileFiles.end(),
                [&requested](const std::wstring& candidate)
                {
                    return normalizedKey(std::filesystem::path(candidate).filename().wstring()) == requested;
                });

            return match == profileFiles.end() ? std::wstring() : *match;
        }

        void pushUniquePath(std::vector<std::filesystem::path>& values, std::filesystem::path value)
        {
            const std::wstring key = toAsciiLower(value.lexically_normal().generic_wstring());
            const auto match = std::find_if(
                values.begin(),
                values.end(),
                [&key](const std::filesystem::path& candidate)
                {
                    return toAsciiLower(candidate.lexically_normal().generic_wstring()) == key;
                });

            if (match == values.end())
            {
                values.push_back(std::move(value));
            }
        }

        [[nodiscard]] CompiledGameRules compileRules(const GameDefinition& definition)
        {
            CompiledGameRules rules;
            rules.identity = GameIdentityRules{
                definition.id,
                definition.displayName,
                definition.summary,
                definition.definitionVersion,
                definition.defaultProfileName,
                definition.uiTemplateId,
                definition.aliases,
                definition.domains,
                definition.installFolderAliases
            };
            rules.capabilities = definition.capabilities;

            rules.detection.executableNames = definition.detectionHints.executableNames;
            for (const GameExecutableDefinition& executable : definition.executables)
            {
                pushUnique(rules.detection.executableNames, executable.name);
            }
            rules.detection.folderNames = definition.detectionHints.folderNames;
            for (const std::wstring& folderName : definition.installFolderAliases)
            {
                pushUnique(rules.detection.folderNames, folderName);
            }
            rules.detection.domains = definition.detectionHints.domains;
            for (const std::wstring& domain : definition.domains)
            {
                pushUnique(rules.detection.domains, domain);
            }
            rules.detection.requiredFiles = definition.healthRules.requiredFiles.empty()
                ? definition.requiredFiles
                : definition.healthRules.requiredFiles;

            rules.plugins = PluginSupportRules{
                definition.pluginExtensions,
                definition.pluginRules.profileFiles,
                definition.pluginRules.basePlugins
            };
            pushUniquePath(rules.plugins.pluginSearchDirectories, {});
            const std::wstring pluginDataFolder = definition.contentLayoutRules.dataFolder.empty()
                ? definition.dataFolder
                : definition.contentLayoutRules.dataFolder;
            if (!pluginDataFolder.empty())
            {
                pushUniquePath(rules.plugins.pluginSearchDirectories, std::filesystem::path(pluginDataFolder));
            }
            rules.plugins.activePluginsFileName =
                profileFileNamed(definition.pluginRules.profileFiles, L"plugins.txt");
            rules.plugins.loadOrderFileName =
                profileFileNamed(definition.pluginRules.profileFiles, L"loadorder.txt");
            rules.plugins.basePluginSourceLabel = definition.displayName.empty()
                ? std::wstring(L"Base game")
                : definition.displayName;
            rules.plugins.basePluginLockReason = L"Базовый мастер игры: всегда сверху";
            rules.executables = ExecutableSupportRules{
                definition.executables,
                definition.executableRoles
            };
            rules.launch = LaunchSupportRules{definition.launchRules};
            rules.vfs = VfsSupportRules{definition.vfsRules};
            rules.contentLayout = ContentLayoutSupportRules{
                definition.contentLayoutRules.dataFolder.empty()
                    ? definition.dataFolder
                    : definition.contentLayoutRules.dataFolder,
                definition.contentLayoutRules.supportsRootFiles,
                definition.contentLayoutRules.rootFileWrapperDirectory,
                definition.pluginExtensions,
                definition.archiveExtensions,
                {},
                {},
                {}
            };
            if (definition.executableRoles.scriptExtender.has_value())
            {
                pushUnique(
                    rules.contentLayout.scriptExtenderLoaders,
                    definition.executableRoles.scriptExtender.value());
            }
            for (const GameExecutableDefinition& executable : definition.executables)
            {
                if (executable.role == GameExecutableRole::ScriptExtender)
                {
                    pushUnique(rules.contentLayout.scriptExtenderLoaders, executable.name);
                }
            }
            rules.ui = UiSupportRules{
                definition.uiTemplateId,
                definition.displayName
            };
            rules.health = HealthSupportRules{rules.detection.requiredFiles};

            for (const std::wstring& domain : rules.detection.domains)
            {
                addKey(rules.domainKeys, domain);
                addKey(rules.detection.domainKeys, domain);
            }
            for (const ExecutableName& executable : rules.detection.executableNames)
            {
                addExecutableKey(rules.executableNameKeys, executable);
                addExecutableKey(rules.detection.executableNameKeys, executable);
            }
            for (const std::wstring& folder : rules.detection.folderNames)
            {
                addKey(rules.installFolderKeys, folder);
                addKey(rules.detection.folderNameKeys, folder);
            }
            for (const std::wstring& requiredFile : rules.detection.requiredFiles)
            {
                addPathKey(rules.detection.requiredFileKeys, std::filesystem::path(requiredFile));
                addPathKey(rules.health.requiredFileKeys, std::filesystem::path(requiredFile));
            }
            for (const NormalizedExtension& extension : rules.plugins.pluginExtensions)
            {
                addExtensionKey(rules.plugins.pluginExtensionKeys, extension);
            }
            for (const NormalizedExtension& extension : rules.plugins.masterPluginExtensions)
            {
                addExtensionKey(rules.plugins.masterPluginExtensionKeys, extension);
            }
            for (const NormalizedExtension& extension : rules.plugins.lightPluginExtensions)
            {
                addExtensionKey(rules.plugins.lightPluginExtensionKeys, extension);
            }
            for (const std::wstring& basePlugin : rules.plugins.basePlugins)
            {
                addKey(rules.plugins.basePluginKeys, basePlugin);
            }
            for (const std::filesystem::path& directory : rules.plugins.pluginSearchDirectories)
            {
                addPathKey(rules.plugins.pluginSearchDirectoryKeys, directory);
            }
            for (const NormalizedExtension& extension : rules.contentLayout.pluginExtensions)
            {
                addExtensionKey(rules.contentLayout.pluginExtensionKeys, extension);
            }
            for (const NormalizedExtension& extension : rules.contentLayout.archiveExtensions)
            {
                addExtensionKey(rules.contentLayout.archiveExtensionKeys, extension);
            }
            for (const ExecutableName& loader : rules.contentLayout.scriptExtenderLoaders)
            {
                addExecutableKey(rules.contentLayout.scriptExtenderLoaderKeys, loader);
            }
            for (const std::wstring& directory : rules.contentLayout.gameDataDirectories)
            {
                addKey(rules.contentLayout.gameDataDirectoryKeys, directory);
            }
            for (const std::filesystem::path& path : rules.contentLayout.scriptExtenderDataPaths)
            {
                addPathKey(rules.contentLayout.scriptExtenderDataPathKeys, path);
            }

            return rules;
        }
    }

    DefinitionBackedGameSupport::DefinitionBackedGameSupport(const GameDefinition& definition)
        : definition_(&definition),
          rules_(compileRules(definition))
    {
        components_ = GameSupportComponents{
            this,
            this,
            this,
            this,
            this,
            this,
            this,
            this,
            this,
            this
        };
    }

    const GameDefinition& DefinitionBackedGameSupport::definition() const noexcept
    {
        return *definition_;
    }

    const GameIdentityRules& DefinitionBackedGameSupport::identity() const noexcept
    {
        return rules_.identity;
    }

    const CapabilitySet& DefinitionBackedGameSupport::capabilities() const noexcept
    {
        return rules_.capabilities;
    }

    const GameSupportComponents& DefinitionBackedGameSupport::components() const noexcept
    {
        return components_;
    }

    const GameIdentityRules& DefinitionBackedGameSupport::identityRules() const noexcept
    {
        return rules_.identity;
    }

    const GameDetectionRules& DefinitionBackedGameSupport::detectionRules() const noexcept
    {
        return rules_.detection;
    }

    const HealthSupportRules& DefinitionBackedGameSupport::healthRules() const noexcept
    {
        return rules_.health;
    }

    const PluginSupportRules& DefinitionBackedGameSupport::pluginRules() const noexcept
    {
        return rules_.plugins;
    }

    const ExecutableSupportRules& DefinitionBackedGameSupport::executableRules() const noexcept
    {
        return rules_.executables;
    }

    const LaunchSupportRules& DefinitionBackedGameSupport::launchRules() const noexcept
    {
        return rules_.launch;
    }

    const VfsSupportRules& DefinitionBackedGameSupport::vfsRules() const noexcept
    {
        return rules_.vfs;
    }

    const ContentLayoutSupportRules& DefinitionBackedGameSupport::contentLayoutRules() const noexcept
    {
        return rules_.contentLayout;
    }

    const UiSupportRules& DefinitionBackedGameSupport::uiRules() const noexcept
    {
        return rules_.ui;
    }

    const ManifestMigrationRules& DefinitionBackedGameSupport::manifestMigrationRules() const noexcept
    {
        return rules_.manifestMigration;
    }
}
