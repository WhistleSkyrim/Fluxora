#include "FluxoraCore/GameSupport/SkyrimSpecialEditionSupport.hpp"

#include <filesystem>
#include <set>

namespace fluxora
{
    namespace
    {
        [[nodiscard]] std::wstring normalizedKey(std::wstring_view value)
        {
            return toAsciiLower(trimAscii(value));
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
    }

    SkyrimSpecialEditionSupport::SkyrimSpecialEditionSupport(const GameDefinition& definition)
        : DefinitionBackedGameSupport(definition)
    {
        pluginRules_ = DefinitionBackedGameSupport::pluginRules();
        pluginRules_.masterPluginExtensions = {
            NormalizedExtension::parseOrThrow(L".esm")
        };
        pluginRules_.lightPluginExtensions = {
            NormalizedExtension::parseOrThrow(L".esl")
        };
        for (const NormalizedExtension& extension : pluginRules_.masterPluginExtensions)
        {
            addExtensionKey(pluginRules_.masterPluginExtensionKeys, extension);
        }
        for (const NormalizedExtension& extension : pluginRules_.lightPluginExtensions)
        {
            addExtensionKey(pluginRules_.lightPluginExtensionKeys, extension);
        }

        contentLayoutRules_ = DefinitionBackedGameSupport::contentLayoutRules();
        contentLayoutRules_.gameDataDirectories = {
            L"animations",
            L"calientetools",
            L"dialogueviews",
            L"distantlod",
            L"facegendata",
            L"interface",
            L"lodsettings",
            L"materials",
            L"meshes",
            L"music",
            L"scripts",
            L"seq",
            L"shaderfx",
            L"sound",
            L"strings",
            L"textures",
            L"video",
            L"skse",
            L"dllplugins",
            L"netscriptframework"
        };
        contentLayoutRules_.scriptExtenderDataPaths = {
            L"SKSE/Plugins",
            L"DLLPlugins",
            L"NetScriptFramework",
            L"Plugins"
        };
        for (const std::wstring& directory : contentLayoutRules_.gameDataDirectories)
        {
            const std::wstring key = normalizedKey(directory);
            if (!key.empty())
            {
                contentLayoutRules_.gameDataDirectoryKeys.insert(key);
            }
        }
        for (const std::filesystem::path& path : contentLayoutRules_.scriptExtenderDataPaths)
        {
            addPathKey(contentLayoutRules_.scriptExtenderDataPathKeys, path);
        }

        manifestMigrationRules_.supportsAutomaticMigration = true;
    }

    GameId SkyrimSpecialEditionSupport::gameId()
    {
        return GameId::parseOrThrow(L"skyrimse");
    }

    bool SkyrimSpecialEditionSupport::supportsDefinition(const GameDefinition& definition) noexcept
    {
        return definition.id.value() == L"skyrimse";
    }

    const PluginSupportRules& SkyrimSpecialEditionSupport::pluginRules() const noexcept
    {
        return pluginRules_;
    }

    const ContentLayoutSupportRules& SkyrimSpecialEditionSupport::contentLayoutRules() const noexcept
    {
        return contentLayoutRules_;
    }

    const ManifestMigrationRules& SkyrimSpecialEditionSupport::manifestMigrationRules() const noexcept
    {
        return manifestMigrationRules_;
    }
}
