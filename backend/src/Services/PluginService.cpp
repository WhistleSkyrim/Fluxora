#include "FluxoraCore/Services/PluginService.hpp"

#include "FluxoraCore/Services/BuildPathSettingsService.hpp"
#include "FluxoraCore/Services/Logger.hpp"
#include "FluxoraCore/Storage/InstanceMetadataStore.hpp"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fluxora
{
    namespace
    {
        constexpr std::wstring_view fallbackProfileName = L"Default";
        constexpr std::wstring_view pluginsFileName = L"plugins.txt";
        constexpr std::wstring_view loadOrderFileName = L"loadorder.txt";
        constexpr std::wstring_view transientFileExtension = L".tmp";
        constexpr std::wstring_view skyrimTemplateId = L"skyrimse";
        constexpr std::wstring_view pluginKind = L"plugin";
        constexpr std::wstring_view separatorKind = L"separator";

        struct DetectedPlugin
        {
            std::wstring name;
            std::wstring extension;
            std::wstring sourceMod;
            bool sourceModEnabled{true};
        };

        struct StoredPlugin
        {
            std::wstring name;
            bool isEnabled{true};
        };

        std::wstring toLower(std::wstring value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character)
            {
                return static_cast<wchar_t>(std::towlower(character));
            });
            return value;
        }

        std::wstring toUpper(std::wstring value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character)
            {
                return static_cast<wchar_t>(std::towupper(character));
            });
            return value;
        }

        std::wstring trim(std::wstring value)
        {
            const auto first = value.find_first_not_of(L" \t\r\n");
            if (first == std::wstring::npos)
            {
                return {};
            }

            const auto last = value.find_last_not_of(L" \t\r\n");
            return value.substr(first, last - first + 1);
        }

        bool equalsIgnoreCase(std::wstring_view left, std::wstring_view right)
        {
            return toLower(std::wstring(left)) == toLower(std::wstring(right));
        }

        bool isSkyrimTemplate(const BuildTemplate& resolvedTemplate)
        {
            return equalsIgnoreCase(resolvedTemplate.id, skyrimTemplateId);
        }

        std::wstring normalizeExtension(std::filesystem::path path)
        {
            return toLower(path.extension().wstring());
        }

        std::wstring extensionLabel(const std::filesystem::path& path)
        {
            const std::wstring extension = path.extension().wstring();
            return extension.size() > 1 ? toUpper(extension.substr(1)) : std::wstring();
        }

        bool hasPluginExtension(
            const std::filesystem::path& path,
            const std::vector<std::wstring>& pluginExtensions)
        {
            const std::wstring extension = normalizeExtension(path);
            return std::any_of(
                pluginExtensions.begin(),
                pluginExtensions.end(),
                [&extension](const std::wstring& candidate)
                {
                    return extension == toLower(candidate);
                });
        }

        std::wstring profileNameOrDefault(const BuildTemplate& resolvedTemplate, std::wstring_view profileName)
        {
            if (!profileName.empty())
            {
                return std::wstring(profileName);
            }

            return resolvedTemplate.defaultProfileName.empty()
                ? std::wstring(fallbackProfileName)
                : resolvedTemplate.defaultProfileName;
        }

        std::filesystem::path profileDirectory(
            const BuildPathSettingsService& pathSettings,
            const std::filesystem::path& projectDirectory,
            const BuildTemplate& resolvedTemplate,
            std::wstring_view profileName)
        {
            return pathSettings.profilesDirectory(projectDirectory) /
                std::filesystem::path(profileNameOrDefault(resolvedTemplate, profileName));
        }

        std::filesystem::path pluginsFilePath(
            const BuildPathSettingsService& pathSettings,
            const std::filesystem::path& projectDirectory,
            const BuildTemplate& resolvedTemplate,
            std::wstring_view profileName)
        {
            return profileDirectory(pathSettings, projectDirectory, resolvedTemplate, profileName) /
                std::filesystem::path(pluginsFileName);
        }

        std::string toUtf8(const std::wstring& value)
        {
#ifdef _WIN32
            if (value.empty())
            {
                return {};
            }

            const int size = WideCharToMultiByte(
                CP_UTF8,
                0,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0,
                nullptr,
                nullptr);
            std::string out(static_cast<std::size_t>(size), '\0');
            WideCharToMultiByte(
                CP_UTF8,
                0,
                value.data(),
                static_cast<int>(value.size()),
                out.data(),
                size,
                nullptr,
                nullptr);
            return out;
#else
            return std::string(value.begin(), value.end());
#endif
        }

        std::wstring fromUtf8(const std::string& value)
        {
#ifdef _WIN32
            if (value.empty())
            {
                return {};
            }

            const int size = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0);
            if (size <= 0)
            {
                throw std::invalid_argument("plugins.txt is not valid UTF-8.");
            }

            std::wstring out(static_cast<std::size_t>(size), L'\0');
            MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                out.data(),
                size);
            return out;
#else
            return std::wstring(value.begin(), value.end());
#endif
        }

        std::string readTextFile(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::in | std::ios::binary);
            if (!file)
            {
                return {};
            }

            return std::string(
                std::istreambuf_iterator<char>(file),
                std::istreambuf_iterator<char>());
        }

        void writeTextFile(const std::filesystem::path& path, const std::string& content)
        {
            const std::filesystem::path parent = path.parent_path();
            if (!parent.empty())
            {
                std::filesystem::create_directories(parent);
            }

            const std::filesystem::path temporaryPath = path.wstring() + std::wstring(transientFileExtension);
            std::ofstream file(temporaryPath, std::ios::out | std::ios::trunc | std::ios::binary);
            if (!file)
            {
                throw std::runtime_error("Failed to write plugins.txt.");
            }

            file.write(content.data(), static_cast<std::streamsize>(content.size()));
            file.close();

            std::error_code error;
            std::filesystem::rename(temporaryPath, path, error);
            if (!error)
            {
                return;
            }

            std::filesystem::remove(path, error);
            std::filesystem::rename(temporaryPath, path);
        }

        bool isBasePlugin(const BuildTemplate& resolvedTemplate, std::wstring_view pluginName)
        {
            return std::any_of(
                resolvedTemplate.basePlugins.begin(),
                resolvedTemplate.basePlugins.end(),
                [pluginName](const std::wstring& basePlugin)
                {
                    return equalsIgnoreCase(basePlugin, pluginName);
                });
        }

        std::vector<StoredPlugin> readStoredPlugins(const std::filesystem::path& path)
        {
            std::vector<StoredPlugin> result;
            const std::string content = readTextFile(path);
            if (content.empty())
            {
                return result;
            }

            std::wstring text = fromUtf8(content);
            std::set<std::wstring> seen;
            std::size_t offset = 0;
            while (offset <= text.size())
            {
                const std::size_t lineEnd = text.find_first_of(L"\r\n", offset);
                std::wstring line = lineEnd == std::wstring::npos
                    ? text.substr(offset)
                    : text.substr(offset, lineEnd - offset);

                bool enabled = false;
                line = trim(std::move(line));
                if (!line.empty() && line.front() == L'*')
                {
                    enabled = true;
                    line = trim(line.substr(1));
                }

                if (!line.empty() && line.front() != L'#')
                {
                    const std::filesystem::path pluginPath(line);
                    const std::wstring fileName = pluginPath.filename().wstring();
                    const std::wstring key = toLower(fileName);
                    if (!fileName.empty() && seen.insert(key).second)
                    {
                        result.push_back(StoredPlugin{fileName, enabled});
                    }
                }

                if (lineEnd == std::wstring::npos)
                {
                    break;
                }

                offset = lineEnd + 1;
                if (offset < text.size() && text[lineEnd] == L'\r' && text[offset] == L'\n')
                {
                    ++offset;
                }
            }

            return result;
        }

        std::string serializeStoredPlugins(const std::vector<StoredPlugin>& plugins)
        {
            std::wstring text;
            for (const StoredPlugin& plugin : plugins)
            {
                if (plugin.isEnabled)
                {
                    text.push_back(L'*');
                }

                text.append(plugin.name);
                text.push_back(L'\n');
            }

            return toUtf8(text);
        }

        std::map<std::wstring, DetectedPlugin> detectInstalledPlugins(
            const BuildPathSettingsService& pathSettings,
            const std::filesystem::path& projectDirectory,
            const BuildTemplate& resolvedTemplate,
            std::wstring_view profileName)
        {
            std::map<std::wstring, DetectedPlugin> detected;
            const std::filesystem::path directory = pathSettings.modsDirectory(projectDirectory);
            if (!std::filesystem::exists(directory))
            {
                return detected;
            }

            struct ProfileModFolder
            {
                std::wstring folderName;
                bool isEnabled{true};
            };

            std::vector<ProfileModFolder> profileModFolders;
            for (const ProfileOrderItemRecord& item :
                 InstanceMetadataStore::listProfileOrderItems(projectDirectory, profileName, directory))
            {
                if (item.kind == L"mod" && item.hasMod)
                {
                    profileModFolders.push_back(ProfileModFolder{
                        item.mod.folderName,
                        item.mod.state != L"disabled"
                    });
                }
            }

            const auto addPlugin = [&detected, &resolvedTemplate](
                const std::filesystem::directory_entry& entry,
                const std::wstring& sourceMod,
                bool sourceModEnabled)
            {
                if (!entry.is_regular_file() ||
                    !hasPluginExtension(entry.path(), resolvedTemplate.pluginExtensions))
                {
                    return;
                }

                const std::wstring fileName = entry.path().filename().wstring();
                const std::wstring key = toLower(fileName);
                if (!sourceModEnabled)
                {
                    const auto existing = detected.find(key);
                    if (existing != detected.end() && existing->second.sourceModEnabled)
                    {
                        return;
                    }
                }

                detected[key] = DetectedPlugin{
                    fileName,
                    extensionLabel(entry.path()),
                    sourceMod,
                    sourceModEnabled
                };
            };

            for (const ProfileModFolder& folder : profileModFolders)
            {
                const std::filesystem::path modDirectory = directory / std::filesystem::path(folder.folderName);
                if (!std::filesystem::exists(modDirectory) || !std::filesystem::is_directory(modDirectory))
                {
                    continue;
                }

                std::error_code error;
                for (const auto& entry : std::filesystem::directory_iterator(
                         modDirectory,
                         std::filesystem::directory_options::skip_permission_denied,
                         error))
                {
                    if (error)
                    {
                        break;
                    }

                    addPlugin(entry, folder.folderName, folder.isEnabled);
                }

                if (!resolvedTemplate.dataDirectory.empty())
                {
                    const std::filesystem::path dataDirectory =
                        modDirectory / std::filesystem::path(resolvedTemplate.dataDirectory);
                    std::error_code dataError;
                    if (std::filesystem::exists(dataDirectory, dataError) &&
                        std::filesystem::is_directory(dataDirectory, dataError))
                    {
                        for (const auto& entry : std::filesystem::directory_iterator(
                                 dataDirectory,
                                 std::filesystem::directory_options::skip_permission_denied,
                                 dataError))
                        {
                            if (dataError)
                            {
                                break;
                            }

                            addPlugin(entry, folder.folderName, folder.isEnabled);
                        }
                    }
                }
            }

            return detected;
        }

        std::vector<StoredPlugin> reconcileStoredPlugins(
            const BuildPathSettingsService& pathSettings,
            const std::filesystem::path& projectDirectory,
            const BuildTemplate& resolvedTemplate,
            std::wstring_view profileName,
            const std::map<std::wstring, DetectedPlugin>& detected)
        {
            const std::filesystem::path pluginsPath =
                pluginsFilePath(pathSettings, projectDirectory, resolvedTemplate, profileName);
            std::vector<StoredPlugin> stored = readStoredPlugins(pluginsPath);
            const std::vector<StoredPlugin> loadOrder =
                readStoredPlugins(profileDirectory(pathSettings, projectDirectory, resolvedTemplate, profileName) /
                    std::filesystem::path(loadOrderFileName));

            std::set<std::wstring> storedKeys;
            for (const StoredPlugin& plugin : stored)
            {
                storedKeys.insert(toLower(plugin.name));
            }

            for (const StoredPlugin& plugin : loadOrder)
            {
                const std::wstring key = toLower(plugin.name);
                if (!key.empty() && storedKeys.insert(key).second)
                {
                    stored.push_back(StoredPlugin{plugin.name, false});
                }
            }

            std::vector<StoredPlugin> reconciled;
            std::set<std::wstring> included;

            for (const std::wstring& basePlugin : resolvedTemplate.basePlugins)
            {
                const std::wstring key = toLower(basePlugin);
                if (included.insert(key).second)
                {
                    reconciled.push_back(StoredPlugin{basePlugin, true});
                }
            }

            for (const StoredPlugin& plugin : stored)
            {
                const std::wstring key = toLower(plugin.name);
                if (included.contains(key) || isBasePlugin(resolvedTemplate, plugin.name))
                {
                    continue;
                }

                const auto detectedPlugin = detected.find(key);
                if (detectedPlugin == detected.end())
                {
                    continue;
                }

                included.insert(key);
                reconciled.push_back(StoredPlugin{
                    detectedPlugin->second.name,
                    detectedPlugin->second.sourceModEnabled ? plugin.isEnabled : false
                });
            }

            for (const auto& [key, plugin] : detected)
            {
                if (!included.contains(key) && plugin.sourceModEnabled)
                {
                    included.insert(key);
                    reconciled.push_back(StoredPlugin{plugin.name, true});
                }
            }

            if (serializeStoredPlugins(stored) != serializeStoredPlugins(reconciled))
            {
                writeTextFile(pluginsPath, serializeStoredPlugins(reconciled));
            }

            return reconciled;
        }

        void writeStoredPlugins(
            const BuildPathSettingsService& pathSettings,
            const std::filesystem::path& projectDirectory,
            const BuildTemplate& resolvedTemplate,
            std::wstring_view profileName,
            const std::vector<StoredPlugin>& plugins)
        {
            writeTextFile(
                pluginsFilePath(pathSettings, projectDirectory, resolvedTemplate, profileName),
                serializeStoredPlugins(plugins));
        }

        void writeStoredPluginsIfChanged(
            const BuildPathSettingsService& pathSettings,
            const std::filesystem::path& projectDirectory,
            const BuildTemplate& resolvedTemplate,
            std::wstring_view profileName,
            const std::vector<StoredPlugin>& currentPlugins,
            const std::vector<StoredPlugin>& desiredPlugins)
        {
            if (serializeStoredPlugins(currentPlugins) == serializeStoredPlugins(desiredPlugins))
            {
                return;
            }

            writeStoredPlugins(pathSettings, projectDirectory, resolvedTemplate, profileName, desiredPlugins);
        }

        std::vector<std::wstring> storedPluginNames(const std::vector<StoredPlugin>& plugins)
        {
            std::vector<std::wstring> names;
            names.reserve(plugins.size());
            for (const StoredPlugin& plugin : plugins)
            {
                names.push_back(plugin.name);
            }

            return names;
        }

        std::map<std::wstring, StoredPlugin> storedPluginsByName(const std::vector<StoredPlugin>& plugins)
        {
            std::map<std::wstring, StoredPlugin> entries;
            for (const StoredPlugin& plugin : plugins)
            {
                entries.emplace(toLower(plugin.name), plugin);
            }

            return entries;
        }

        std::vector<PluginEntry> buildEntries(
            const std::filesystem::path& projectDirectory,
            const BuildTemplate& resolvedTemplate,
            const std::vector<StoredPlugin>& stored,
            const std::vector<ProfilePluginOrderItemRecord>& orderRecords,
            const std::map<std::wstring, DetectedPlugin>& detected)
        {
            (void)projectDirectory;
            const std::map<std::wstring, StoredPlugin> storedByName = storedPluginsByName(stored);

            std::vector<PluginEntry> entries;
            entries.reserve(orderRecords.size());
            for (const ProfilePluginOrderItemRecord& record : orderRecords)
            {
                if (record.kind == separatorKind)
                {
                    const std::wstring title = record.separatorTitle.empty()
                        ? std::wstring(L"Разделитель")
                        : record.separatorTitle;
                    entries.push_back(PluginEntry{
                        record.id,
                        std::wstring(separatorKind),
                        static_cast<int>(entries.size()),
                        title,
                        {},
                        {},
                        true,
                        false,
                        false,
                        false,
                        {},
                        title
                    });
                    continue;
                }

                if (record.kind != pluginKind)
                {
                    continue;
                }

                const auto storedPlugin = storedByName.find(toLower(record.pluginName));
                if (storedPlugin == storedByName.end())
                {
                    continue;
                }

                const StoredPlugin& plugin = storedPlugin->second;
                const std::wstring key = toLower(plugin.name);
                const auto detectedPlugin = detected.find(key);
                const bool locked = isBasePlugin(resolvedTemplate, plugin.name);
                const std::wstring extension = detectedPlugin == detected.end()
                    ? extensionLabel(std::filesystem::path(plugin.name))
                    : detectedPlugin->second.extension;
                const bool enabled = locked || (
                    plugin.isEnabled &&
                    (detectedPlugin == detected.end() || detectedPlugin->second.sourceModEnabled));

                entries.push_back(PluginEntry{
                    record.id,
                    std::wstring(pluginKind),
                    static_cast<int>(entries.size()),
                    plugin.name,
                    extension,
                    locked ? std::wstring(L"Skyrim") :
                        (detectedPlugin == detected.end() ? std::wstring() : detectedPlugin->second.sourceMod),
                    enabled,
                    locked || equalsIgnoreCase(extension, L"ESM"),
                    equalsIgnoreCase(extension, L"ESL"),
                    locked,
                    locked ? std::wstring(L"Базовый мастер Skyrim: всегда сверху") : std::wstring(),
                    {}
                });
            }

            return entries;
        }

        std::vector<StoredPlugin> storedPluginsFromEntries(const std::vector<PluginEntry>& entries)
        {
            std::vector<StoredPlugin> plugins;
            for (const PluginEntry& entry : entries)
            {
                if (entry.kind == pluginKind)
                {
                    plugins.push_back(StoredPlugin{entry.name, entry.isEnabled});
                }
            }

            return plugins;
        }

        int firstUnlockedPluginTargetIndex(const std::vector<PluginEntry>& entries)
        {
            int lastLockedPluginIndex = -1;
            for (int index = 0; index < static_cast<int>(entries.size()); ++index)
            {
                const PluginEntry& entry = entries[static_cast<std::size_t>(index)];
                if (entry.kind == pluginKind && entry.isLocked)
                {
                    lastLockedPluginIndex = index;
                }
            }

            return lastLockedPluginIndex + 1;
        }

        int pluginMoveBlockEnd(const std::vector<PluginEntry>& entries, int sourceIndex)
        {
            if (entries[static_cast<std::size_t>(sourceIndex)].kind != separatorKind)
            {
                return sourceIndex + 1;
            }

            for (int index = sourceIndex + 1; index < static_cast<int>(entries.size()); ++index)
            {
                if (entries[static_cast<std::size_t>(index)].kind == separatorKind)
                {
                    return index;
                }
            }

            return static_cast<int>(entries.size());
        }

        bool pluginMoveBlockContainsPlugin(
            const std::vector<PluginEntry>& entries,
            int sourceIndex,
            int blockEnd)
        {
            for (int index = sourceIndex; index < blockEnd; ++index)
            {
                if (entries[static_cast<std::size_t>(index)].kind == pluginKind)
                {
                    return true;
                }
            }

            return false;
        }

        bool pluginMoveBlockContainsLockedPlugin(
            const std::vector<PluginEntry>& entries,
            int sourceIndex,
            int blockEnd)
        {
            for (int index = sourceIndex; index < blockEnd; ++index)
            {
                const PluginEntry& entry = entries[static_cast<std::size_t>(index)];
                if (entry.kind == pluginKind && entry.isLocked)
                {
                    return true;
                }
            }

            return false;
        }

        int clampExistingPluginOrderTarget(
            const std::vector<PluginEntry>& entries,
            const PluginEntry& movingEntry,
            int targetIndex)
        {
            if (entries.empty())
            {
                return 0;
            }

            const int maxTarget = static_cast<int>(entries.size() - 1);
            const int pluginMinTarget = firstUnlockedPluginTargetIndex(entries);
            if (movingEntry.kind == separatorKind)
            {
                const int sourceIndex = movingEntry.order;
                const int blockEnd = pluginMoveBlockEnd(entries, sourceIndex);
                if (pluginMoveBlockContainsLockedPlugin(entries, sourceIndex, blockEnd))
                {
                    throw std::invalid_argument("This separator contains locked plugins and cannot be moved.");
                }

                const int separatorMinTarget = pluginMoveBlockContainsPlugin(entries, sourceIndex, blockEnd)
                    ? (pluginMinTarget < maxTarget ? pluginMinTarget : maxTarget)
                    : 0;
                return std::clamp(targetIndex, separatorMinTarget, maxTarget);
            }

            const int minTarget = pluginMinTarget < maxTarget ? pluginMinTarget : maxTarget;
            return std::clamp(targetIndex, minTarget, maxTarget);
        }

        int clampPluginSeparatorInsertionTarget(const std::vector<PluginEntry>& entries, int targetIndex)
        {
            const int maxTarget = static_cast<int>(entries.size());
            return std::clamp(targetIndex, 0, maxTarget);
        }

        std::vector<ProfilePluginOrderItemRecord> syncPluginOrderItems(
            const std::filesystem::path& projectDirectory,
            std::wstring_view profileName,
            const std::vector<StoredPlugin>& stored)
        {
            return InstanceMetadataStore::listProfilePluginOrderItems(
                projectDirectory,
                profileName,
                storedPluginNames(stored));
        }

        const PluginEntry* findPluginOrderEntry(
            const std::vector<PluginEntry>& entries,
            std::wstring_view orderItemId)
        {
            for (const PluginEntry& entry : entries)
            {
                if (equalsIgnoreCase(entry.orderId, orderItemId) ||
                    (entry.kind == pluginKind && equalsIgnoreCase(entry.name, orderItemId)))
                {
                    return &entry;
                }
            }

            return nullptr;
        }

        void ensurePluginSystemSupported(const BuildTemplate& resolvedTemplate)
        {
            if (!isSkyrimTemplate(resolvedTemplate))
            {
                throw std::invalid_argument("Plugin management is currently available for Skyrim builds only.");
            }

            if (resolvedTemplate.pluginExtensions.empty())
            {
                throw std::invalid_argument("The selected build template does not define plugin extensions.");
            }
        }
    }

    PluginService::PluginService(
        Logger& logger,
        const BuildPathSettingsService& pathSettings) noexcept
        : logger_(logger),
          pathSettings_(pathSettings)
    {
    }

    void PluginService::initialize()
    {
        if (initialized_)
        {
            return;
        }

        initialized_ = true;
        logger_.write(LogLevel::Info, "Plugin service initialized.");
    }

    void PluginService::shutdown()
    {
        if (!initialized_)
        {
            return;
        }

        logger_.write(LogLevel::Info, "Plugin service shut down.");
        initialized_ = false;
    }

    std::vector<PluginEntry> PluginService::listPlugins(
        const std::filesystem::path& projectDirectory,
        const BuildTemplate& resolvedTemplate,
        std::wstring_view profileName) const
    {
        if (projectDirectory.empty())
        {
            throw std::invalid_argument("Project directory is required.");
        }

        ensurePluginSystemSupported(resolvedTemplate);
        std::filesystem::create_directories(
            profileDirectory(pathSettings_, projectDirectory, resolvedTemplate, profileName));

        const std::map<std::wstring, DetectedPlugin> detected =
            detectInstalledPlugins(pathSettings_, projectDirectory, resolvedTemplate, profileName);
        std::vector<StoredPlugin> stored =
            reconcileStoredPlugins(pathSettings_, projectDirectory, resolvedTemplate, profileName, detected);
        std::vector<ProfilePluginOrderItemRecord> orderRecords =
            syncPluginOrderItems(projectDirectory, profileName, stored);
        std::vector<PluginEntry> entries =
            buildEntries(projectDirectory, resolvedTemplate, stored, orderRecords, detected);
        writeStoredPluginsIfChanged(
            pathSettings_,
            projectDirectory,
            resolvedTemplate,
            profileName,
            stored,
            storedPluginsFromEntries(entries));
        return entries;
    }

    std::vector<PluginEntry> PluginService::movePlugin(
        const std::filesystem::path& projectDirectory,
        const BuildTemplate& resolvedTemplate,
        std::wstring_view profileName,
        std::wstring_view orderItemId,
        int targetIndex) const
    {
        if (orderItemId.empty())
        {
            throw std::invalid_argument("Plugin order item id is required.");
        }

        ensurePluginSystemSupported(resolvedTemplate);
        std::filesystem::create_directories(
            profileDirectory(pathSettings_, projectDirectory, resolvedTemplate, profileName));

        const std::map<std::wstring, DetectedPlugin> detected =
            detectInstalledPlugins(pathSettings_, projectDirectory, resolvedTemplate, profileName);
        std::vector<StoredPlugin> stored =
            reconcileStoredPlugins(pathSettings_, projectDirectory, resolvedTemplate, profileName, detected);
        std::vector<ProfilePluginOrderItemRecord> orderRecords =
            syncPluginOrderItems(projectDirectory, profileName, stored);
        std::vector<PluginEntry> entries =
            buildEntries(projectDirectory, resolvedTemplate, stored, orderRecords, detected);
        const PluginEntry* movingEntry = findPluginOrderEntry(entries, orderItemId);
        if (movingEntry == nullptr)
        {
            throw std::invalid_argument("Plugin order item was not found.");
        }

        if (movingEntry->isLocked)
        {
            throw std::invalid_argument("This plugin is locked at the top of the load order.");
        }

        const int clampedTarget = clampExistingPluginOrderTarget(entries, *movingEntry, targetIndex);
        if (movingEntry->order == clampedTarget)
        {
            return entries;
        }

        orderRecords = InstanceMetadataStore::moveProfilePluginOrderItem(
            projectDirectory,
            profileName,
            storedPluginNames(stored),
            movingEntry->orderId,
            clampedTarget);
        entries = buildEntries(projectDirectory, resolvedTemplate, stored, orderRecords, detected);
        writeStoredPluginsIfChanged(
            pathSettings_,
            projectDirectory,
            resolvedTemplate,
            profileName,
            stored,
            storedPluginsFromEntries(entries));

        return entries;
    }

    std::vector<PluginEntry> PluginService::createPluginSeparator(
        const std::filesystem::path& projectDirectory,
        const BuildTemplate& resolvedTemplate,
        std::wstring_view profileName,
        std::wstring_view title,
        int targetIndex) const
    {
        if (projectDirectory.empty())
        {
            throw std::invalid_argument("Project directory is required.");
        }

        ensurePluginSystemSupported(resolvedTemplate);
        std::filesystem::create_directories(
            profileDirectory(pathSettings_, projectDirectory, resolvedTemplate, profileName));

        const std::map<std::wstring, DetectedPlugin> detected =
            detectInstalledPlugins(pathSettings_, projectDirectory, resolvedTemplate, profileName);
        std::vector<StoredPlugin> stored =
            reconcileStoredPlugins(pathSettings_, projectDirectory, resolvedTemplate, profileName, detected);
        std::vector<ProfilePluginOrderItemRecord> orderRecords =
            syncPluginOrderItems(projectDirectory, profileName, stored);
        std::vector<PluginEntry> entries =
            buildEntries(projectDirectory, resolvedTemplate, stored, orderRecords, detected);
        const int clampedTarget = clampPluginSeparatorInsertionTarget(entries, targetIndex);

        orderRecords = InstanceMetadataStore::createProfilePluginOrderSeparator(
            projectDirectory,
            profileName,
            storedPluginNames(stored),
            title,
            clampedTarget);
        entries = buildEntries(projectDirectory, resolvedTemplate, stored, orderRecords, detected);
        writeStoredPluginsIfChanged(
            pathSettings_,
            projectDirectory,
            resolvedTemplate,
            profileName,
            stored,
            storedPluginsFromEntries(entries));
        return entries;
    }

    std::vector<PluginEntry> PluginService::deletePluginSeparator(
        const std::filesystem::path& projectDirectory,
        const BuildTemplate& resolvedTemplate,
        std::wstring_view profileName,
        std::wstring_view separatorId) const
    {
        if (projectDirectory.empty())
        {
            throw std::invalid_argument("Project directory is required.");
        }

        ensurePluginSystemSupported(resolvedTemplate);
        std::filesystem::create_directories(
            profileDirectory(pathSettings_, projectDirectory, resolvedTemplate, profileName));

        const std::map<std::wstring, DetectedPlugin> detected =
            detectInstalledPlugins(pathSettings_, projectDirectory, resolvedTemplate, profileName);
        std::vector<StoredPlugin> stored =
            reconcileStoredPlugins(pathSettings_, projectDirectory, resolvedTemplate, profileName, detected);
        std::vector<ProfilePluginOrderItemRecord> orderRecords =
            InstanceMetadataStore::deleteProfilePluginOrderSeparator(
                projectDirectory,
                profileName,
                storedPluginNames(stored),
                separatorId);
        std::vector<PluginEntry> entries =
            buildEntries(projectDirectory, resolvedTemplate, stored, orderRecords, detected);
        writeStoredPluginsIfChanged(
            pathSettings_,
            projectDirectory,
            resolvedTemplate,
            profileName,
            stored,
            storedPluginsFromEntries(entries));
        return entries;
    }

    std::vector<PluginEntry> PluginService::setPluginEnabled(
        const std::filesystem::path& projectDirectory,
        const BuildTemplate& resolvedTemplate,
        std::wstring_view profileName,
        std::wstring_view pluginName,
        bool isEnabled) const
    {
        if (pluginName.empty())
        {
            throw std::invalid_argument("Plugin name is required.");
        }

        ensurePluginSystemSupported(resolvedTemplate);
        std::filesystem::create_directories(
            profileDirectory(pathSettings_, projectDirectory, resolvedTemplate, profileName));

        const std::map<std::wstring, DetectedPlugin> detected =
            detectInstalledPlugins(pathSettings_, projectDirectory, resolvedTemplate, profileName);
        std::vector<StoredPlugin> stored =
            reconcileStoredPlugins(pathSettings_, projectDirectory, resolvedTemplate, profileName, detected);
        const std::vector<StoredPlugin> previousStored = stored;
        const auto match = std::find_if(
            stored.begin(),
            stored.end(),
            [pluginName](const StoredPlugin& plugin)
            {
                return equalsIgnoreCase(plugin.name, pluginName);
            });
        if (match == stored.end())
        {
            throw std::invalid_argument("Plugin was not found.");
        }

        if (isBasePlugin(resolvedTemplate, match->name) && !isEnabled)
        {
            throw std::invalid_argument("Base Skyrim masters cannot be disabled.");
        }

        const auto detectedPlugin = detected.find(toLower(match->name));
        if (isEnabled && detectedPlugin != detected.end() && !detectedPlugin->second.sourceModEnabled)
        {
            throw std::invalid_argument("Enable the source mod before enabling this plugin.");
        }

        match->isEnabled = isBasePlugin(resolvedTemplate, match->name) ? true : isEnabled;

        std::vector<ProfilePluginOrderItemRecord> orderRecords =
            syncPluginOrderItems(projectDirectory, profileName, stored);
        std::vector<PluginEntry> entries =
            buildEntries(projectDirectory, resolvedTemplate, stored, orderRecords, detected);
        writeStoredPluginsIfChanged(
            pathSettings_,
            projectDirectory,
            resolvedTemplate,
            profileName,
            previousStored,
            storedPluginsFromEntries(entries));
        return entries;
    }

    bool PluginService::isInitialized() const noexcept
    {
        return initialized_;
    }
}
