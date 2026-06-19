#include "FluxoraCore/Services/PluginService.hpp"

#include "FluxoraCore/GameSupport/GameHealthCheckService.hpp"
#include "FluxoraCore/GameSupport/GameSupportRegistry.hpp"
#include "FluxoraCore/GameSupport/GameTypes.hpp"
#include "FluxoraCore/Services/BuildPathSettingsService.hpp"
#include "FluxoraCore/Services/Logger.hpp"
#include "FluxoraCore/Storage/AtomicFileStore.hpp"
#include "FluxoraCore/Storage/InstanceMetadataStore.hpp"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
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
        constexpr std::wstring_view pluginKind = L"plugin";
        constexpr std::wstring_view separatorKind = L"separator";

        struct DetectedPlugin
        {
            std::wstring name;
            NormalizedExtension extension;
            std::wstring sourceMod;
            bool sourceModEnabled{true};
        };

        struct StoredPlugin
        {
            std::wstring name;
            bool isEnabled{true};
        };

        struct ProfileModFolder
        {
            std::wstring folderName;
            bool isEnabled{true};
        };

        struct PluginScanCacheEntry
        {
            std::wstring fingerprint;
            std::map<std::wstring, DetectedPlugin> detected;
        };

        std::wstring toLower(std::wstring value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character)
            {
                return static_cast<wchar_t>(std::towlower(character));
            });
            return value;
        }

        void pushUniquePath(std::vector<std::filesystem::path>& values, std::filesystem::path value)
        {
            const std::wstring key = toLower(value.lexically_normal().generic_wstring());
            const auto match = std::find_if(
                values.begin(),
                values.end(),
                [&key](const std::filesystem::path& candidate)
                {
                    return toLower(candidate.lexically_normal().generic_wstring()) == key;
                });

            if (match == values.end())
            {
                values.push_back(std::move(value));
            }
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

        [[nodiscard]] std::wstring profileFileNamed(
            const std::vector<std::wstring>& profileFiles,
            std::wstring_view fileName)
        {
            const std::wstring requested = toLower(std::wstring(fileName));
            const auto match = std::find_if(
                profileFiles.begin(),
                profileFiles.end(),
                [&requested](const std::wstring& candidate)
                {
                    return toLower(std::filesystem::path(candidate).filename().wstring()) == requested;
                });

            return match == profileFiles.end() ? std::wstring() : *match;
        }

        [[nodiscard]] std::vector<NormalizedExtension> normalizedExtensions(
            const std::vector<std::wstring>& rawExtensions)
        {
            std::vector<NormalizedExtension> extensions;
            extensions.reserve(rawExtensions.size());
            for (const std::wstring& rawExtension : rawExtensions)
            {
                const GameTypeParseResult<NormalizedExtension> parsed = NormalizedExtension::parse(rawExtension);
                if (parsed && std::find(extensions.begin(), extensions.end(), parsed.value()) == extensions.end())
                {
                    extensions.push_back(parsed.value());
                }
            }

            return extensions;
        }

        [[nodiscard]] bool hasCapabilityId(const BuildTemplate& resolvedTemplate, std::wstring_view id)
        {
            return std::any_of(
                resolvedTemplate.capabilities.begin(),
                resolvedTemplate.capabilities.end(),
                [id](const TemplateCapability& capability)
                {
                    return equalsIgnoreCase(capability.id, id);
                });
        }

        [[nodiscard]] CapabilitySet capabilitiesFromTemplate(const BuildTemplate& resolvedTemplate)
        {
            CapabilitySet capabilities;
            if (hasCapabilityId(resolvedTemplate, L"plugins"))
            {
                capabilities.enable(GameCapability::Plugins);
            }
            if (hasCapabilityId(resolvedTemplate, L"load-order"))
            {
                capabilities.enable(GameCapability::LoadOrder);
            }
            return capabilities;
        }

        [[nodiscard]] PluginSupportRules rulesFromTemplate(const BuildTemplate& resolvedTemplate)
        {
            PluginSupportRules rules;
            rules.pluginExtensions = normalizedExtensions(resolvedTemplate.pluginExtensions);
            rules.profileFiles = resolvedTemplate.profileFiles;
            rules.basePlugins = resolvedTemplate.basePlugins;
            pushUniquePath(rules.pluginSearchDirectories, {});
            if (!resolvedTemplate.dataDirectory.empty())
            {
                pushUniquePath(rules.pluginSearchDirectories, std::filesystem::path(resolvedTemplate.dataDirectory));
            }
            rules.activePluginsFileName = profileFileNamed(resolvedTemplate.profileFiles, L"plugins.txt");
            rules.loadOrderFileName = profileFileNamed(resolvedTemplate.profileFiles, L"loadorder.txt");
            rules.basePluginSourceLabel = !resolvedTemplate.gameName.empty()
                ? resolvedTemplate.gameName
                : (!resolvedTemplate.displayName.empty() ? resolvedTemplate.displayName : std::wstring(L"Base game"));
            rules.basePluginLockReason = L"Базовый мастер игры: всегда сверху";

            for (const NormalizedExtension& extension : rules.pluginExtensions)
            {
                addExtensionKey(rules.pluginExtensionKeys, extension);
            }
            for (const NormalizedExtension& extension : rules.masterPluginExtensions)
            {
                addExtensionKey(rules.masterPluginExtensionKeys, extension);
            }
            for (const NormalizedExtension& extension : rules.lightPluginExtensions)
            {
                addExtensionKey(rules.lightPluginExtensionKeys, extension);
            }
            for (const std::wstring& basePlugin : rules.basePlugins)
            {
                const std::wstring key = toLower(basePlugin);
                if (!key.empty())
                {
                    rules.basePluginKeys.insert(key);
                }
            }
            for (const std::filesystem::path& directory : rules.pluginSearchDirectories)
            {
                addPathKey(rules.pluginSearchDirectoryKeys, directory);
            }

            return rules;
        }

        class CompatibilityPluginRulesProvider final : public IPluginRulesProvider
        {
        public:
            explicit CompatibilityPluginRulesProvider(const BuildTemplate& resolvedTemplate)
                : rules_(rulesFromTemplate(resolvedTemplate))
            {
            }

            [[nodiscard]] const PluginSupportRules& pluginRules() const noexcept override
            {
                return rules_;
            }

        private:
            PluginSupportRules rules_;
        };

        template <typename Callback>
        auto withTemplatePluginRules(const BuildTemplate& resolvedTemplate, Callback&& callback)
        {
            const GameSupportLookupResult lookup =
                GameSupportRegistry::embedded().lookupById(resolvedTemplate.id);
            if (lookup.supported && lookup.support != nullptr)
            {
                const GameSupportComponents& components = lookup.support->components();
                if (components.pluginRulesProvider != nullptr)
                {
                    const GameIdentityRules& identity = lookup.support->identity();
                    return callback(PluginRuleContext{
                        components.pluginRulesProvider,
                        &lookup.support->capabilities(),
                        nullptr,
                        identity.defaultProfileName.empty()
                            ? resolvedTemplate.defaultProfileName
                            : identity.defaultProfileName,
                        identity.id.value()
                    });
                }
            }

            const CompatibilityPluginRulesProvider rulesProvider(resolvedTemplate);
            const CapabilitySet capabilities = capabilitiesFromTemplate(resolvedTemplate);
            return callback(PluginRuleContext{
                &rulesProvider,
                &capabilities,
                nullptr,
                resolvedTemplate.defaultProfileName,
                resolvedTemplate.id
            });
        }

        GameTypeParseResult<NormalizedExtension> parsePathExtension(const std::filesystem::path& path)
        {
            return NormalizedExtension::parse(path.extension().wstring());
        }

        std::wstring extensionLabel(const NormalizedExtension& extension)
        {
            const std::wstring value = extension.value();
            return value.size() > 1 ? toUpper(value.substr(1)) : std::wstring();
        }

        std::wstring extensionLabel(const std::filesystem::path& path)
        {
            const GameTypeParseResult<NormalizedExtension> extension = parsePathExtension(path);
            if (!extension)
            {
                return {};
            }

            return extensionLabel(extension.value());
        }

        bool containsExtension(
            const std::vector<NormalizedExtension>& extensions,
            const NormalizedExtension& extension)
        {
            return std::any_of(
                extensions.begin(),
                extensions.end(),
                [&extension](const NormalizedExtension& candidate)
                {
                    return extension == candidate;
                });
        }

        bool containsExtension(
            const std::vector<NormalizedExtension>& extensions,
            const std::set<std::wstring>& extensionKeys,
            const NormalizedExtension& extension)
        {
            if (extension.value().empty())
            {
                return false;
            }
            if (!extensionKeys.empty())
            {
                return extensionKeys.contains(extension.value());
            }

            return containsExtension(extensions, extension);
        }

        bool hasPluginExtension(
            const std::filesystem::path& path,
            const PluginSupportRules& rules,
            NormalizedExtension& extension)
        {
            const GameTypeParseResult<NormalizedExtension> parsed = parsePathExtension(path);
            if (!parsed)
            {
                return false;
            }

            extension = parsed.value();
            return containsExtension(rules.pluginExtensions, rules.pluginExtensionKeys, extension);
        }

        std::wstring profileNameOrDefault(const PluginRuleContext& context, std::wstring_view profileName)
        {
            if (!profileName.empty())
            {
                return std::wstring(profileName);
            }

            return context.defaultProfileName.empty()
                ? std::wstring(fallbackProfileName)
                : context.defaultProfileName;
        }

        std::filesystem::path profileDirectory(
            const BuildPathSettingsService& pathSettings,
            const std::filesystem::path& projectDirectory,
            const PluginRuleContext& context,
            std::wstring_view profileName)
        {
            return pathSettings.profilesDirectory(projectDirectory) /
                std::filesystem::path(profileNameOrDefault(context, profileName));
        }

        std::filesystem::path pluginsFilePath(
            const BuildPathSettingsService& pathSettings,
            const std::filesystem::path& projectDirectory,
            const PluginRuleContext& context,
            std::wstring_view profileName)
        {
            const PluginSupportRules& rules = context.rulesProvider->pluginRules();
            return profileDirectory(pathSettings, projectDirectory, context, profileName) /
                std::filesystem::path(rules.activePluginsFileName);
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
                throw std::invalid_argument("Plugin state file is not valid UTF-8.");
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

        void recoverPluginStateFile(const std::filesystem::path& path)
        {
            static_cast<void>(AtomicFileStore().recoverFile(
                path,
                AtomicFileWriteOptions{
                    L"plugin list state",
                    ProjectStateValidation::Utf8Text
                }));
        }

        void writePluginStateFile(const std::filesystem::path& path, const std::string& content)
        {
            AtomicFileStore().writeTextFile(
                path,
                content,
                AtomicFileWriteOptions{
                    L"plugin list state",
                    ProjectStateValidation::Utf8Text
                });
        }

        bool isBasePlugin(const PluginSupportRules& rules, std::wstring_view pluginName)
        {
            if (!rules.basePluginKeys.empty())
            {
                return rules.basePluginKeys.contains(toLower(std::wstring(pluginName)));
            }

            return std::any_of(
                rules.basePlugins.begin(),
                rules.basePlugins.end(),
                [pluginName](const std::wstring& basePlugin)
                {
                    return equalsIgnoreCase(basePlugin, pluginName);
                });
        }

        bool isMasterPlugin(
            const PluginSupportRules& rules,
            std::wstring_view pluginName,
            const NormalizedExtension& extension)
        {
            return isBasePlugin(rules, pluginName) ||
                containsExtension(rules.masterPluginExtensions, rules.masterPluginExtensionKeys, extension);
        }

        std::vector<StoredPlugin> readStoredPlugins(const std::filesystem::path& path)
        {
            recoverPluginStateFile(path);
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

        [[nodiscard]] std::map<std::wstring, PluginScanCacheEntry>& pluginScanCache()
        {
            static std::map<std::wstring, PluginScanCacheEntry> cache;
            return cache;
        }

        [[nodiscard]] std::mutex& pluginScanCacheMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        [[nodiscard]] std::wstring pathFingerprint(const std::filesystem::path& path)
        {
            std::error_code error;
            const bool exists = std::filesystem::exists(path, error) && !error;
            if (!exists)
            {
                return L"missing";
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

            return L"mtime=" + std::to_wstring(timestampCount) +
                L";size=" + std::to_wstring(size) +
                L";kind=" + (regular ? std::wstring(L"file") : std::wstring(L"other"));
        }

        [[nodiscard]] std::wstring pluginRulesSignature(const PluginSupportRules& rules)
        {
            std::wstring signature;
            for (const NormalizedExtension& extension : rules.pluginExtensions)
            {
                signature.append(L"ext=");
                signature.append(extension.value());
                signature.push_back(L';');
            }
            for (const std::filesystem::path& directory : rules.pluginSearchDirectories)
            {
                signature.append(L"dir=");
                signature.append(normalizePathComparisonKey(directory, PathCaseSensitivity::CaseInsensitive));
                signature.push_back(L';');
            }

            return signature;
        }

        [[nodiscard]] std::string pluginRulesSummary(const PluginSupportRules& rules)
        {
            return "extensions=" + toUtf8(pluginRulesSignature(rules)) +
                ";activeFile=" + toUtf8(rules.activePluginsFileName) +
                ";loadOrderFile=" + toUtf8(rules.loadOrderFileName) +
                ";basePlugins=" + std::to_string(rules.basePlugins.size()) +
                ";searchDirectories=" + std::to_string(rules.pluginSearchDirectories.size());
        }

        void logPluginRuleFailure(
            Logger* logger,
            std::string_view operation,
            const PluginRuleContext& context,
            std::string_view reason)
        {
            if (logger == nullptr)
            {
                return;
            }

            logger->writeOperation(
                LogLevel::Error,
                "PluginDiagnostics",
                std::string(operation) +
                    " unsupportedCapabilityError=\"" + std::string(reason) + "\"" +
                    ", selectedGameId=\"" + toUtf8(context.gameId) + "\"" +
                    ", capabilities=" +
                    std::to_string(context.capabilities == nullptr ? 0 : context.capabilities->bits()) +
                    ", healthResult=\"" +
                    (context.health == nullptr
                        ? std::string("<unknown>")
                        : toUtf8(GameHealthCheckService::healthStatusName(context.health->status))) + "\".");
        }

        void logAppliedPluginRules(
            Logger& logger,
            std::string_view operation,
            const PluginRuleContext& context,
            std::wstring_view profileName)
        {
            if (context.rulesProvider == nullptr)
            {
                return;
            }

            const PluginSupportRules& rules = context.rulesProvider->pluginRules();
            logger.writeOperation(
                LogLevel::Info,
                "PluginDiagnostics",
                std::string(operation) +
                    " selectedGameId=\"" + toUtf8(context.gameId) + "\"" +
                    ", profile=\"" + toUtf8(profileNameOrDefault(context, profileName)) + "\"" +
                    ", appliedPluginRules=\"" + pluginRulesSummary(rules) + "\".");
        }

        [[nodiscard]] std::wstring pluginScanCacheKey(
            const std::filesystem::path& projectDirectory,
            std::wstring_view profileName,
            const PluginSupportRules& rules,
            const std::filesystem::path& modsDirectory)
        {
            return normalizePathComparisonKey(projectDirectory, PathCaseSensitivity::CaseInsensitive) +
                L"|profile=" + toLower(std::wstring(profileName)) +
                L"|mods=" + normalizePathComparisonKey(modsDirectory, PathCaseSensitivity::CaseInsensitive) +
                L"|rules=" + pluginRulesSignature(rules);
        }

        [[nodiscard]] std::wstring pluginScanFingerprint(
            const std::filesystem::path& modsDirectory,
            const PluginSupportRules& rules,
            const std::vector<ProfileModFolder>& profileModFolders)
        {
            std::wstring fingerprint;
            for (const ProfileModFolder& folder : profileModFolders)
            {
                fingerprint.append(L"mod=");
                fingerprint.append(toLower(folder.folderName));
                fingerprint.append(folder.isEnabled ? L":enabled;" : L":disabled;");

                const std::filesystem::path modDirectory =
                    modsDirectory / std::filesystem::path(folder.folderName);
                for (const std::filesystem::path& relativeSearchDirectory : rules.pluginSearchDirectories)
                {
                    const std::filesystem::path searchDirectory =
                        relativeSearchDirectory.empty() || relativeSearchDirectory == std::filesystem::path(L".")
                            ? modDirectory
                            : modDirectory / relativeSearchDirectory;
                    fingerprint.append(L"search=");
                    fingerprint.append(normalizePathComparisonKey(
                        searchDirectory,
                        PathCaseSensitivity::CaseInsensitive));
                    fingerprint.push_back(L':');
                    fingerprint.append(pathFingerprint(searchDirectory));
                    fingerprint.push_back(L';');

                    std::error_code error;
                    if (!std::filesystem::exists(searchDirectory, error) ||
                        !std::filesystem::is_directory(searchDirectory, error))
                    {
                        continue;
                    }

                    std::vector<std::wstring> entries;
                    for (const auto& entry : std::filesystem::directory_iterator(
                             searchDirectory,
                             std::filesystem::directory_options::skip_permission_denied,
                             error))
                    {
                        if (error)
                        {
                            break;
                        }

                        entries.push_back(
                            normalizePathComparisonKey(
                                entry.path().filename(),
                                PathCaseSensitivity::CaseInsensitive) +
                            L":" +
                            pathFingerprint(entry.path()));
                    }

                    std::sort(entries.begin(), entries.end());
                    for (const std::wstring& entry : entries)
                    {
                        fingerprint.append(entry);
                        fingerprint.push_back(L';');
                    }
                }
            }

            return fingerprint;
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
            const PluginRuleContext& context,
            std::wstring_view profileName)
        {
            const PluginSupportRules& rules = context.rulesProvider->pluginRules();
            std::map<std::wstring, DetectedPlugin> detected;
            const std::filesystem::path directory = pathSettings.modsDirectory(projectDirectory);
            if (!std::filesystem::exists(directory))
            {
                return detected;
            }

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

            const std::wstring cacheKey = pluginScanCacheKey(projectDirectory, profileName, rules, directory);
            const std::wstring fingerprint = pluginScanFingerprint(directory, rules, profileModFolders);
            {
                std::lock_guard<std::mutex> lock(pluginScanCacheMutex());
                const auto cached = pluginScanCache().find(cacheKey);
                if (cached != pluginScanCache().end() && cached->second.fingerprint == fingerprint)
                {
                    return cached->second.detected;
                }
            }

            const auto addPlugin = [&detected, &rules](
                const std::filesystem::directory_entry& entry,
                const std::wstring& sourceMod,
                bool sourceModEnabled)
            {
                NormalizedExtension extension;
                if (!entry.is_regular_file() ||
                    !hasPluginExtension(entry.path(), rules, extension))
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
                    extension,
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

                for (const std::filesystem::path& relativeSearchDirectory : rules.pluginSearchDirectories)
                {
                    const std::filesystem::path searchDirectory =
                        relativeSearchDirectory.empty() || relativeSearchDirectory == std::filesystem::path(L".")
                            ? modDirectory
                            : modDirectory / relativeSearchDirectory;
                    std::error_code searchError;
                    if (!std::filesystem::exists(searchDirectory, searchError) ||
                        !std::filesystem::is_directory(searchDirectory, searchError))
                    {
                        continue;
                    }

                    for (const auto& entry : std::filesystem::directory_iterator(
                             searchDirectory,
                             std::filesystem::directory_options::skip_permission_denied,
                             searchError))
                    {
                        if (searchError)
                        {
                            break;
                        }

                        addPlugin(entry, folder.folderName, folder.isEnabled);
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lock(pluginScanCacheMutex());
                pluginScanCache()[cacheKey] = PluginScanCacheEntry{fingerprint, detected};
                while (pluginScanCache().size() > 64)
                {
                    pluginScanCache().erase(pluginScanCache().begin());
                }
            }

            return detected;
        }

        std::vector<StoredPlugin> reconcileStoredPlugins(
            const BuildPathSettingsService& pathSettings,
            const std::filesystem::path& projectDirectory,
            const PluginRuleContext& context,
            std::wstring_view profileName,
            const std::map<std::wstring, DetectedPlugin>& detected)
        {
            const PluginSupportRules& rules = context.rulesProvider->pluginRules();
            const std::filesystem::path pluginsPath =
                pluginsFilePath(pathSettings, projectDirectory, context, profileName);
            std::vector<StoredPlugin> stored = readStoredPlugins(pluginsPath);
            const std::vector<StoredPlugin> loadOrder =
                rules.loadOrderFileName.empty()
                    ? std::vector<StoredPlugin>()
                    : readStoredPlugins(profileDirectory(pathSettings, projectDirectory, context, profileName) /
                        std::filesystem::path(rules.loadOrderFileName));

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

            for (const std::wstring& basePlugin : rules.basePlugins)
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
                if (included.contains(key) || isBasePlugin(rules, plugin.name))
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
                writePluginStateFile(pluginsPath, serializeStoredPlugins(reconciled));
            }

            return reconciled;
        }

        void writeStoredPlugins(
            const BuildPathSettingsService& pathSettings,
            const std::filesystem::path& projectDirectory,
            const PluginRuleContext& context,
            std::wstring_view profileName,
            const std::vector<StoredPlugin>& plugins)
        {
            writePluginStateFile(
                pluginsFilePath(pathSettings, projectDirectory, context, profileName),
                serializeStoredPlugins(plugins));
        }

        void writeStoredPluginsIfChanged(
            const BuildPathSettingsService& pathSettings,
            const std::filesystem::path& projectDirectory,
            const PluginRuleContext& context,
            std::wstring_view profileName,
            const std::vector<StoredPlugin>& currentPlugins,
            const std::vector<StoredPlugin>& desiredPlugins)
        {
            if (serializeStoredPlugins(currentPlugins) == serializeStoredPlugins(desiredPlugins))
            {
                return;
            }

            writeStoredPlugins(pathSettings, projectDirectory, context, profileName, desiredPlugins);
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
            const PluginRuleContext& context,
            const std::vector<StoredPlugin>& stored,
            const std::vector<ProfilePluginOrderItemRecord>& orderRecords,
            const std::map<std::wstring, DetectedPlugin>& detected)
        {
            (void)projectDirectory;
            const PluginSupportRules& rules = context.rulesProvider->pluginRules();
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
                const bool locked = isBasePlugin(rules, plugin.name);
                const NormalizedExtension extension = detectedPlugin == detected.end()
                    ? parsePathExtension(std::filesystem::path(plugin.name)).valueOrThrow()
                    : detectedPlugin->second.extension;
                const std::wstring extensionText = extensionLabel(extension);
                const bool enabled = locked || (
                    plugin.isEnabled &&
                    (detectedPlugin == detected.end() || detectedPlugin->second.sourceModEnabled));

                entries.push_back(PluginEntry{
                    record.id,
                    std::wstring(pluginKind),
                    static_cast<int>(entries.size()),
                    plugin.name,
                    extensionText,
                    locked ? (rules.basePluginSourceLabel.empty()
                        ? std::wstring(L"Base game")
                        : rules.basePluginSourceLabel) :
                        (detectedPlugin == detected.end() ? std::wstring() : detectedPlugin->second.sourceMod),
                    enabled,
                    isMasterPlugin(rules, plugin.name, extension),
                    containsExtension(rules.lightPluginExtensions, rules.lightPluginExtensionKeys, extension),
                    locked,
                    locked ? (rules.basePluginLockReason.empty()
                        ? std::wstring(L"Plugin is locked by the selected game's load-order rules.")
                        : rules.basePluginLockReason) : std::wstring(),
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

        void ensurePluginSystemSupported(
            const PluginRuleContext& context,
            bool requireLoadOrder,
            Logger* logger,
            std::string_view operation)
        {
            const auto fail = [&](const char* reason)
            {
                logPluginRuleFailure(logger, operation, context, reason);
                throw std::invalid_argument(reason);
            };

            if (context.health != nullptr && !context.health->allowsAutomation())
            {
                fail("Plugin management is not available because game health is blocking automation.");
            }

            if (context.capabilities == nullptr)
            {
                fail("Plugin management requires game capabilities.");
            }

            if (!context.capabilities->has(GameCapability::Plugins))
            {
                fail("Plugin management is not supported by the selected game.");
            }

            if (requireLoadOrder && !context.capabilities->has(GameCapability::LoadOrder))
            {
                fail("Plugin load-order management is not supported by the selected game.");
            }

            if (context.rulesProvider == nullptr)
            {
                fail("Plugin management requires plugin rules for the selected game.");
            }

            const PluginSupportRules& rules = context.rulesProvider->pluginRules();
            if (rules.pluginExtensions.empty())
            {
                fail("The selected game does not define plugin extensions.");
            }

            if (rules.activePluginsFileName.empty())
            {
                fail("The selected game does not define an active plugin list file.");
            }

            if (requireLoadOrder && rules.loadOrderFileName.empty())
            {
                fail("The selected game does not define a load-order file.");
            }

            if (rules.pluginSearchDirectories.empty())
            {
                fail("The selected game does not define plugin search locations.");
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
        return withTemplatePluginRules(
            resolvedTemplate,
            [this, &projectDirectory, profileName](const PluginRuleContext& rules)
            {
                return listPlugins(projectDirectory, rules, profileName);
            });
    }

    std::vector<PluginEntry> PluginService::listPlugins(
        const std::filesystem::path& projectDirectory,
        const PluginRuleContext& rules,
        std::wstring_view profileName) const
    {
        if (projectDirectory.empty())
        {
            throw std::invalid_argument("Project directory is required.");
        }

        ensurePluginSystemSupported(rules, false, &logger_, "listPlugins");
        logAppliedPluginRules(logger_, "listPlugins", rules, profileName);
        std::filesystem::create_directories(
            profileDirectory(pathSettings_, projectDirectory, rules, profileName));

        const std::map<std::wstring, DetectedPlugin> detected =
            detectInstalledPlugins(pathSettings_, projectDirectory, rules, profileName);
        std::vector<StoredPlugin> stored =
            reconcileStoredPlugins(pathSettings_, projectDirectory, rules, profileName, detected);
        std::vector<ProfilePluginOrderItemRecord> orderRecords =
            syncPluginOrderItems(projectDirectory, profileName, stored);
        std::vector<PluginEntry> entries =
            buildEntries(projectDirectory, rules, stored, orderRecords, detected);
        writeStoredPluginsIfChanged(
            pathSettings_,
            projectDirectory,
            rules,
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
        return withTemplatePluginRules(
            resolvedTemplate,
            [this, &projectDirectory, profileName, orderItemId, targetIndex](const PluginRuleContext& rules)
            {
                return movePlugin(projectDirectory, rules, profileName, orderItemId, targetIndex);
            });
    }

    std::vector<PluginEntry> PluginService::movePlugin(
        const std::filesystem::path& projectDirectory,
        const PluginRuleContext& rules,
        std::wstring_view profileName,
        std::wstring_view orderItemId,
        int targetIndex) const
    {
        if (orderItemId.empty())
        {
            throw std::invalid_argument("Plugin order item id is required.");
        }

        ensurePluginSystemSupported(rules, true, &logger_, "movePlugin");
        logAppliedPluginRules(logger_, "movePlugin", rules, profileName);
        std::filesystem::create_directories(
            profileDirectory(pathSettings_, projectDirectory, rules, profileName));

        const std::map<std::wstring, DetectedPlugin> detected =
            detectInstalledPlugins(pathSettings_, projectDirectory, rules, profileName);
        std::vector<StoredPlugin> stored =
            reconcileStoredPlugins(pathSettings_, projectDirectory, rules, profileName, detected);
        std::vector<ProfilePluginOrderItemRecord> orderRecords =
            syncPluginOrderItems(projectDirectory, profileName, stored);
        std::vector<PluginEntry> entries =
            buildEntries(projectDirectory, rules, stored, orderRecords, detected);
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
        entries = buildEntries(projectDirectory, rules, stored, orderRecords, detected);
        writeStoredPluginsIfChanged(
            pathSettings_,
            projectDirectory,
            rules,
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
        return withTemplatePluginRules(
            resolvedTemplate,
            [this, &projectDirectory, profileName, title, targetIndex](const PluginRuleContext& rules)
            {
                return createPluginSeparator(projectDirectory, rules, profileName, title, targetIndex);
            });
    }

    std::vector<PluginEntry> PluginService::createPluginSeparator(
        const std::filesystem::path& projectDirectory,
        const PluginRuleContext& rules,
        std::wstring_view profileName,
        std::wstring_view title,
        int targetIndex) const
    {
        if (projectDirectory.empty())
        {
            throw std::invalid_argument("Project directory is required.");
        }

        ensurePluginSystemSupported(rules, true, &logger_, "createPluginSeparator");
        logAppliedPluginRules(logger_, "createPluginSeparator", rules, profileName);
        std::filesystem::create_directories(
            profileDirectory(pathSettings_, projectDirectory, rules, profileName));

        const std::map<std::wstring, DetectedPlugin> detected =
            detectInstalledPlugins(pathSettings_, projectDirectory, rules, profileName);
        std::vector<StoredPlugin> stored =
            reconcileStoredPlugins(pathSettings_, projectDirectory, rules, profileName, detected);
        std::vector<ProfilePluginOrderItemRecord> orderRecords =
            syncPluginOrderItems(projectDirectory, profileName, stored);
        std::vector<PluginEntry> entries =
            buildEntries(projectDirectory, rules, stored, orderRecords, detected);
        const int clampedTarget = clampPluginSeparatorInsertionTarget(entries, targetIndex);

        orderRecords = InstanceMetadataStore::createProfilePluginOrderSeparator(
            projectDirectory,
            profileName,
            storedPluginNames(stored),
            title,
            clampedTarget);
        entries = buildEntries(projectDirectory, rules, stored, orderRecords, detected);
        writeStoredPluginsIfChanged(
            pathSettings_,
            projectDirectory,
            rules,
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
        return withTemplatePluginRules(
            resolvedTemplate,
            [this, &projectDirectory, profileName, separatorId](const PluginRuleContext& rules)
            {
                return deletePluginSeparator(projectDirectory, rules, profileName, separatorId);
            });
    }

    std::vector<PluginEntry> PluginService::deletePluginSeparator(
        const std::filesystem::path& projectDirectory,
        const PluginRuleContext& rules,
        std::wstring_view profileName,
        std::wstring_view separatorId) const
    {
        if (projectDirectory.empty())
        {
            throw std::invalid_argument("Project directory is required.");
        }

        ensurePluginSystemSupported(rules, true, &logger_, "deletePluginSeparator");
        logAppliedPluginRules(logger_, "deletePluginSeparator", rules, profileName);
        std::filesystem::create_directories(
            profileDirectory(pathSettings_, projectDirectory, rules, profileName));

        const std::map<std::wstring, DetectedPlugin> detected =
            detectInstalledPlugins(pathSettings_, projectDirectory, rules, profileName);
        std::vector<StoredPlugin> stored =
            reconcileStoredPlugins(pathSettings_, projectDirectory, rules, profileName, detected);
        std::vector<ProfilePluginOrderItemRecord> orderRecords =
            InstanceMetadataStore::deleteProfilePluginOrderSeparator(
                projectDirectory,
                profileName,
                storedPluginNames(stored),
                separatorId);
        std::vector<PluginEntry> entries =
            buildEntries(projectDirectory, rules, stored, orderRecords, detected);
        writeStoredPluginsIfChanged(
            pathSettings_,
            projectDirectory,
            rules,
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
        return withTemplatePluginRules(
            resolvedTemplate,
            [this, &projectDirectory, profileName, pluginName, isEnabled](const PluginRuleContext& rules)
            {
                return setPluginEnabled(projectDirectory, rules, profileName, pluginName, isEnabled);
            });
    }

    std::vector<PluginEntry> PluginService::setPluginEnabled(
        const std::filesystem::path& projectDirectory,
        const PluginRuleContext& rules,
        std::wstring_view profileName,
        std::wstring_view pluginName,
        bool isEnabled) const
    {
        if (pluginName.empty())
        {
            throw std::invalid_argument("Plugin name is required.");
        }

        ensurePluginSystemSupported(rules, false, &logger_, "setPluginEnabled");
        logAppliedPluginRules(logger_, "setPluginEnabled", rules, profileName);
        const PluginSupportRules& pluginRules = rules.rulesProvider->pluginRules();
        std::filesystem::create_directories(
            profileDirectory(pathSettings_, projectDirectory, rules, profileName));

        const std::map<std::wstring, DetectedPlugin> detected =
            detectInstalledPlugins(pathSettings_, projectDirectory, rules, profileName);
        std::vector<StoredPlugin> stored =
            reconcileStoredPlugins(pathSettings_, projectDirectory, rules, profileName, detected);
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

        if (isBasePlugin(pluginRules, match->name) && !isEnabled)
        {
            throw std::invalid_argument("Base game masters cannot be disabled.");
        }

        const auto detectedPlugin = detected.find(toLower(match->name));
        if (isEnabled && detectedPlugin != detected.end() && !detectedPlugin->second.sourceModEnabled)
        {
            throw std::invalid_argument("Enable the source mod before enabling this plugin.");
        }

        match->isEnabled = isBasePlugin(pluginRules, match->name) ? true : isEnabled;

        std::vector<ProfilePluginOrderItemRecord> orderRecords =
            syncPluginOrderItems(projectDirectory, profileName, stored);
        std::vector<PluginEntry> entries =
            buildEntries(projectDirectory, rules, stored, orderRecords, detected);
        writeStoredPluginsIfChanged(
            pathSettings_,
            projectDirectory,
            rules,
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
