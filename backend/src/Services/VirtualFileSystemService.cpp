#include "FluxoraCore/Services/VirtualFileSystemService.hpp"

#include "FluxoraCore/Services/BuildPathSettingsService.hpp"
#include "FluxoraCore/Services/ContentLayoutService.hpp"
#include "FluxoraCore/Services/Logger.hpp"
#include "FluxoraCore/Services/PathSafetyService.hpp"
#include "FluxoraCore/Services/ProfileOrderService.hpp"
#include "FluxoraCore/Support/JsonWriter.hpp"

#include <fstream>
#include <algorithm>
#include <cwctype>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

#ifdef FLUXORA_ENABLE_VFS
#include "FluxoraVfs/VfsProtocol.hpp"
#include <detours.h>
#endif

namespace fluxora
{
    namespace
    {
#ifdef _WIN32
        std::string toUtf8(const std::wstring& value)
        {
            if (value.empty())
            {
                return {};
            }

            const int size = WideCharToMultiByte(
                CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            std::string out(static_cast<std::size_t>(size), '\0');
            WideCharToMultiByte(
                CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), size, nullptr, nullptr);
            return out;
        }

        std::string toAnsi(const std::wstring& value)
        {
            if (value.empty())
            {
                return {};
            }

            const int size = WideCharToMultiByte(
                CP_ACP, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            std::string out(static_cast<std::size_t>(size), '\0');
            WideCharToMultiByte(
                CP_ACP, 0, value.data(), static_cast<int>(value.size()), out.data(), size, nullptr, nullptr);
            return out;
        }

        std::string pathSafetyErrorForLog(const PathSafetyResult& result)
        {
            const std::wstring message = result.message();
            return message.empty() ? std::string("unsafe path") : toUtf8(message);
        }

        std::string win32Message(DWORD value)
        {
            if (value == ERROR_SUCCESS)
            {
                return {};
            }

            LPWSTR raw = nullptr;
            const DWORD length = FormatMessageW(
                FORMAT_MESSAGE_ALLOCATE_BUFFER |
                    FORMAT_MESSAGE_FROM_SYSTEM |
                    FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr,
                value,
                0,
                reinterpret_cast<LPWSTR>(&raw),
                0,
                nullptr);
            if (length == 0 || raw == nullptr)
            {
                return {};
            }

            std::wstring message(raw, raw + length);
            LocalFree(raw);
            while (!message.empty() &&
                (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' '))
            {
                message.pop_back();
            }

            return toUtf8(message);
        }

        std::string describeWin32Error(DWORD value)
        {
            std::string description = std::to_string(value);
            if (const std::string message = win32Message(value); !message.empty())
            {
                description += " (" + message + ")";
            }

            return description;
        }
#else
        std::string toUtf8(const std::wstring& value)
        {
            return std::string(value.begin(), value.end());
        }
#endif

        struct VfsMountDescriptor
        {
            std::filesystem::path target;
            std::filesystem::path overwrite;
            std::vector<std::filesystem::path> mods;
            std::vector<std::wstring> excludedRootNames;
        };

        [[nodiscard]] std::string joinVfsList(const std::vector<std::wstring>& values)
        {
            std::string joined;
            for (const std::wstring& value : values)
            {
                if (!joined.empty())
                {
                    joined += "|";
                }
                joined += toUtf8(value);
            }

            return joined.empty() ? std::string("<none>") : joined;
        }

        [[nodiscard]] std::string vfsRulesSummary(const GameVfsRules* rules)
        {
            if (rules == nullptr)
            {
                return "<none>";
            }

            return "supportsRootBuilder=" + std::to_string(rules->supportsRootBuilder ? 1 : 0) +
                ";rootBuilderDirectory=" + toUtf8(rules->rootBuilderDirectoryName) +
                ";userSettingsDirectory=" + toUtf8(rules->userSettingsDirectoryName) +
                ";profileIniFiles=" + joinVfsList(rules->profileIniFileNames) +
                ";saveDirectories=" + joinVfsList(rules->saveDirectoryNames) +
                ";excludedLaunchCacheDirectories=" + joinVfsList(rules->excludedLaunchCacheDirectories);
        }

        std::wstring toLower(std::wstring value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
            return value;
        }

        bool equalsIgnoreCase(std::wstring_view left, std::wstring_view right)
        {
            return toLower(std::wstring(left)) == toLower(std::wstring(right));
        }

        bool isDirectory(const std::filesystem::path& path)
        {
            std::error_code error;
            return std::filesystem::exists(path, error) && std::filesystem::is_directory(path, error);
        }

        bool directoryHasEntries(const std::filesystem::path& path)
        {
            std::error_code error;
            if (!std::filesystem::exists(path, error) || !std::filesystem::is_directory(path, error))
            {
                return false;
            }

            std::filesystem::directory_iterator iterator(
                path,
                std::filesystem::directory_options::skip_permission_denied,
                error);
            return !error && iterator != std::filesystem::directory_iterator{};
        }

        std::filesystem::path childDirectoryByName(
            const std::filesystem::path& parent,
            std::wstring_view name)
        {
            if (parent.empty() || name.empty())
            {
                return {};
            }

            const std::filesystem::path direct = parent / std::filesystem::path(std::wstring(name));
            if (isDirectory(direct))
            {
                return direct;
            }

            std::error_code error;
            if (!std::filesystem::exists(parent, error) || !std::filesystem::is_directory(parent, error))
            {
                return {};
            }

            for (const std::filesystem::directory_entry& entry :
                 std::filesystem::directory_iterator(
                     parent,
                     std::filesystem::directory_options::skip_permission_denied,
                     error))
            {
                if (error)
                {
                    break;
                }

                if (entry.is_directory(error) &&
                    equalsIgnoreCase(entry.path().filename().wstring(), name))
                {
                    return entry.path();
                }
            }

            return {};
        }

        std::wstring normalizedPathForComparison(const std::filesystem::path& path)
        {
            std::wstring value = std::filesystem::absolute(path).lexically_normal().wstring();
            while (value.size() > 1 && (value.back() == L'\\' || value.back() == L'/'))
            {
                value.pop_back();
            }

            return toLower(value);
        }

        std::wstring normalizedRelativePathForComparison(const std::filesystem::path& path)
        {
            std::wstring value = path.lexically_normal().wstring();
            while (!value.empty() && (value.back() == L'\\' || value.back() == L'/'))
            {
                value.pop_back();
            }

            return toLower(value);
        }

        void appendUniqueDirectory(
            std::vector<std::filesystem::path>& directories,
            const std::filesystem::path& directory)
        {
            if (!directoryHasEntries(directory))
            {
                return;
            }

            const std::wstring normalized = normalizedPathForComparison(directory);
            const auto existing = std::find_if(
                directories.begin(),
                directories.end(),
                [&normalized](const std::filesystem::path& candidate)
                {
                    return normalizedPathForComparison(candidate) == normalized;
                });

            if (existing == directories.end())
            {
                directories.push_back(directory);
            }
        }

        void appendUniqueRelativePath(
            std::vector<std::filesystem::path>& paths,
            const std::filesystem::path& path)
        {
            if (path.empty())
            {
                return;
            }

            const std::wstring normalized = normalizedRelativePathForComparison(path);
            const auto existing = std::find_if(
                paths.begin(),
                paths.end(),
                [&normalized](const std::filesystem::path& candidate)
                {
                    return normalizedRelativePathForComparison(candidate) == normalized;
                });

            if (existing == paths.end())
            {
                paths.push_back(path);
            }
        }

        std::vector<std::filesystem::path> collectProfileSaveDirectories(
            const std::filesystem::path& profilesDirectory,
            const std::filesystem::path& profileDirectory,
            const std::vector<std::wstring>& saveDirectoryNames)
        {
            std::vector<std::filesystem::path> directories;
            for (const std::wstring& saveDirectoryName : saveDirectoryNames)
            {
                appendUniqueDirectory(directories, childDirectoryByName(profilesDirectory, saveDirectoryName));
                appendUniqueDirectory(directories, childDirectoryByName(profileDirectory, saveDirectoryName));
            }
            return directories;
        }

        std::filesystem::path profileSaveOverwriteDirectory(
            const std::filesystem::path& profilesDirectory,
            const std::filesystem::path& profileDirectory,
            const std::vector<std::wstring>& saveDirectoryNames)
        {
            for (const std::wstring& saveDirectoryName : saveDirectoryNames)
            {
                if (const std::filesystem::path profileSaves =
                        childDirectoryByName(profileDirectory, saveDirectoryName);
                    !profileSaves.empty())
                {
                    return profileSaves;
                }
            }

            if (isDirectory(profileDirectory) && !saveDirectoryNames.empty())
            {
                return profileDirectory / std::filesystem::path(saveDirectoryNames.front());
            }

            for (const std::wstring& saveDirectoryName : saveDirectoryNames)
            {
                if (const std::filesystem::path rootSaves =
                        childDirectoryByName(profilesDirectory, saveDirectoryName);
                    !rootSaves.empty())
                {
                    return rootSaves;
                }
            }

            if (isDirectory(profilesDirectory) && !saveDirectoryNames.empty())
            {
                return profilesDirectory / std::filesystem::path(saveDirectoryNames.front());
            }

            return {};
        }

        std::string trimAscii(std::string value)
        {
            const auto isSpace = [](unsigned char character)
            {
                return character == ' ' ||
                    character == '\t' ||
                    character == '\r' ||
                    character == '\n';
            };

            while (!value.empty() && isSpace(static_cast<unsigned char>(value.front())))
            {
                value.erase(value.begin());
            }

            while (!value.empty() && isSpace(static_cast<unsigned char>(value.back())))
            {
                value.pop_back();
            }

            return value;
        }

        std::string toLowerAscii(std::string value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](unsigned char character)
                {
                    return static_cast<char>(
                        character >= 'A' && character <= 'Z'
                            ? character - 'A' + 'a'
                            : character);
                });
            return value;
        }

        std::wstring asciiToWide(const std::string& value)
        {
            return std::wstring(value.begin(), value.end());
        }

        std::filesystem::path normalizeRelativeSettingsPath(std::wstring value)
        {
            while (!value.empty() && (value.front() == L'"' || value.front() == L'\''))
            {
                value.erase(value.begin());
            }
            while (!value.empty() && (value.back() == L'"' || value.back() == L'\''))
            {
                value.pop_back();
            }

            while (!value.empty() && (value.back() == L'\\' || value.back() == L'/'))
            {
                value.pop_back();
            }

            if (value.empty())
            {
                return {};
            }

            std::filesystem::path path(value);
            if (path.is_absolute() || !path.root_name().empty())
            {
                return {};
            }

            for (const std::filesystem::path& part : path)
            {
                if (part == L".." || part == L"." || part.empty())
                {
                    return {};
                }
            }

            path = path.lexically_normal();
            return path.empty() || path == L"." ? std::filesystem::path{} : path;
        }

        std::filesystem::path readLocalSavePathFromIni(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::in | std::ios::binary);
            if (!file)
            {
                return {};
            }

            std::filesystem::path localSavePath;
            std::string line;
            while (std::getline(file, line))
            {
                if (line.rfind("\xEF\xBB\xBF", 0) == 0)
                {
                    line.erase(0, 3);
                }

                const std::size_t comment = line.find_first_of(";#");
                if (comment != std::string::npos)
                {
                    line.erase(comment);
                }

                const std::size_t equals = line.find('=');
                if (equals == std::string::npos)
                {
                    continue;
                }

                const std::string key = toLowerAscii(trimAscii(line.substr(0, equals)));
                if (key != "slocalsavepath")
                {
                    continue;
                }

                const std::string value = trimAscii(line.substr(equals + 1));
                const std::filesystem::path parsed =
                    normalizeRelativeSettingsPath(asciiToWide(value));
                if (!parsed.empty())
                {
                    localSavePath = parsed;
                }
            }

            return localSavePath;
        }

        std::vector<std::filesystem::path> collectSaveTargets(
            const std::filesystem::path& profileDirectory,
            const std::vector<std::wstring>& saveDirectoryNames,
            const std::vector<std::wstring>& profileIniFileNames)
        {
            std::vector<std::filesystem::path> targets;
            for (const std::wstring& saveDirectoryName : saveDirectoryNames)
            {
                appendUniqueRelativePath(targets, std::filesystem::path(saveDirectoryName));
            }

            for (const std::wstring& fileName : profileIniFileNames)
            {
                appendUniqueRelativePath(
                    targets,
                    readLocalSavePathFromIni(profileDirectory / std::filesystem::path(fileName)));
            }

            return targets;
        }

        std::wstring safePathSegment(std::wstring value, std::wstring_view fallback)
        {
            static constexpr std::wstring_view invalid = L"<>:\"/\\|?*";
            for (wchar_t& character : value)
            {
                if (character < 32 || invalid.find(character) != std::wstring_view::npos)
                {
                    character = L'_';
                }
            }

            while (!value.empty() && (value.back() == L'.' || value.back() == L' '))
            {
                value.pop_back();
            }

            return value.empty() ? std::wstring(fallback) : value;
        }

#ifdef _WIN32
        std::wstring readEnvironmentVariable(const wchar_t* name)
        {
            const DWORD requiredLength = GetEnvironmentVariableW(name, nullptr, 0);
            if (requiredLength == 0)
            {
                return {};
            }

            std::wstring value(requiredLength, L'\0');
            const DWORD actualLength = GetEnvironmentVariableW(name, value.data(), requiredLength);
            if (actualLength == 0 || actualLength >= requiredLength)
            {
                return {};
            }

            value.resize(actualLength);
            return value;
        }

        std::filesystem::path shellFolderPath(int folder)
        {
            wchar_t buffer[MAX_PATH]{};
            if (SUCCEEDED(SHGetFolderPathW(nullptr, folder, nullptr, SHGFP_TYPE_CURRENT, buffer)) &&
                buffer[0] != L'\0')
            {
                return std::filesystem::path(buffer);
            }

            return {};
        }
#endif

        std::filesystem::path documentsDirectory()
        {
#ifdef _WIN32
            if (const std::filesystem::path folder = shellFolderPath(CSIDL_PERSONAL); !folder.empty())
            {
                return folder;
            }

            if (const std::wstring userProfile = readEnvironmentVariable(L"USERPROFILE"); !userProfile.empty())
            {
                return std::filesystem::path(userProfile) / L"Documents";
            }
#endif

            return {};
        }

        std::filesystem::path localAppDataDirectory()
        {
#ifdef _WIN32
            if (const std::wstring localAppData = readEnvironmentVariable(L"LOCALAPPDATA"); !localAppData.empty())
            {
                return std::filesystem::path(localAppData);
            }

            if (const std::filesystem::path folder = shellFolderPath(CSIDL_LOCAL_APPDATA); !folder.empty())
            {
                return folder;
            }

            if (const std::wstring userProfile = readEnvironmentVariable(L"USERPROFILE"); !userProfile.empty())
            {
                return std::filesystem::path(userProfile) / L"AppData" / L"Local";
            }
#endif

            return {};
        }

        void writeTextFile(const std::filesystem::path& path, const std::string& content)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }

            std::ofstream file(path, std::ios::out | std::ios::trunc | std::ios::binary);
            if (!file)
            {
                throw std::runtime_error("Failed to write the virtual file system descriptor.");
            }

            file.write(content.data(), static_cast<std::streamsize>(content.size()));
        }

        // The FluxoraVfs.dll hook ships next to FluxoraCore.dll, so it is located
        // relative to this very module rather than the (unknown) game folder.
        std::filesystem::path hookDllPath()
        {
#ifdef _WIN32
            HMODULE module = nullptr;
            if (!GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&writeTextFile),
                    &module))
            {
                return {};
            }

            wchar_t buffer[MAX_PATH * 2];
            const DWORD length = GetModuleFileNameW(module, buffer, static_cast<DWORD>(std::size(buffer)));
            if (length == 0 || length >= std::size(buffer))
            {
                return {};
            }

            return std::filesystem::path(std::wstring(buffer, length)).parent_path() / L"FluxoraVfs.dll";
#else
            return {};
#endif
        }

        std::vector<std::filesystem::path> collectEnabledMods(
            const ProfileOrderService& profileOrder,
            const std::filesystem::path& projectDirectory,
            std::wstring_view profileName)
        {
            std::vector<std::filesystem::path> mods;
            for (const ProfileModOrderItem& item : profileOrder.listModOrder(projectDirectory, profileName))
            {
                const bool isFullyOverwritten =
                    item.fileCount > 0 &&
                    item.overwrittenFileCount >= item.fileCount;
                if (item.kind == L"mod" && item.isEnabled && !item.id.empty() && !isFullyOverwritten)
                {
                    mods.push_back(item.id);
                }
            }

            return mods;
        }

        struct StaticContentLayoutRulesProvider final : IContentLayoutRulesProvider
        {
            explicit StaticContentLayoutRulesProvider(const ContentLayoutSupportRules& value) noexcept
                : rules(&value)
            {
            }

            [[nodiscard]] const ContentLayoutSupportRules& contentLayoutRules() const noexcept override
            {
                return *rules;
            }

            const ContentLayoutSupportRules* rules;
        };

        bool relativePathStartsWith(
            const std::filesystem::path& path,
            const std::filesystem::path& prefix)
        {
            if (path.empty() || prefix.empty())
            {
                return false;
            }

            auto pathIt = path.begin();
            auto prefixIt = prefix.begin();
            for (; prefixIt != prefix.end(); ++prefixIt, ++pathIt)
            {
                if (pathIt == path.end() || !equalsIgnoreCase(pathIt->wstring(), prefixIt->wstring()))
                {
                    return false;
                }
            }

            return true;
        }

        struct VfsContentPlacementRoots
        {
            bool dataAtModRoot{false};
            bool dataWrapper{false};
            bool rootBuilderData{false};
            bool rootBuilderRoot{false};
        };

        VfsContentPlacementRoots analyzeVfsContentPlacement(
            const std::filesystem::path& mod,
            const ContentLayoutAnalysisRequest& baseRequest,
            const std::wstring& dataDirectory,
            const std::wstring& rootBuilderDirectoryName,
            Logger& logger)
        {
            VfsContentPlacementRoots roots;
            if (!directoryHasEntries(mod))
            {
                return roots;
            }

            try
            {
                const PlacementPlan plan = ContentLayoutService().analyzeDirectory(mod, baseRequest);
                if (!plan.summary.supported || plan.summary.hasBlockers)
                {
                    return roots;
                }

                const std::filesystem::path dataPrefix(dataDirectory);
                const std::filesystem::path rootPrefix(rootBuilderDirectoryName);
                const std::filesystem::path rootDataPrefix =
                    rootBuilderDirectoryName.empty()
                        ? std::filesystem::path{}
                        : std::filesystem::path(rootBuilderDirectoryName) / dataDirectory;

                for (const PlacementPlanEntry& entry : plan.entries)
                {
                    const std::filesystem::path source = entry.sourcePath.path();
                    if (entry.target == PlacementTarget::Data)
                    {
                        if (!rootDataPrefix.empty() && relativePathStartsWith(source, rootDataPrefix))
                        {
                            roots.rootBuilderData = true;
                        }
                        else if (relativePathStartsWith(source, dataPrefix))
                        {
                            roots.dataWrapper = true;
                        }
                        else
                        {
                            roots.dataAtModRoot = true;
                        }
                    }
                    else if (entry.target == PlacementTarget::GameRoot &&
                        !rootPrefix.empty() &&
                        relativePathStartsWith(source, rootPrefix))
                    {
                        roots.rootBuilderRoot = true;
                    }
                }
            }
            catch (const std::exception& exception)
            {
                logger.write(
                    LogLevel::Warning,
                    "VFS content layout analysis skipped mod \"" + toUtf8(mod.wstring()) +
                        "\": " + exception.what());
            }

            return roots;
        }

        std::vector<std::filesystem::path> collectRootBuilderMods(
            const std::vector<std::filesystem::path>& mods,
            const std::vector<VfsContentPlacementRoots>& placements,
            const std::wstring& rootBuilderDirectoryName)
        {
            std::vector<std::filesystem::path> rootMods;
            rootMods.reserve(mods.size());
            for (std::size_t index = 0; index < mods.size(); ++index)
            {
                if (index >= placements.size() || !placements[index].rootBuilderRoot)
                {
                    continue;
                }

                const std::filesystem::path root = mods[index] / rootBuilderDirectoryName;
                if (isDirectory(root))
                {
                    rootMods.push_back(root);
                }
            }

            return rootMods;
        }

        std::vector<std::filesystem::path> collectDataMountMods(
            const std::vector<std::filesystem::path>& mods,
            const std::vector<VfsContentPlacementRoots>& placements,
            const std::wstring& dataDirectory,
            bool rootBuilderEnabled,
            const std::wstring& rootBuilderDirectoryName)
        {
            std::vector<std::filesystem::path> dataMods;
            dataMods.reserve(rootBuilderEnabled ? mods.size() * 2 : mods.size());
            for (std::size_t index = 0; index < mods.size(); ++index)
            {
                if (index >= placements.size())
                {
                    continue;
                }

                const std::filesystem::path& mod = mods[index];
                const VfsContentPlacementRoots& placement = placements[index];
                if (placement.dataAtModRoot)
                {
                    dataMods.push_back(mod);
                }

                const std::filesystem::path nestedData = mod / dataDirectory;
                if (placement.dataWrapper && isDirectory(nestedData))
                {
                    dataMods.push_back(nestedData);
                }

                if (!rootBuilderEnabled)
                {
                    continue;
                }

                const std::filesystem::path rootData =
                    mod / rootBuilderDirectoryName / dataDirectory;
                if (placement.rootBuilderData && isDirectory(rootData))
                {
                    dataMods.push_back(rootData);
                }
            }

            return dataMods;
        }

        std::vector<std::wstring> dataMountExcludedRootNames(
            const std::wstring& dataDirectory,
            bool rootBuilderEnabled,
            const std::wstring& rootBuilderDirectoryName)
        {
            std::vector<std::wstring> names;
            if (!dataDirectory.empty())
            {
                names.push_back(dataDirectory);
            }
            if (rootBuilderEnabled &&
                !rootBuilderDirectoryName.empty() &&
                !equalsIgnoreCase(dataDirectory, rootBuilderDirectoryName))
            {
                names.push_back(rootBuilderDirectoryName);
            }
            return names;
        }

        void appendProfileSettingsMount(
            std::vector<VfsMountDescriptor>& mounts,
            const std::filesystem::path& target,
            const std::filesystem::path& overwrite,
            const std::filesystem::path& profileDirectory)
        {
            if (target.empty())
            {
                return;
            }

            mounts.push_back(VfsMountDescriptor{
                target,
                overwrite,
                {profileDirectory},
                {}
            });
        }

        void appendProfileSavesMount(
            std::vector<VfsMountDescriptor>& mounts,
            const std::filesystem::path& settingsTarget,
            const std::filesystem::path& overwrite,
            const std::vector<std::filesystem::path>& saveTargets,
            const std::vector<std::filesystem::path>& saveDirectories)
        {
            if (settingsTarget.empty() || overwrite.empty())
            {
                return;
            }

            std::vector<std::filesystem::path> readDirectories;
            const std::wstring overwriteNormalized = normalizedPathForComparison(overwrite);
            for (const std::filesystem::path& saveDirectory : saveDirectories)
            {
                if (normalizedPathForComparison(saveDirectory) != overwriteNormalized)
                {
                    readDirectories.push_back(saveDirectory);
                }
            }

            for (const std::filesystem::path& saveTarget : saveTargets)
            {
                if (saveTarget.empty())
                {
                    continue;
                }

                mounts.push_back(VfsMountDescriptor{
                    settingsTarget / saveTarget,
                    overwrite,
                    readDirectories,
                    {}
                });
            }
        }

        void appendGameProfileSettingsMounts(
            std::vector<VfsMountDescriptor>& mounts,
            const GameVfsRules& rules,
            const std::filesystem::path& profilesDirectory,
            const std::filesystem::path& profileDirectory,
            const std::filesystem::path& profileOverwriteRoot,
            std::wstring_view profileName)
        {
            const std::wstring& settingsDirectoryName = rules.userSettingsDirectoryName;
            if (settingsDirectoryName.empty())
            {
                return;
            }

            const bool hasProfileEntries = directoryHasEntries(profileDirectory);
            const std::vector<std::filesystem::path> saveDirectories =
                collectProfileSaveDirectories(
                    profilesDirectory,
                    profileDirectory,
                    rules.saveDirectoryNames);
            const std::vector<std::filesystem::path> saveTargets =
                collectSaveTargets(
                    profileDirectory,
                    rules.saveDirectoryNames,
                    rules.profileIniFileNames);
            const std::filesystem::path saveOverwriteDirectory =
                profileSaveOverwriteDirectory(
                    profilesDirectory,
                    profileDirectory,
                    rules.saveDirectoryNames);
            if (!hasProfileEntries && saveDirectories.empty() && saveOverwriteDirectory.empty())
            {
                return;
            }

            const std::filesystem::path profileOverwrite =
                profileOverwriteRoot / safePathSegment(std::wstring(profileName), L"Default");
            const std::filesystem::path documents = documentsDirectory();
            if (!documents.empty())
            {
                const std::filesystem::path documentsTarget =
                    documents / L"My Games" / settingsDirectoryName;
                const std::filesystem::path documentsOverwrite = profileOverwrite / L"documents";

                if (hasProfileEntries)
                {
                    appendProfileSettingsMount(
                        mounts,
                        documentsTarget,
                        documentsOverwrite,
                        profileDirectory);
                }

                appendProfileSavesMount(
                    mounts,
                    documentsTarget,
                    saveOverwriteDirectory,
                    saveTargets,
                    saveDirectories);
            }

            const std::filesystem::path localAppData = localAppDataDirectory();
            if (!localAppData.empty() && hasProfileEntries)
            {
                appendProfileSettingsMount(
                    mounts,
                    localAppData / settingsDirectoryName,
                    profileOverwrite / L"local-appdata",
                    profileDirectory);
            }
        }

#ifdef FLUXORA_ENABLE_VFS
        void writePathArray(JsonWriter& writer, const std::vector<std::filesystem::path>& paths)
        {
            writer.beginArray();
            for (const std::filesystem::path& path : paths)
            {
                writer.value(path.wstring());
            }
            writer.endArray();
        }

        void writeMount(JsonWriter& writer, const VfsMountDescriptor& mount)
        {
            writer.beginObject();
            writer.field(vfs::protocol::fields::target, mount.target.wstring());
            writer.field(vfs::protocol::fields::overwrite, mount.overwrite.wstring());
            writer.key(vfs::protocol::fields::mods);
            writePathArray(writer, mount.mods);
            writer.stringArray(vfs::protocol::fields::excludedRootNames, mount.excludedRootNames);
            writer.endObject();
        }

        std::wstring buildDescriptor(
            const std::filesystem::path& logPath,
            const std::filesystem::path& hookDll,
            const std::vector<VfsMountDescriptor>& mounts)
        {
            JsonWriter writer;
            writer.beginObject();
            writer.field(vfs::protocol::fields::schemaVersion, vfs::protocol::schemaVersion);
            writer.field(vfs::protocol::fields::logPath, logPath.wstring());
            writer.field(vfs::protocol::fields::hookDll, hookDll.wstring());

            if (!mounts.empty())
            {
                writer.field(vfs::protocol::fields::target, mounts.front().target.wstring());
                writer.field(vfs::protocol::fields::overwrite, mounts.front().overwrite.wstring());
                writer.key(vfs::protocol::fields::mods);
                writePathArray(writer, mounts.front().mods);
            }

            writer.key(vfs::protocol::fields::mounts).beginArray();
            for (const VfsMountDescriptor& mount : mounts)
            {
                writeMount(writer, mount);
            }
            writer.endArray();
            writer.endObject();
            return writer.str();
        }
#endif
    }

    VirtualFileSystemService::VirtualFileSystemService(
        Logger& logger,
        ExecutableService& executables,
        ProfileOrderService& profileOrder,
        const BuildPathSettingsService& pathSettings) noexcept
        : logger_(logger),
          executables_(executables),
          profileOrder_(profileOrder),
          pathSettings_(pathSettings)
    {
    }

    void VirtualFileSystemService::initialize()
    {
        if (initialized_)
        {
            return;
        }

        initialized_ = true;
        logger_.write(LogLevel::Info, "Virtual file system service initialized.");
    }

    void VirtualFileSystemService::shutdown()
    {
        if (!initialized_)
        {
            return;
        }

        initialized_ = false;
        logger_.write(LogLevel::Info, "Virtual file system service shut down.");
    }

    GameExecutableLaunchResult VirtualFileSystemService::launchExecutable(
        const std::filesystem::path& configPath,
        std::wstring_view executableId) const
    {
        const ResolvedExecutableLaunch resolved = executables_.resolveExecutable(configPath, executableId);
        logger_.writeOperation(
            LogLevel::Info,
            "VfsDiagnostics",
            "vfsOperation requested selectedGameId=\"" + toUtf8(resolved.gameId.value()) +
                "\", definitionVersion=\"" + toUtf8(resolved.gameDefinitionVersion) +
                "\", executableId=\"" + toUtf8(std::wstring(executableId.begin(), executableId.end())) +
                "\", executablePath=\"" + toUtf8(resolved.resolvedExecutablePath.wstring()) + "\".");

#if !defined(FLUXORA_ENABLE_VFS) || !defined(_WIN32)
        // Built without the virtual file system: behave exactly like a plain run.
        logger_.writeOperation(
            LogLevel::Warning,
            "VfsDiagnostics",
            "vfsOperation fallback selectedGameId=\"" + toUtf8(resolved.gameId.value()) +
                "\", definitionVersion=\"" + toUtf8(resolved.gameDefinitionVersion) +
                "\", reason=\"Fluxora was built without VFS support for this platform.\".");
        return executables_.launchProjectExecutable(configPath, executableId);
#else
        const auto fallbackPlainLaunch = [&](const std::string& reason) -> GameExecutableLaunchResult
        {
            logger_.write(LogLevel::Warning, "Launching without the virtual file system: " + reason);
            logger_.writeOperation(
                LogLevel::Warning,
                "VfsDiagnostics",
                "vfsOperation fallback selectedGameId=\"" + toUtf8(resolved.gameId.value()) +
                    "\", definitionVersion=\"" + toUtf8(resolved.gameDefinitionVersion) +
                    "\", appliedVfsRules=\"" +
                    vfsRulesSummary(resolved.vfsRules.has_value() ? &resolved.vfsRules->rules : nullptr) +
                    "\", reason=\"" + reason + "\".");
            return executables_.launchProjectExecutable(configPath, executableId);
        };
        const auto failVfsLaunch = [&](const std::string& reason) -> GameExecutableLaunchResult
        {
            logger_.write(LogLevel::Error, "Virtual file system launch failed: " + reason);
            logger_.writeOperation(
                LogLevel::Error,
                "VfsDiagnostics",
                "vfsOperation failed selectedGameId=\"" + toUtf8(resolved.gameId.value()) +
                    "\", definitionVersion=\"" + toUtf8(resolved.gameDefinitionVersion) +
                    "\", appliedVfsRules=\"" +
                    vfsRulesSummary(resolved.vfsRules.has_value() ? &resolved.vfsRules->rules : nullptr) +
                    "\", unsupportedCapabilityError=\"" + reason + "\".");
            throw std::runtime_error("Virtual file system launch failed: " + reason);
        };

        if (resolved.gamePath.empty() || !std::filesystem::exists(resolved.gamePath))
        {
            return failVfsLaunch("the build has no valid game path.");
        }

        const GameVfsRules* rules = resolved.vfsRules.has_value()
            ? &resolved.vfsRules->rules
            : nullptr;
        const ContentLayoutSupportRules* contentRules = resolved.contentLayoutRules.has_value()
            ? &resolved.contentLayoutRules.value()
            : nullptr;
        if (!resolved.gameCapabilities.has(GameCapability::GameSpecificVfs) || rules == nullptr)
        {
            return failVfsLaunch("the selected game does not support virtual file system launches.");
        }
        if (!resolved.gameCapabilities.has(GameCapability::ContentLayoutRules) || contentRules == nullptr)
        {
            return failVfsLaunch("the selected game does not provide content layout rules for VFS placement.");
        }
        if (contentRules->dataFolder.empty())
        {
            return failVfsLaunch("the selected game content layout does not define a data directory.");
        }

        if ((!rules->userSettingsDirectoryName.empty() || !rules->profileIniFileNames.empty()) &&
            !resolved.gameCapabilities.has(GameCapability::IniProfiles))
        {
            return failVfsLaunch("the selected game defines profile INI mounts but the INI profile capability is disabled.");
        }
        if (!rules->saveDirectoryNames.empty() &&
            !resolved.gameCapabilities.has(GameCapability::SaveProfiles))
        {
            return failVfsLaunch("the selected game defines save mounts but the save profile capability is disabled.");
        }

        const std::wstring dataDirectory = contentRules->dataFolder;
        const std::wstring rootBuilderDirectoryName = rules->rootBuilderDirectoryName;
        const bool rootBuilderEnabled =
            rules->supportsRootBuilder &&
            !rootBuilderDirectoryName.empty() &&
            contentRules->supportsRootFiles &&
            resolved.gameCapabilities.has(GameCapability::RootFiles);
        logger_.writeOperation(
            LogLevel::Info,
            "VfsDiagnostics",
            "vfsOperation applyingRules selectedGameId=\"" + toUtf8(resolved.gameId.value()) +
                "\", definitionVersion=\"" + toUtf8(resolved.gameDefinitionVersion) +
                "\", appliedVfsRules=\"" + vfsRulesSummary(rules) +
                "\", rootBuilderEnabled=" + std::to_string(rootBuilderEnabled ? 1 : 0) +
                ", dataDirectory=\"" + toUtf8(dataDirectory) + "\".");
        if (rules->supportsRootBuilder && rootBuilderDirectoryName.empty())
        {
            return failVfsLaunch("the selected game enables Root Builder but does not define its root directory name.");
        }

        const std::filesystem::path dataTarget = resolved.gamePath / dataDirectory;

        std::wstring profile = resolved.defaultProfile.empty() ? L"Default" : resolved.defaultProfile;
        const std::vector<std::filesystem::path> mods =
            collectEnabledMods(profileOrder_, resolved.projectDirectory, profile);
        StaticContentLayoutRulesProvider contentProvider(*contentRules);
        ContentLayoutAnalysisRequest layoutRequest;
        layoutRequest.selectedGameId = resolved.gameId;
        layoutRequest.selectedGameDisplayName = resolved.gameDisplayName;
        layoutRequest.selectedGameCapabilities = resolved.gameCapabilities;
        layoutRequest.rulesProvider = &contentProvider;
        layoutRequest.logger = &logger_;
        std::vector<VfsContentPlacementRoots> placements;
        placements.reserve(mods.size());
        for (const std::filesystem::path& mod : mods)
        {
            placements.push_back(analyzeVfsContentPlacement(
                mod,
                layoutRequest,
                dataDirectory,
                rootBuilderDirectoryName,
                logger_));
        }

        const std::filesystem::path overwrite = pathSettings_.overwriteDirectory(resolved.projectDirectory);
        const std::filesystem::path vfsDirectory = resolved.projectDirectory / L".flow" / L"vfs";
        const std::vector<std::filesystem::path> dataMods =
            collectDataMountMods(
                mods,
                placements,
                dataDirectory,
                rootBuilderEnabled,
                rootBuilderDirectoryName);
        std::vector<VfsMountDescriptor> mounts;
        if (!dataMods.empty() || directoryHasEntries(overwrite))
        {
            mounts.push_back(VfsMountDescriptor{
                dataTarget,
                overwrite,
                dataMods,
                dataMountExcludedRootNames(dataDirectory, rootBuilderEnabled, rootBuilderDirectoryName)
            });
        }

        const std::vector<std::filesystem::path> rootMods = rootBuilderEnabled
            ? collectRootBuilderMods(mods, placements, rootBuilderDirectoryName)
            : std::vector<std::filesystem::path>{};
        const std::filesystem::path rootOverwrite = overwrite / rootBuilderDirectoryName;
        if (rootBuilderEnabled && (!rootMods.empty() || directoryHasEntries(rootOverwrite)))
        {
            mounts.push_back(VfsMountDescriptor{
                resolved.gamePath,
                rootOverwrite,
                rootMods,
                {dataDirectory}
            });
        }

        const std::filesystem::path launchCacheRoot = resolved.rootBuilderLaunchCacheDirectory;
        if (rootBuilderEnabled && !launchCacheRoot.empty())
        {
            const std::filesystem::path launchCacheDataTarget = launchCacheRoot / dataDirectory;
            std::vector<std::wstring> launchCacheDataExcluded =
                rules->excludedLaunchCacheDirectories;

            std::vector<std::filesystem::path> launchCacheDataMods;
            if (isDirectory(dataTarget))
            {
                launchCacheDataMods.push_back(dataTarget);
            }
            launchCacheDataMods.insert(
                launchCacheDataMods.end(),
                dataMods.begin(),
                dataMods.end());

            if (!launchCacheDataMods.empty() || isDirectory(overwrite))
            {
                mounts.push_back(VfsMountDescriptor{
                    launchCacheDataTarget,
                    overwrite,
                    launchCacheDataMods,
                    launchCacheDataExcluded
                });
            }

            mounts.push_back(VfsMountDescriptor{
                launchCacheRoot,
                rootOverwrite,
                {},
                std::vector<std::wstring>{dataDirectory}
            });
        }

        const std::filesystem::path profilesDirectory =
            pathSettings_.profilesDirectory(resolved.projectDirectory);
        const std::filesystem::path profileDirectory =
            profilesDirectory / std::filesystem::path(profile);
        if (rules != nullptr)
        {
            appendGameProfileSettingsMounts(
                mounts,
                *rules,
                profilesDirectory,
                profileDirectory,
                vfsDirectory / L"profile-overwrite",
                profile);
        }

        if (mounts.empty())
        {
            return fallbackPlainLaunch("no enabled mods or profile files to virtualize.");
        }

        logger_.writeOperation(
            LogLevel::Info,
            "VfsDiagnostics",
            "vfsOperation mountPlan selectedGameId=\"" + toUtf8(resolved.gameId.value()) +
                "\", definitionVersion=\"" + toUtf8(resolved.gameDefinitionVersion) +
                "\", mounts=" + std::to_string(mounts.size()) +
                ", dataMods=" + std::to_string(dataMods.size()) +
                ", rootMods=" + std::to_string(rootMods.size()) + ".");

        const std::filesystem::path hookDll = hookDllPath();
        if (hookDll.empty() || !std::filesystem::exists(hookDll))
        {
            return failVfsLaunch("FluxoraVfs.dll was not found next to FluxoraCore.dll.");
        }

        const std::filesystem::path descriptorPath = vfsDirectory / L"vfs-config.json";
        const std::filesystem::path logPath = vfsDirectory / L"vfs.log";
        const std::string descriptorContent = toUtf8(buildDescriptor(logPath, hookDll, mounts));

        const PathSafetyService pathSafety;
        const PathSafetyResult vfsDirectorySafety =
            pathSafety.validateWritePath(resolved.projectDirectory, vfsDirectory);
        if (!vfsDirectorySafety.safe())
        {
            return failVfsLaunch(
                "unsafe VFS directory " + toUtf8(vfsDirectory.wstring()) +
                " (" + pathSafetyErrorForLog(vfsDirectorySafety) + ").");
        }

        PathSafetyWriteOptions descriptorWriteOptions;
        descriptorWriteOptions.requiredBytes = descriptorContent.size();
        const PathSafetyResult descriptorSafety =
            pathSafety.validateWritePath(vfsDirectory, descriptorPath, descriptorWriteOptions);
        if (!descriptorSafety.safe())
        {
            return failVfsLaunch(
                "unsafe VFS descriptor path " + toUtf8(descriptorPath.wstring()) +
                " (" + pathSafetyErrorForLog(descriptorSafety) + ").");
        }

        const PathSafetyResult logPathSafety =
            pathSafety.validateWritePath(vfsDirectory, logPath);
        if (!logPathSafety.safe())
        {
            return failVfsLaunch(
                "unsafe VFS log path " + toUtf8(logPath.wstring()) +
                " (" + pathSafetyErrorForLog(logPathSafety) + ").");
        }

        std::error_code error;
        for (const VfsMountDescriptor& mount : mounts)
        {
            if (!mount.overwrite.empty())
            {
                const PathSafetyResult overwriteSafety =
                    pathSafety.validateDirectoryWriteRoot(mount.overwrite);
                if (!overwriteSafety.safe())
                {
                    return failVfsLaunch(
                        "unsafe VFS overwrite path " + toUtf8(mount.overwrite.wstring()) +
                        " (" + pathSafetyErrorForLog(overwriteSafety) + ").");
                }

                error.clear();
                std::filesystem::create_directories(mount.overwrite, error);
                if (error)
                {
                    return failVfsLaunch(
                        "could not create VFS overwrite path " + toUtf8(mount.overwrite.wstring()) +
                        " (" + describeWin32Error(static_cast<DWORD>(error.value())) + ").");
                }
            }
        }
        error.clear();
        std::filesystem::create_directories(vfsDirectory, error);
        if (error)
        {
            return failVfsLaunch(
                "could not create VFS directory " + toUtf8(vfsDirectory.wstring()) +
                " (" + describeWin32Error(static_cast<DWORD>(error.value())) + ").");
        }

        for (const VfsMountDescriptor& mount : mounts)
        {
            logger_.write(
                LogLevel::Info,
                "VFS mount prepared: target=\"" + toUtf8(mount.target.wstring()) +
                    "\", overwrite=\"" + toUtf8(mount.overwrite.wstring()) +
                    "\", mods=" + std::to_string(mount.mods.size()) +
                    ", excluded=" + std::to_string(mount.excludedRootNames.size()) + ".");
        }

        writeTextFile(
            descriptorPath,
            descriptorContent);

        // Children inherit this, so the whole process tree shares one virtual view.
        SetEnvironmentVariableW(vfs::protocol::configEnvironmentVariable, descriptorPath.c_str());

        std::vector<wchar_t> commandLineBuffer(resolved.commandLine.begin(), resolved.commandLine.end());
        commandLineBuffer.push_back(L'\0');
        const std::string hookDllAnsi = toAnsi(hookDll.wstring());

        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInformation{};

        const BOOL started = DetourCreateProcessWithDllExW(
            resolved.resolvedExecutablePath.c_str(),
            commandLineBuffer.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_DEFAULT_ERROR_MODE,
            nullptr,
            resolved.resolvedWorkingDirectory.c_str(),
            &startupInfo,
            &processInformation,
            hookDllAnsi.c_str(),
            nullptr);

        if (!started)
        {
            const DWORD launchError = GetLastError();
            return failVfsLaunch(
                ("the game could not be started with the hook injected. Win32 error: " +
                 describeWin32Error(launchError) + "."));
        }

        const DWORD processId = processInformation.dwProcessId;
        CloseHandle(processInformation.hThread);
        CloseHandle(processInformation.hProcess);

        logger_.write(
            LogLevel::Info,
            "Game launched through the virtual file system (" + std::to_string(mods.size()) +
                " active mods, " + std::to_string(mounts.size()) + " mounts).");
        logger_.writeOperation(
            LogLevel::Info,
            "VfsDiagnostics",
            "vfsOperation completed selectedGameId=\"" + toUtf8(resolved.gameId.value()) +
                "\", definitionVersion=\"" + toUtf8(resolved.gameDefinitionVersion) +
                "\", appliedVfsRules=\"" + vfsRulesSummary(rules) +
                "\", processId=" + std::to_string(static_cast<std::uint32_t>(processId)) +
                ", mounts=" + std::to_string(mounts.size()) +
                ", activeMods=" + std::to_string(mods.size()) + ".");

        return GameExecutableLaunchResult{
            resolved.executable,
            resolved.resolvedExecutablePath,
            resolved.resolvedWorkingDirectory,
            resolved.launchTrackingKind,
            resolved.expectedChildProcessNames,
            resolved.handoffDisplayName,
            resolved.handoffTimeoutMs,
            static_cast<std::uint32_t>(processId)
        };
#endif
    }

    bool VirtualFileSystemService::isInitialized() const noexcept
    {
        return initialized_;
    }
}
