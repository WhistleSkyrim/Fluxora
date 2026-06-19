#include "FluxoraCore/GameSupport/GameDefinitionLoader.hpp"

#include "FluxoraCore/GameSupport/EmbeddedGameDefinitions.hpp"
#include "FluxoraCore/Support/JsonReader.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace fluxora
{
    namespace
    {
        [[nodiscard]] std::string narrowContext(std::wstring_view context)
        {
            std::string result;
            result.reserve(context.size());
            for (const wchar_t character : context)
            {
                result.push_back(character <= 0x7f ? static_cast<char>(character) : '?');
            }

            return result;
        }

        [[nodiscard]] std::string pathContext(const std::filesystem::path& path)
        {
            return path.string();
        }

        template <typename T>
        [[nodiscard]] T requireParsed(GameTypeParseResult<T> result, std::wstring_view context)
        {
            if (!result)
            {
                throw std::runtime_error(narrowContext(context) + ": " + result.error().message);
            }

            return result.value();
        }

        [[nodiscard]] const JsonValue& requireObject(const JsonValue& value, std::wstring_view context)
        {
            if (!value.isObject())
            {
                throw std::runtime_error(narrowContext(context) + " must be a JSON object.");
            }

            return value;
        }

        void validateAllowedFields(
            const JsonValue& object,
            const std::set<std::wstring>& allowed,
            std::wstring_view context)
        {
            (void)requireObject(object, context);
            for (const auto& [key, ignored] : object.asObject())
            {
                (void)ignored;
                if (allowed.find(key) == allowed.end())
                {
                    throw std::runtime_error(
                        narrowContext(context) + " contains unsupported field '" + narrowContext(key) + "'.");
                }
            }
        }

        [[nodiscard]] const JsonValue& requireField(
            const JsonValue& object,
            std::wstring_view field,
            std::wstring_view context)
        {
            const JsonValue* value = object.find(field);
            if (value == nullptr || value->isNull())
            {
                throw std::runtime_error(
                    narrowContext(context) + " is missing required field '" + narrowContext(field) + "'.");
            }

            return *value;
        }

        [[nodiscard]] std::wstring readRequiredString(
            const JsonValue& object,
            std::wstring_view field,
            std::wstring_view context)
        {
            const JsonValue& value = requireField(object, field, context);
            if (!value.isString())
            {
                throw std::runtime_error(
                    narrowContext(context) + "." + narrowContext(field) + " must be a string.");
            }

            std::wstring text = trimAscii(value.asString());
            if (text.empty())
            {
                throw std::runtime_error(
                    narrowContext(context) + "." + narrowContext(field) + " cannot be empty.");
            }

            return text;
        }

        [[nodiscard]] std::wstring readOptionalString(
            const JsonValue& object,
            std::wstring_view field,
            std::wstring_view context)
        {
            const JsonValue* value = object.find(field);
            if (value == nullptr || value->isNull())
            {
                return {};
            }
            if (!value->isString())
            {
                throw std::runtime_error(
                    narrowContext(context) + "." + narrowContext(field) + " must be a string.");
            }

            return trimAscii(value->asString());
        }

        void validateGameRelativePath(std::wstring_view value, std::wstring_view context, bool allowEmpty)
        {
            const std::wstring trimmed = trimAscii(value);
            if (trimmed.empty())
            {
                if (allowEmpty)
                {
                    return;
                }

                throw std::runtime_error(narrowContext(context) + " cannot be empty.");
            }

            (void)requireParsed(
                GameRelativePath::parse(std::filesystem::path(trimmed)),
                context);
        }

        void validateOptionalGameRelativePath(std::wstring_view value, std::wstring_view context)
        {
            validateGameRelativePath(value, context, true);
        }

        void validateGameRelativePathArray(
            const std::vector<std::wstring>& values,
            std::wstring_view context)
        {
            for (const std::wstring& value : values)
            {
                validateGameRelativePath(value, context, false);
            }
        }

        [[nodiscard]] std::uint32_t readOptionalUInt32(
            const JsonValue& object,
            std::wstring_view field,
            std::wstring_view context,
            std::uint32_t fallback = 0)
        {
            const JsonValue* value = object.find(field);
            if (value == nullptr || value->isNull())
            {
                return fallback;
            }
            if (!value->isNumber())
            {
                throw std::runtime_error(
                    narrowContext(context) + "." + narrowContext(field) + " must be a number.");
            }

            const std::wstring text = trimAscii(value->asNumber());
            if (text.empty() || text.find_first_not_of(L"0123456789") != std::wstring::npos)
            {
                throw std::runtime_error(
                    narrowContext(context) + "." + narrowContext(field) + " must be a non-negative integer.");
            }

            unsigned long long parsed = 0;
            try
            {
                parsed = std::stoull(text);
            }
            catch (const std::exception&)
            {
                throw std::runtime_error(
                    narrowContext(context) + "." + narrowContext(field) + " is outside the supported range.");
            }
            if (parsed > std::numeric_limits<std::uint32_t>::max())
            {
                throw std::runtime_error(
                    narrowContext(context) + "." + narrowContext(field) + " is outside the supported range.");
            }

            return static_cast<std::uint32_t>(parsed);
        }

        [[nodiscard]] std::vector<std::wstring> readStringArray(
            const JsonValue& object,
            std::wstring_view field,
            bool required,
            std::wstring_view context)
        {
            const JsonValue* value = object.find(field);
            if (value == nullptr || value->isNull())
            {
                if (required)
                {
                    throw std::runtime_error(
                        narrowContext(context) + " is missing required field '" + narrowContext(field) + "'.");
                }

                return {};
            }
            if (!value->isArray())
            {
                throw std::runtime_error(
                    narrowContext(context) + "." + narrowContext(field) + " must be an array.");
            }

            const auto& array = value->asArray();
            std::vector<std::wstring> result;
            result.reserve(array.size());
            for (const JsonValue& item : array)
            {
                if (!item.isString())
                {
                    throw std::runtime_error(
                        narrowContext(context) + "." + narrowContext(field) + " items must be strings.");
                }

                std::wstring text = trimAscii(item.asString());
                if (text.empty())
                {
                    throw std::runtime_error(
                        narrowContext(context) + "." + narrowContext(field) + " cannot contain empty items.");
                }

                result.push_back(std::move(text));
            }

            return result;
        }

        [[nodiscard]] std::vector<NormalizedExtension> readExtensionArray(
            const JsonValue& object,
            std::wstring_view field)
        {
            std::vector<std::wstring> raw = readStringArray(object, field, true, L"definition");
            std::vector<NormalizedExtension> result;
            result.reserve(raw.size());
            std::set<std::wstring> seen;
            for (const std::wstring& item : raw)
            {
                NormalizedExtension extension = requireParsed(
                    NormalizedExtension::parse(item),
                    L"definition.extension");
                if (seen.insert(extension.value()).second)
                {
                    result.push_back(std::move(extension));
                }
            }

            return result;
        }

        [[nodiscard]] std::vector<ExecutableName> readExecutableNameArray(
            const JsonValue& object,
            std::wstring_view field,
            std::wstring_view context)
        {
            std::vector<std::wstring> raw = readStringArray(object, field, false, context);
            std::vector<ExecutableName> result;
            result.reserve(raw.size());
            for (const std::wstring& item : raw)
            {
                result.push_back(requireParsed(ExecutableName::parse(item), context));
            }

            return result;
        }

        [[nodiscard]] bool readCapabilityFlag(
            const JsonValue& capabilities,
            std::wstring_view field)
        {
            const JsonValue& value = requireField(capabilities, field, L"capabilities");
            if (value.type() != JsonValue::Type::Boolean)
            {
                throw std::runtime_error("capabilities." + narrowContext(field) + " must be a boolean.");
            }

            return value.asBoolean();
        }

        void enableIf(CapabilitySet& set, bool enabled, GameCapability capability)
        {
            if (enabled)
            {
                set.enable(capability);
            }
        }

        [[nodiscard]] CapabilitySet readCapabilities(const JsonValue& object)
        {
            const JsonValue& capabilities = requireObject(requireField(object, L"capabilities", L"definition"), L"capabilities");
            validateAllowedFields(
                capabilities,
                {
                    L"supportsPlugins",
                    L"supportsLoadOrder",
                    L"supportsRootFiles",
                    L"supportsArchives",
                    L"supportsScriptExtender",
                    L"supportsIniProfiles",
                    L"supportsSaveProfiles",
                    L"supportsGameSpecificVfs",
                    L"supportsContentLayoutRules",
                },
                L"capabilities");

            CapabilitySet set;
            enableIf(set, readCapabilityFlag(capabilities, L"supportsPlugins"), GameCapability::Plugins);
            enableIf(set, readCapabilityFlag(capabilities, L"supportsLoadOrder"), GameCapability::LoadOrder);
            enableIf(set, readCapabilityFlag(capabilities, L"supportsRootFiles"), GameCapability::RootFiles);
            enableIf(set, readCapabilityFlag(capabilities, L"supportsArchives"), GameCapability::Archives);
            enableIf(set, readCapabilityFlag(capabilities, L"supportsScriptExtender"), GameCapability::ScriptExtender);
            enableIf(set, readCapabilityFlag(capabilities, L"supportsIniProfiles"), GameCapability::IniProfiles);
            enableIf(set, readCapabilityFlag(capabilities, L"supportsSaveProfiles"), GameCapability::SaveProfiles);
            enableIf(set, readCapabilityFlag(capabilities, L"supportsGameSpecificVfs"), GameCapability::GameSpecificVfs);
            enableIf(set, readCapabilityFlag(capabilities, L"supportsContentLayoutRules"), GameCapability::ContentLayoutRules);
            return set;
        }

        [[nodiscard]] GameExecutableRole readExecutableRole(std::wstring_view value)
        {
            const std::wstring normalized = toAsciiLower(trimAscii(value));
            if (normalized == L"primary")
            {
                return GameExecutableRole::Primary;
            }
            if (normalized == L"launcher")
            {
                return GameExecutableRole::Launcher;
            }
            if (normalized == L"scriptextender" || normalized == L"script-extender")
            {
                return GameExecutableRole::ScriptExtender;
            }

            throw std::runtime_error("Executable role '" + narrowContext(value) + "' is unsupported.");
        }

        [[nodiscard]] std::vector<GameExecutableDefinition> readExecutables(const JsonValue& object)
        {
            const JsonValue& executables = requireField(object, L"executables", L"definition");
            if (!executables.isArray())
            {
                throw std::runtime_error("definition.executables must be an array.");
            }

            const auto& array = executables.asArray();
            std::vector<GameExecutableDefinition> result;
            result.reserve(array.size());
            for (const JsonValue& item : array)
            {
                validateAllowedFields(
                    item,
                    {L"id", L"displayName", L"name", L"role", L"workingDirectory"},
                    L"executable");

                std::optional<GameExecutableWorkingDirectoryKind> workingDirectory;
                if (const JsonValue* workingDirectoryValue = item.find(L"workingDirectory");
                    workingDirectoryValue != nullptr && !workingDirectoryValue->isNull())
                {
                    if (!workingDirectoryValue->isString())
                    {
                        throw std::runtime_error("executable.workingDirectory must be a string.");
                    }
                    workingDirectory = requireParsed(
                        parseGameExecutableWorkingDirectoryKind(workingDirectoryValue->asString()),
                        L"executable.workingDirectory");
                }

                result.push_back(GameExecutableDefinition{
                    readRequiredString(item, L"id", L"executable"),
                    readRequiredString(item, L"displayName", L"executable"),
                    requireParsed(
                        ExecutableName::parse(readRequiredString(item, L"name", L"executable")),
                        L"executable.name"),
                    readExecutableRole(readRequiredString(item, L"role", L"executable")),
                    workingDirectory
                });
            }

            return result;
        }

        [[nodiscard]] std::optional<ExecutableName> readOptionalExecutableName(
            const JsonValue& object,
            std::wstring_view field,
            std::wstring_view context)
        {
            const JsonValue* value = object.find(field);
            if (value == nullptr || value->isNull())
            {
                return std::nullopt;
            }
            if (!value->isString())
            {
                throw std::runtime_error(
                    narrowContext(context) + "." + narrowContext(field) + " must be a string.");
            }

            return requireParsed(ExecutableName::parse(value->asString()), context);
        }

        void validateExecutableDefinitions(const std::vector<GameExecutableDefinition>& executables)
        {
            if (executables.empty())
            {
                throw std::runtime_error("definition.executables cannot be empty.");
            }

            std::set<std::wstring> ids;
            std::set<std::wstring> names;
            for (const GameExecutableDefinition& executable : executables)
            {
                const std::wstring id = toAsciiLower(trimAscii(executable.id));
                if (id.empty())
                {
                    throw std::runtime_error("executable.id cannot be empty.");
                }
                if (!ids.insert(id).second)
                {
                    throw std::runtime_error("definition.executables contains a duplicate executable id.");
                }
                if (!names.insert(executable.name.normalizedName()).second)
                {
                    throw std::runtime_error("definition.executables contains a duplicate executable name.");
                }
            }
        }

        void validateExecutableRoleReferences(const GameDefinition& definition)
        {
            std::set<std::wstring> executableNames;
            for (const GameExecutableDefinition& executable : definition.executables)
            {
                executableNames.insert(executable.name.normalizedName());
            }

            auto requireKnownExecutable =
                [&executableNames](const std::optional<ExecutableName>& executable, const char* context)
                {
                    if (!executable.has_value())
                    {
                        return;
                    }
                    if (executableNames.find(executable->normalizedName()) == executableNames.end())
                    {
                        throw std::runtime_error(
                            std::string(context) + " must reference an executable declared in definition.executables.");
                    }
                };

            requireKnownExecutable(definition.executableRoles.primary, "executableRoles.primary");
            requireKnownExecutable(definition.executableRoles.launcher, "executableRoles.launcher");
            requireKnownExecutable(definition.executableRoles.scriptExtender, "executableRoles.scriptExtender");
        }

        [[nodiscard]] GameExecutableRoles readExecutableRoles(const JsonValue& object)
        {
            const JsonValue* roles = object.find(L"executableRoles");
            if (roles == nullptr || roles->isNull())
            {
                return {};
            }

            validateAllowedFields(*roles, {L"primary", L"launcher", L"scriptExtender"}, L"executableRoles");
            return GameExecutableRoles{
                readOptionalExecutableName(*roles, L"primary", L"executableRoles"),
                readOptionalExecutableName(*roles, L"launcher", L"executableRoles"),
                readOptionalExecutableName(*roles, L"scriptExtender", L"executableRoles")
            };
        }

        [[nodiscard]] GameDetectionHints readDetectionHints(const JsonValue& object)
        {
            const JsonValue* hints = object.find(L"detectionHints");
            if (hints == nullptr || hints->isNull())
            {
                return {};
            }

            validateAllowedFields(*hints, {L"executableNames", L"folderNames", L"domains"}, L"detectionHints");
            GameDetectionHints result{
                readExecutableNameArray(*hints, L"executableNames", L"detectionHints"),
                readStringArray(*hints, L"folderNames", false, L"detectionHints"),
                readStringArray(*hints, L"domains", false, L"detectionHints")
            };
            for (std::wstring& domain : result.domains)
            {
                domain = toAsciiLower(trimAscii(domain));
            }

            return result;
        }

        [[nodiscard]] GamePluginRules readPluginRules(const JsonValue& object)
        {
            const JsonValue* rules = object.find(L"pluginRules");
            if (rules == nullptr || rules->isNull())
            {
                return {};
            }

            validateAllowedFields(*rules, {L"profileFiles", L"basePlugins"}, L"pluginRules");
            return GamePluginRules{
                readStringArray(*rules, L"profileFiles", false, L"pluginRules"),
                readStringArray(*rules, L"basePlugins", false, L"pluginRules")
            };
        }

        [[nodiscard]] GameContentLayoutRules readContentLayoutRules(const JsonValue& object)
        {
            const JsonValue* rules = object.find(L"contentLayoutRules");
            if (rules == nullptr || rules->isNull())
            {
                return {};
            }

            validateAllowedFields(
                *rules,
                {L"dataFolder", L"supportsRootFiles", L"rootFileWrapperDirectory"},
                L"contentLayoutRules");
            GameContentLayoutRules result;
            result.dataFolder = readOptionalString(*rules, L"dataFolder", L"contentLayoutRules");
            if (const JsonValue* value = rules->find(L"supportsRootFiles"))
            {
                if (value->type() != JsonValue::Type::Boolean)
                {
                    throw std::runtime_error("contentLayoutRules.supportsRootFiles must be a boolean.");
                }

                result.supportsRootFiles = value->asBoolean();
            }
            result.rootFileWrapperDirectory =
                readOptionalString(*rules, L"rootFileWrapperDirectory", L"contentLayoutRules");

            return result;
        }

        [[nodiscard]] GameVfsRules readVfsRules(const JsonValue& object)
        {
            const JsonValue* rules = object.find(L"vfsRules");
            if (rules == nullptr || rules->isNull())
            {
                return {};
            }

            validateAllowedFields(
                *rules,
                {
                    L"supportsRootBuilder",
                    L"rootBuilderDirectoryName",
                    L"userSettingsDirectoryName",
                    L"profileIniFileNames",
                    L"saveDirectoryNames",
                    L"excludedLaunchCacheDirectories"
                },
                L"vfsRules");

            GameVfsRules result;
            if (const JsonValue* value = rules->find(L"supportsRootBuilder"))
            {
                if (value->type() != JsonValue::Type::Boolean)
                {
                    throw std::runtime_error("vfsRules.supportsRootBuilder must be a boolean.");
                }

                result.supportsRootBuilder = value->asBoolean();
            }
            result.rootBuilderDirectoryName =
                readOptionalString(*rules, L"rootBuilderDirectoryName", L"vfsRules");
            result.userSettingsDirectoryName =
                readOptionalString(*rules, L"userSettingsDirectoryName", L"vfsRules");
            result.profileIniFileNames =
                readStringArray(*rules, L"profileIniFileNames", false, L"vfsRules");
            result.saveDirectoryNames =
                readStringArray(*rules, L"saveDirectoryNames", false, L"vfsRules");
            result.excludedLaunchCacheDirectories =
                readStringArray(*rules, L"excludedLaunchCacheDirectories", false, L"vfsRules");
            return result;
        }

        [[nodiscard]] GameLaunchRules readLaunchRules(const JsonValue& object)
        {
            const JsonValue* launchRules = object.find(L"launchRules");
            if (launchRules == nullptr || launchRules->isNull())
            {
                return {};
            }

            validateAllowedFields(*launchRules, {L"scriptExtender"}, L"launchRules");
            const JsonValue* scriptExtender = launchRules->find(L"scriptExtender");
            if (scriptExtender == nullptr || scriptExtender->isNull())
            {
                return {};
            }

            validateAllowedFields(
                *scriptExtender,
                {
                    L"name",
                    L"loaderExecutable",
                    L"website",
                    L"expectedChildProcessNames",
                    L"handoffDisplayName",
                    L"handoffTimeoutMs",
                    L"launchTrackingKind"
                },
                L"launchRules.scriptExtender");

            LaunchTrackingKind trackingKind = LaunchTrackingKind::DirectProcess;
            if (const JsonValue* trackingValue = scriptExtender->find(L"launchTrackingKind");
                trackingValue != nullptr && !trackingValue->isNull())
            {
                if (!trackingValue->isString())
                {
                    throw std::runtime_error("launchRules.scriptExtender.launchTrackingKind must be a string.");
                }
                trackingKind = requireParsed(
                    parseLaunchTrackingKind(trackingValue->asString()),
                    L"launchRules.scriptExtender.launchTrackingKind");
            }

            return GameLaunchRules{
                GameScriptExtenderRules{
                    readRequiredString(*scriptExtender, L"name", L"launchRules.scriptExtender"),
                    requireParsed(
                        ExecutableName::parse(
                            readRequiredString(*scriptExtender, L"loaderExecutable", L"launchRules.scriptExtender")),
                        L"launchRules.scriptExtender.loaderExecutable"),
                    readOptionalString(*scriptExtender, L"website", L"launchRules.scriptExtender"),
                    readExecutableNameArray(
                        *scriptExtender,
                        L"expectedChildProcessNames",
                        L"launchRules.scriptExtender"),
                    readOptionalString(*scriptExtender, L"handoffDisplayName", L"launchRules.scriptExtender"),
                    readOptionalUInt32(*scriptExtender, L"handoffTimeoutMs", L"launchRules.scriptExtender"),
                    trackingKind
                }
            };
        }

        [[nodiscard]] GameHealthRules readHealthRules(const JsonValue& object)
        {
            const JsonValue* rules = object.find(L"healthRules");
            if (rules == nullptr || rules->isNull())
            {
                return {};
            }

            validateAllowedFields(*rules, {L"requiredFiles"}, L"healthRules");
            return GameHealthRules{
                readStringArray(*rules, L"requiredFiles", false, L"healthRules")
            };
        }

        void validatePathFields(const GameDefinition& definition)
        {
            validateGameRelativePathArray(definition.requiredFiles, L"definition.requiredFiles");
            validateOptionalGameRelativePath(definition.dataFolder, L"definition.dataFolder");
            validateGameRelativePathArray(definition.pluginRules.profileFiles, L"pluginRules.profileFiles");
            validateOptionalGameRelativePath(
                definition.contentLayoutRules.dataFolder,
                L"contentLayoutRules.dataFolder");
            validateOptionalGameRelativePath(
                definition.contentLayoutRules.rootFileWrapperDirectory,
                L"contentLayoutRules.rootFileWrapperDirectory");
            validateOptionalGameRelativePath(
                definition.vfsRules.rootBuilderDirectoryName,
                L"vfsRules.rootBuilderDirectoryName");
            validateOptionalGameRelativePath(
                definition.vfsRules.userSettingsDirectoryName,
                L"vfsRules.userSettingsDirectoryName");
            validateGameRelativePathArray(
                definition.vfsRules.profileIniFileNames,
                L"vfsRules.profileIniFileNames");
            validateGameRelativePathArray(
                definition.vfsRules.saveDirectoryNames,
                L"vfsRules.saveDirectoryNames");
            validateGameRelativePathArray(
                definition.vfsRules.excludedLaunchCacheDirectories,
                L"vfsRules.excludedLaunchCacheDirectories");
            validateGameRelativePathArray(definition.healthRules.requiredFiles, L"healthRules.requiredFiles");
        }

        void appendCodePoint(std::wstring& output, std::uint32_t codePoint)
        {
            if constexpr (sizeof(wchar_t) >= 4)
            {
                output.push_back(static_cast<wchar_t>(codePoint));
            }
            else
            {
                if (codePoint <= 0xffff)
                {
                    output.push_back(static_cast<wchar_t>(codePoint));
                    return;
                }

                codePoint -= 0x10000;
                output.push_back(static_cast<wchar_t>(0xd800 + (codePoint >> 10)));
                output.push_back(static_cast<wchar_t>(0xdc00 + (codePoint & 0x3ff)));
            }
        }

        [[nodiscard]] std::wstring decodeUtf8(std::string_view bytes)
        {
            std::wstring output;
            output.reserve(bytes.size());

            for (std::size_t index = 0; index < bytes.size();)
            {
                const unsigned char first = static_cast<unsigned char>(bytes[index++]);
                if (first <= 0x7f)
                {
                    output.push_back(static_cast<wchar_t>(first));
                    continue;
                }

                std::uint32_t codePoint = 0;
                std::size_t continuationCount = 0;
                if ((first & 0xe0) == 0xc0)
                {
                    codePoint = first & 0x1f;
                    continuationCount = 1;
                }
                else if ((first & 0xf0) == 0xe0)
                {
                    codePoint = first & 0x0f;
                    continuationCount = 2;
                }
                else if ((first & 0xf8) == 0xf0)
                {
                    codePoint = first & 0x07;
                    continuationCount = 3;
                }
                else
                {
                    throw std::runtime_error("Invalid UTF-8 in game definition file.");
                }

                if (index + continuationCount > bytes.size())
                {
                    throw std::runtime_error("Truncated UTF-8 in game definition file.");
                }

                for (std::size_t offset = 0; offset < continuationCount; ++offset)
                {
                    const unsigned char next = static_cast<unsigned char>(bytes[index++]);
                    if ((next & 0xc0) != 0x80)
                    {
                        throw std::runtime_error("Invalid UTF-8 continuation in game definition file.");
                    }

                    codePoint = (codePoint << 6) | (next & 0x3f);
                }

                appendCodePoint(output, codePoint);
            }

            return output;
        }

        [[nodiscard]] std::wstring readUtf8File(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
            {
                throw std::runtime_error("Could not open game definition override '" + pathContext(path) + "'.");
            }

            const std::string bytes{
                std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char>()};
            return decodeUtf8(bytes);
        }

        [[nodiscard]] bool isJsonFile(const std::filesystem::path& path)
        {
            return toAsciiLower(path.extension().wstring()) == L".json";
        }

        [[nodiscard]] std::vector<std::filesystem::path> findOverrideJsonFiles(
            const std::filesystem::path& overrideDirectory)
        {
            std::vector<std::filesystem::path> files;
            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(overrideDirectory))
            {
                if (entry.is_regular_file() && isJsonFile(entry.path()))
                {
                    files.push_back(entry.path());
                }
            }

            std::sort(files.begin(), files.end());
            return files;
        }

        [[nodiscard]] std::vector<GameDefinition> mergeOverrides(
            std::vector<GameDefinition> officialDefinitions,
            std::vector<GameDefinition> overrideDefinitions)
        {
            for (GameDefinition& overrideDefinition : overrideDefinitions)
            {
                const auto existing = std::find_if(
                    officialDefinitions.begin(),
                    officialDefinitions.end(),
                    [&overrideDefinition](const GameDefinition& definition)
                    {
                        return definition.id == overrideDefinition.id;
                    });

                if (existing == officialDefinitions.end())
                {
                    officialDefinitions.push_back(std::move(overrideDefinition));
                }
                else
                {
                    *existing = std::move(overrideDefinition);
                }
            }

            return officialDefinitions;
        }
    }

    std::vector<GameDefinition> GameDefinitionLoader::loadEmbeddedDefinitions()
    {
        static const std::vector<GameDefinition> cachedDefinitions = []
        {
            std::vector<std::wstring> documents;
            for (std::wstring_view document : embedded::gameDefinitionJsonDocuments)
            {
                documents.emplace_back(document);
            }

            return loadDefinitionsFromJsonStrings(documents);
        }();

        return cachedDefinitions;
    }

    GameDefinitionOverrideLoadResult GameDefinitionLoader::loadEmbeddedDefinitionsWithDevOverrides(
        const std::filesystem::path& overrideDirectory)
    {
        GameDefinitionOverrideLoadResult result;
        result.definitions = loadEmbeddedDefinitions();

        if (overrideDirectory.empty())
        {
            return result;
        }

        std::error_code error;
        if (!std::filesystem::exists(overrideDirectory, error))
        {
            result.diagnostics.push_back(
                "Dev game definition override directory does not exist: " + pathContext(overrideDirectory));
            return result;
        }
        if (!std::filesystem::is_directory(overrideDirectory, error))
        {
            result.diagnostics.push_back(
                "Dev game definition override path is not a directory: " + pathContext(overrideDirectory));
            return result;
        }

        try
        {
            const std::vector<std::filesystem::path> paths = findOverrideJsonFiles(overrideDirectory);
            if (paths.empty())
            {
                return result;
            }

            std::vector<std::wstring> overrideDocuments;
            overrideDocuments.reserve(paths.size());
            for (const std::filesystem::path& path : paths)
            {
                overrideDocuments.push_back(readUtf8File(path));
            }

            result.definitions = mergeOverrides(
                std::move(result.definitions),
                loadDefinitionsFromJsonStrings(overrideDocuments));
            result.overrideApplied = true;
        }
        catch (const std::exception& exception)
        {
            result.diagnostics.push_back(
                std::string("Dev game definition overrides were ignored: ") + exception.what());
        }

        return result;
    }

    std::vector<GameDefinition> GameDefinitionLoader::loadDefinitionsFromJsonStrings(
        const std::vector<std::wstring>& jsonDocuments)
    {
        std::vector<GameDefinition> definitions;
        definitions.reserve(jsonDocuments.size());
        std::set<GameId> ids;

        for (const std::wstring& json : jsonDocuments)
        {
            GameDefinition definition = loadDefinition(json);
            if (!ids.insert(definition.id).second)
            {
                throw std::runtime_error(
                    "Duplicate game definition id '" + narrowContext(definition.id.value()) + "'.");
            }

            definitions.push_back(std::move(definition));
        }

        return definitions;
    }

    GameDefinition GameDefinitionLoader::loadDefinition(std::wstring_view jsonText)
    {
        const JsonValue root = JsonReader::parse(jsonText);
        validateAllowedFields(
            root,
            {
                L"schemaVersion",
                L"definitionVersion",
                L"id",
                L"displayName",
                L"summary",
                L"aliases",
                L"domains",
                L"installFolderAliases",
                L"defaultProfileName",
                L"dataFolder",
                L"requiredFiles",
                L"executables",
                L"executableRoles",
                L"archiveExtensions",
                L"pluginExtensions",
                L"capabilities",
                L"uiTemplateId",
                L"detectionHints",
                L"pluginRules",
                L"contentLayoutRules",
                L"vfsRules",
                L"launchRules",
                L"healthRules",
            },
            L"definition");

        GameDefinition definition;
        definition.schemaVersion = readRequiredString(root, L"schemaVersion", L"definition");
        if (definition.schemaVersion != L"1")
        {
            throw std::runtime_error("Unsupported game definition schemaVersion '" +
                narrowContext(definition.schemaVersion) + "'.");
        }

        definition.definitionVersion = readRequiredString(root, L"definitionVersion", L"definition");
        definition.id = requireParsed(
            GameId::parse(readRequiredString(root, L"id", L"definition")),
            L"definition.id");
        definition.displayName = readRequiredString(root, L"displayName", L"definition");
        definition.summary = readOptionalString(root, L"summary", L"definition");
        definition.aliases = readStringArray(root, L"aliases", false, L"definition");
        definition.domains = readStringArray(root, L"domains", false, L"definition");
        for (std::wstring& domain : definition.domains)
        {
            domain = toAsciiLower(trimAscii(domain));
        }
        definition.installFolderAliases = readStringArray(root, L"installFolderAliases", false, L"definition");
        definition.defaultProfileName = readOptionalString(root, L"defaultProfileName", L"definition");
        definition.dataFolder = readOptionalString(root, L"dataFolder", L"definition");
        definition.requiredFiles = readStringArray(root, L"requiredFiles", true, L"definition");
        if (definition.requiredFiles.empty())
        {
            throw std::runtime_error("definition.requiredFiles cannot be empty.");
        }

        definition.executables = readExecutables(root);
        validateExecutableDefinitions(definition.executables);
        definition.executableRoles = readExecutableRoles(root);
        validateExecutableRoleReferences(definition);
        definition.archiveExtensions = readExtensionArray(root, L"archiveExtensions");
        definition.pluginExtensions = readExtensionArray(root, L"pluginExtensions");
        definition.capabilities = readCapabilities(root);
        definition.uiTemplateId = requireParsed(
            UiTemplateId::parse(readRequiredString(root, L"uiTemplateId", L"definition")),
            L"definition.uiTemplateId");
        definition.detectionHints = readDetectionHints(root);
        definition.pluginRules = readPluginRules(root);
        definition.contentLayoutRules = readContentLayoutRules(root);
        definition.vfsRules = readVfsRules(root);
        definition.launchRules = readLaunchRules(root);
        definition.healthRules = readHealthRules(root);
        if (definition.healthRules.requiredFiles.empty())
        {
            definition.healthRules.requiredFiles = definition.requiredFiles;
        }
        if (definition.healthRules.requiredFiles.empty())
        {
            throw std::runtime_error("healthRules.requiredFiles cannot be empty for blocking health checks.");
        }
        validatePathFields(definition);

        return definition;
    }
}
