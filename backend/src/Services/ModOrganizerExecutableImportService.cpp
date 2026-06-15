#include "FluxoraCore/Services/ModOrganizerExecutableImportService.hpp"

#include <algorithm>
#include <array>
#include <cwctype>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fluxora
{
    namespace
    {
        constexpr std::wstring_view customExecutablesPrefix = L"customexecutables.";

        struct RawExecutableEntry
        {
            int index{0};
            std::wstring title;
            std::wstring binary;
            std::wstring arguments;
            std::wstring workingDirectory;
        };

        struct PathMapping
        {
            std::filesystem::path sourceRoot;
            std::filesystem::path targetRoot;
            std::filesystem::path targetRelativeRoot;
            bool alreadyCopied{false};
        };

        struct MappedPath
        {
            std::wstring text;
            std::filesystem::path sourcePath;
            std::filesystem::path targetPath;
            bool isLocalProjectPath{false};
            bool requiresCopy{false};
            bool copySingleFile{false};
        };

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

        std::wstring toLower(std::wstring value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character)
            {
                return static_cast<wchar_t>(std::towlower(character));
            });
            return value;
        }

        bool equalsIgnoreCase(std::wstring_view left, std::wstring_view right)
        {
            return toLower(std::wstring(left)) == toLower(std::wstring(right));
        }

        bool startsWith(std::wstring_view value, std::wstring_view prefix)
        {
            return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
        }

        bool startsWithIgnoreCase(std::wstring_view value, std::wstring_view prefix)
        {
            if (value.size() < prefix.size())
            {
                return false;
            }

            return toLower(std::wstring(value.substr(0, prefix.size()))) ==
                toLower(std::wstring(prefix));
        }

        bool endsWithIgnoreCase(std::wstring_view value, std::wstring_view suffix)
        {
            if (value.size() < suffix.size())
            {
                return false;
            }

            return toLower(std::wstring(value.substr(value.size() - suffix.size()))) ==
                toLower(std::wstring(suffix));
        }

        bool containsIgnoreCase(std::wstring_view value, std::wstring_view token)
        {
            return toLower(std::wstring(value)).find(toLower(std::wstring(token))) != std::wstring::npos;
        }

        std::wstring stripQuotes(std::wstring value)
        {
            value = trim(std::move(value));
            if (value.size() >= 2 &&
                ((value.front() == L'"' && value.back() == L'"') ||
                 (value.front() == L'\'' && value.back() == L'\'')))
            {
                return value.substr(1, value.size() - 2);
            }

            return value;
        }

        void replaceAll(std::wstring& value, std::wstring_view token, std::wstring_view replacement)
        {
            if (token.empty())
            {
                return;
            }

            std::size_t position = 0;
            while ((position = value.find(token, position)) != std::wstring::npos)
            {
                value.replace(position, token.size(), replacement);
                position += replacement.size();
            }
        }

        void replaceAllIgnoreCase(
            std::wstring& value,
            std::wstring_view token,
            const std::wstring& replacement)
        {
            if (token.empty())
            {
                return;
            }

            std::wstring lowerValue = toLower(value);
            const std::wstring lowerToken = toLower(std::wstring(token));
            const std::wstring lowerReplacement = toLower(replacement);

            std::size_t position = 0;
            while ((position = lowerValue.find(lowerToken, position)) != std::wstring::npos)
            {
                value.replace(position, token.size(), replacement);
                lowerValue.replace(position, token.size(), lowerReplacement);
                position += replacement.size();
            }
        }

        std::wstring decodeQtIniValue(std::wstring value)
        {
            value = stripQuotes(std::move(value));
            if (startsWithIgnoreCase(value, L"@ByteArray(") && value.size() >= 12 && value.back() == L')')
            {
                value = value.substr(11, value.size() - 12);
                replaceAll(value, L"\\\\", L"\\");
            }

            return value;
        }

        std::wstring expandEnvironmentVariables(std::wstring value)
        {
#ifdef _WIN32
            const DWORD requiredLength = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
            if (requiredLength == 0)
            {
                return value;
            }

            std::wstring expanded(requiredLength, L'\0');
            const DWORD actualLength =
                ExpandEnvironmentStringsW(value.c_str(), expanded.data(), requiredLength);
            if (actualLength == 0 || actualLength > requiredLength)
            {
                return value;
            }

            expanded.resize(actualLength == 0 ? 0 : actualLength - 1);
            return expanded;
#else
            return value;
#endif
        }

        std::wstring shortPathText(const std::filesystem::path& path)
        {
#ifdef _WIN32
            if (path.empty())
            {
                return {};
            }

            const std::wstring native = path.wstring();
            const DWORD requiredLength = GetShortPathNameW(native.c_str(), nullptr, 0);
            if (requiredLength == 0)
            {
                return {};
            }

            std::wstring shortened(requiredLength, L'\0');
            const DWORD actualLength =
                GetShortPathNameW(native.c_str(), shortened.data(), requiredLength);
            if (actualLength == 0 || actualLength >= requiredLength)
            {
                return {};
            }

            shortened.resize(actualLength);
            return shortened;
#else
            (void)path;
            return {};
#endif
        }

        std::filesystem::path canonicalOrAbsolute(const std::filesystem::path& path)
        {
            if (path.empty())
            {
                return {};
            }

            std::error_code error;
            std::filesystem::path resolved = std::filesystem::weakly_canonical(path, error);
            if (!error)
            {
                return resolved;
            }

            error.clear();
            resolved = std::filesystem::absolute(path, error);
            return error ? path.lexically_normal() : resolved.lexically_normal();
        }

        std::wstring comparablePathText(const std::filesystem::path& path)
        {
            std::wstring value = canonicalOrAbsolute(path).wstring();
            std::replace(value.begin(), value.end(), L'/', L'\\');
            while (value.size() > 1 && (value.back() == L'\\' || value.back() == L'/'))
            {
                value.pop_back();
            }

            return toLower(std::move(value));
        }

        bool areSamePath(const std::filesystem::path& left, const std::filesystem::path& right)
        {
            return comparablePathText(left) == comparablePathText(right);
        }

        bool tryRelativePath(
            const std::filesystem::path& child,
            const std::filesystem::path& root,
            std::filesystem::path& relative)
        {
            if (child.empty() || root.empty())
            {
                return false;
            }

            const std::filesystem::path normalizedChild = canonicalOrAbsolute(child);
            const std::filesystem::path normalizedRoot = canonicalOrAbsolute(root);
            const std::wstring childText = comparablePathText(normalizedChild);
            const std::wstring rootText = comparablePathText(normalizedRoot);
            if (childText != rootText &&
                (childText.size() <= rootText.size() ||
                 childText.compare(0, rootText.size(), rootText) != 0 ||
                 (childText[rootText.size()] != L'\\' && childText[rootText.size()] != L'/')))
            {
                return false;
            }

            if (childText == rootText)
            {
                relative.clear();
                return true;
            }

            std::error_code error;
            relative = std::filesystem::relative(normalizedChild, normalizedRoot, error);
            if (error)
            {
                return false;
            }

            return relative.empty() ||
                (relative.begin() != relative.end() && (*relative.begin()) != L"..");
        }

        bool isRegularFile(const std::filesystem::path& path)
        {
            std::error_code error;
            return std::filesystem::exists(path, error) && std::filesystem::is_regular_file(path, error);
        }

        bool isDirectory(const std::filesystem::path& path)
        {
            std::error_code error;
            return std::filesystem::exists(path, error) && std::filesystem::is_directory(path, error);
        }

        bool hasExecutableExtension(const std::wstring& pathText)
        {
            return toLower(std::filesystem::path(pathText).extension().wstring()) == L".exe";
        }

        bool isLikelyGameDirectory(const std::filesystem::path& gamePath)
        {
            if (gamePath.empty())
            {
                return false;
            }

            static constexpr std::array<std::wstring_view, 8> knownGameExecutables{
                L"SkyrimSE.exe",
                L"SkyrimSELauncher.exe",
                L"SkyrimVR.exe",
                L"SkyrimVRLauncher.exe",
                L"TESV.exe",
                L"SkyrimLauncher.exe",
                L"Fallout4.exe",
                L"Starfield.exe"
            };

            for (std::wstring_view executable : knownGameExecutables)
            {
                if (isRegularFile(gamePath / std::filesystem::path(executable)))
                {
                    return true;
                }
            }

            return isDirectory(gamePath / L"Data");
        }

        bool isScriptExtenderLoaderName(
            const ModOrganizerExecutableImportContext& context,
            const std::filesystem::path& path)
        {
            const std::wstring fileName = path.filename().wstring();
            if (fileName.empty())
            {
                return false;
            }

            std::vector<std::wstring> candidateNames;
            if (!context.scriptExtenderLoaderExecutable.empty())
            {
                candidateNames.push_back(context.scriptExtenderLoaderExecutable);
            }
            if (equalsIgnoreCase(context.templateId, L"skyrimse"))
            {
                candidateNames.push_back(L"skse64_loader.exe");
                candidateNames.push_back(L"sksevr_loader.exe");
                candidateNames.push_back(L"skse_loader.exe");
            }

            for (const std::wstring& candidateName : candidateNames)
            {
                if (equalsIgnoreCase(fileName, candidateName))
                {
                    return true;
                }
            }

            return false;
        }

        std::wstring slashNormalized(std::filesystem::path path)
        {
            std::wstring text = path.lexically_normal().wstring();
            std::replace(text.begin(), text.end(), L'/', L'\\');
            return text;
        }

        std::wstring fileNameWithoutExtension(const std::wstring& pathText)
        {
            const std::filesystem::path path(pathText);
            const std::wstring stem = path.stem().wstring();
            return stem.empty() ? path.filename().wstring() : stem;
        }

        std::wstring slugifyExecutableId(const GameExecutable& executable, int fallbackIndex)
        {
            std::wstring source = trim(executable.id);
            if (source.empty())
            {
                source = trim(executable.displayName);
            }
            if (source.empty())
            {
                source = fileNameWithoutExtension(executable.executablePath);
            }
            if (source.empty())
            {
                source = L"executable-" + std::to_wstring(fallbackIndex + 1);
            }

            std::wstring id;
            bool previousWasDash = false;
            for (wchar_t character : source)
            {
                const wchar_t lower = static_cast<wchar_t>(std::towlower(character));
                if ((lower >= L'a' && lower <= L'z') || (lower >= L'0' && lower <= L'9'))
                {
                    id.push_back(lower);
                    previousWasDash = false;
                    continue;
                }

                if (!previousWasDash)
                {
                    id.push_back(L'-');
                    previousWasDash = true;
                }
            }

            while (!id.empty() && id.front() == L'-')
            {
                id.erase(id.begin());
            }
            while (!id.empty() && id.back() == L'-')
            {
                id.pop_back();
            }

            return id.empty() ? L"executable-" + std::to_wstring(fallbackIndex + 1) : id;
        }

        std::uintmax_t fileSize(const std::filesystem::path& path)
        {
            std::error_code error;
            const std::uintmax_t size = std::filesystem::file_size(path, error);
            return error ? 0 : size;
        }

        std::uintmax_t directorySize(
            const std::filesystem::path& directory,
            const std::function<bool(const std::filesystem::path&)>& shouldSkip)
        {
            std::uintmax_t total = 0;
            std::error_code error;
            std::filesystem::recursive_directory_iterator iterator(
                directory,
                std::filesystem::directory_options::skip_permission_denied,
                error);
            const std::filesystem::recursive_directory_iterator end;
            while (!error && iterator != end)
            {
                const std::filesystem::path current = iterator->path();
                if (shouldSkip(current))
                {
                    if (iterator->is_directory(error))
                    {
                        iterator.disable_recursion_pending();
                    }
                    iterator.increment(error);
                    continue;
                }

                if (iterator->is_regular_file(error))
                {
                    const std::uintmax_t size = iterator->file_size(error);
                    if (!error)
                    {
                        total += size;
                    }
                }

                iterator.increment(error);
            }

            return total;
        }

        std::vector<RawExecutableEntry> readRawExecutables(
            const std::map<std::wstring, std::wstring>& organizerIni)
        {
            std::map<int, std::map<std::wstring, std::wstring>> grouped;
            for (const auto& [key, value] : organizerIni)
            {
                if (!startsWith(key, customExecutablesPrefix))
                {
                    continue;
                }

                std::wstring rest = key.substr(customExecutablesPrefix.size());
                const std::size_t separator = rest.find(L'\\');
                if (separator == std::wstring::npos || separator == 0)
                {
                    continue;
                }

                int index = 0;
                try
                {
                    index = std::stoi(rest.substr(0, separator));
                }
                catch (...)
                {
                    continue;
                }

                std::wstring field = rest.substr(separator + 1);
                while (!field.empty() && field.front() == L'\\')
                {
                    field.erase(field.begin());
                }
                field = toLower(std::move(field));
                grouped[index][field] = decodeQtIniValue(value);
            }

            std::vector<RawExecutableEntry> entries;
            entries.reserve(grouped.size());
            for (auto& [index, fields] : grouped)
            {
                RawExecutableEntry entry;
                entry.index = index;
                entry.title = trim(std::move(fields[L"title"]));
                entry.binary = trim(std::move(fields[L"binary"]));
                entry.arguments = trim(std::move(fields[L"arguments"]));
                entry.workingDirectory = trim(std::move(fields[L"workingdirectory"]));
                entries.push_back(std::move(entry));
            }

            return entries;
        }

        void addUniqueMapping(std::vector<PathMapping>& mappings, PathMapping mapping)
        {
            if (mapping.sourceRoot.empty() || mapping.targetRoot.empty())
            {
                return;
            }

            mapping.sourceRoot = canonicalOrAbsolute(mapping.sourceRoot);
            mapping.targetRoot = canonicalOrAbsolute(mapping.targetRoot);

            const auto duplicate = std::find_if(
                mappings.begin(),
                mappings.end(),
                [&mapping](const PathMapping& existing)
                {
                    return areSamePath(existing.sourceRoot, mapping.sourceRoot);
                });
            if (duplicate != mappings.end())
            {
                return;
            }

            mappings.push_back(std::move(mapping));
        }

        std::vector<PathMapping> createMappings(const ModOrganizerExecutableImportContext& context)
        {
            std::vector<PathMapping> mappings;
            addUniqueMapping(mappings, PathMapping{
                context.modsDirectory,
                context.targetProjectDirectory / L"mods",
                L"mods",
                true
            });
            addUniqueMapping(mappings, PathMapping{
                context.profilesDirectory,
                context.targetProjectDirectory / L"profiles",
                L"profiles",
                true
            });
            addUniqueMapping(mappings, PathMapping{
                context.downloadsDirectory,
                context.targetProjectDirectory / L"downloads",
                L"downloads",
                true
            });
            addUniqueMapping(mappings, PathMapping{
                context.overwriteDirectory,
                context.targetProjectDirectory / L"overwrite",
                L"overwrite",
                true
            });

            std::set<std::wstring> localRoots;
            const auto addLocalRoot = [&localRoots](const std::filesystem::path& path)
            {
                if (!path.empty())
                {
                    localRoots.insert(comparablePathText(path));
                }
            };
            addLocalRoot(context.sourceDirectory);
            addLocalRoot(context.modsDirectory.parent_path());
            addLocalRoot(context.profilesDirectory.parent_path());
            addLocalRoot(context.downloadsDirectory.parent_path());
            addLocalRoot(context.overwriteDirectory.parent_path());

            for (const std::wstring& localRootText : localRoots)
            {
                addUniqueMapping(mappings, PathMapping{
                    std::filesystem::path(localRootText),
                    context.targetProjectDirectory,
                    {},
                    false
                });
            }

            std::sort(
                mappings.begin(),
                mappings.end(),
                [](const PathMapping& left, const PathMapping& right)
                {
                    return comparablePathText(left.sourceRoot).size() >
                        comparablePathText(right.sourceRoot).size();
                });
            return mappings;
        }

        std::filesystem::path resolveConfiguredPath(
            const std::wstring& text,
            const ModOrganizerExecutableImportContext& context)
        {
            if (text.empty())
            {
                return {};
            }

            std::filesystem::path path(expandEnvironmentVariables(text));
            if (path.is_absolute())
            {
                return canonicalOrAbsolute(path);
            }

            std::vector<std::filesystem::path> candidates;
            if (!context.gamePath.empty())
            {
                candidates.push_back(context.gamePath / path);
            }
            if (!context.sourceDirectory.empty())
            {
                candidates.push_back(context.sourceDirectory / path);
            }
            if (!context.modsDirectory.parent_path().empty())
            {
                candidates.push_back(context.modsDirectory.parent_path() / path);
            }
            candidates.push_back(path);

            for (const std::filesystem::path& candidate : candidates)
            {
                if (std::filesystem::exists(candidate))
                {
                    return canonicalOrAbsolute(candidate);
                }
            }

            return {};
        }

        std::filesystem::path findExecutableByNames(
            const std::filesystem::path& root,
            const std::vector<std::wstring>& names)
        {
            if (root.empty() || !isDirectory(root))
            {
                return {};
            }

            std::set<std::wstring> loweredNames;
            for (const std::wstring& name : names)
            {
                loweredNames.insert(toLower(name));
            }

            std::error_code error;
            std::filesystem::recursive_directory_iterator iterator(
                root,
                std::filesystem::directory_options::skip_permission_denied,
                error);
            const std::filesystem::recursive_directory_iterator end;
            while (!error && iterator != end)
            {
                if (iterator->is_regular_file(error))
                {
                    const std::wstring fileName = toLower(iterator->path().filename().wstring());
                    if (loweredNames.find(fileName) != loweredNames.end())
                    {
                        return canonicalOrAbsolute(iterator->path());
                    }
                }

                iterator.increment(error);
            }

            return {};
        }

        std::filesystem::path fallbackBinaryForTitle(
            const RawExecutableEntry& entry,
            const ModOrganizerExecutableImportContext& context)
        {
            if (entry.title.empty())
            {
                return {};
            }

            std::vector<std::wstring> candidateNames;
            if (containsIgnoreCase(entry.title, L"skse"))
            {
                if (!context.scriptExtenderLoaderExecutable.empty())
                {
                    candidateNames.push_back(context.scriptExtenderLoaderExecutable);
                }
                candidateNames.push_back(L"skse64_loader.exe");
                candidateNames.push_back(L"sksevr_loader.exe");
                candidateNames.push_back(L"skse_loader.exe");
            }
            else if (containsIgnoreCase(entry.title, L"launcher"))
            {
                candidateNames.push_back(L"SkyrimSELauncher.exe");
                candidateNames.push_back(L"SkyrimVRLauncher.exe");
                candidateNames.push_back(L"SkyrimLauncher.exe");
            }
            else if (containsIgnoreCase(entry.title, L"skyrim"))
            {
                candidateNames.push_back(L"SkyrimSE.exe");
                candidateNames.push_back(L"SkyrimVR.exe");
                candidateNames.push_back(L"TESV.exe");
            }

            for (const std::wstring& candidateName : candidateNames)
            {
                const std::filesystem::path candidate = context.gamePath / std::filesystem::path(candidateName);
                if (isRegularFile(candidate))
                {
                    return canonicalOrAbsolute(candidate);
                }
            }

            if (containsIgnoreCase(entry.title, L"nemesis"))
            {
                return findExecutableByNames(
                    context.modsDirectory,
                    {L"Nemesis Unlimited Behavior Engine.exe", L"Nemesis.exe"});
            }

            if (containsIgnoreCase(entry.title, L"pandora"))
            {
                return findExecutableByNames(
                    context.modsDirectory,
                    {L"Pandora Behaviour Engine+.exe", L"Pandora Behaviour Engine.exe", L"Pandora.exe"});
            }

            if (!candidateNames.empty() && !context.gamePath.empty())
            {
                return canonicalOrAbsolute(context.gamePath / std::filesystem::path(candidateNames.front()));
            }

            return {};
        }

        std::optional<std::wstring> defaultGameExecutableName(
            const ModOrganizerExecutableImportContext& context)
        {
            std::vector<std::wstring> candidateNames;
            if (equalsIgnoreCase(context.templateId, L"skyrimse"))
            {
                candidateNames.push_back(L"SkyrimSE.exe");
                candidateNames.push_back(L"Skyrim SE.exe");
                candidateNames.push_back(L"SkyrimSELauncher.exe");
            }
            else if (containsIgnoreCase(context.templateId, L"skyrim"))
            {
                candidateNames.push_back(L"SkyrimSE.exe");
                candidateNames.push_back(L"Skyrim SE.exe");
                candidateNames.push_back(L"TESV.exe");
            }

            candidateNames.push_back(L"SkyrimSE.exe");
            candidateNames.push_back(L"Skyrim SE.exe");
            candidateNames.push_back(L"Fallout4.exe");
            candidateNames.push_back(L"Starfield.exe");

            std::set<std::wstring> seen;
            for (const std::wstring& candidateName : candidateNames)
            {
                if (!seen.insert(toLower(candidateName)).second)
                {
                    continue;
                }

                if (isRegularFile(context.gamePath / std::filesystem::path(candidateName)))
                {
                    return candidateName;
                }
            }

            return std::nullopt;
        }

        std::optional<GameExecutable> defaultGameExecutable(
            const ModOrganizerExecutableImportContext& context)
        {
            const std::optional<std::wstring> executableName = defaultGameExecutableName(context);
            if (!executableName.has_value())
            {
                return std::nullopt;
            }

            std::wstring displayName = fileNameWithoutExtension(executableName.value());
            if (equalsIgnoreCase(context.templateId, L"skyrimse"))
            {
                displayName = L"Skyrim Special Edition";
            }

            return GameExecutable{
                L"game",
                displayName,
                executableName.value(),
                {},
                {}
            };
        }

        bool hasExecutablePath(
            const std::vector<GameExecutable>& executables,
            std::wstring_view executablePath)
        {
            for (const GameExecutable& executable : executables)
            {
                if (equalsIgnoreCase(executable.executablePath, executablePath))
                {
                    return true;
                }
            }

            return false;
        }

        bool isPathUnderAnyAlreadyCopiedRoot(
            const std::filesystem::path& path,
            const std::vector<PathMapping>& mappings)
        {
            for (const PathMapping& mapping : mappings)
            {
                std::filesystem::path relative;
                if (mapping.alreadyCopied && tryRelativePath(path, mapping.sourceRoot, relative))
                {
                    return true;
                }
            }

            return false;
        }

        void addCopyRoot(
            std::vector<ModOrganizerExecutableCopyRoot>& copyRoots,
            const ModOrganizerExecutableCopyRoot& root)
        {
            if (root.sourceDirectory.empty() || root.destinationDirectory.empty())
            {
                return;
            }

            const auto duplicate = std::find_if(
                copyRoots.begin(),
                copyRoots.end(),
                [&root](const ModOrganizerExecutableCopyRoot& existing)
                {
                    const bool sameOnlyFile =
                        (!existing.onlyFile.has_value() && !root.onlyFile.has_value()) ||
                        (existing.onlyFile.has_value() && root.onlyFile.has_value() &&
                         areSamePath(existing.onlyFile.value(), root.onlyFile.value()));
                    return sameOnlyFile && areSamePath(existing.sourceDirectory, root.sourceDirectory);
                });
            if (duplicate != copyRoots.end())
            {
                return;
            }

            const auto coveredByDirectoryRoot = std::find_if(
                copyRoots.begin(),
                copyRoots.end(),
                [&root](const ModOrganizerExecutableCopyRoot& existing)
                {
                    if (existing.onlyFile.has_value())
                    {
                        return false;
                    }

                    std::filesystem::path relative;
                    return tryRelativePath(root.sourceDirectory, existing.sourceDirectory, relative);
                });
            if (coveredByDirectoryRoot != copyRoots.end())
            {
                return;
            }

            copyRoots.push_back(root);
        }

        MappedPath mapConfiguredPath(
            const std::wstring& configuredText,
            const ModOrganizerExecutableImportContext& context,
            const std::vector<PathMapping>& mappings,
            bool isExecutableFile)
        {
            MappedPath mapped;
            const std::wstring decodedText = decodeQtIniValue(configuredText);
            if (decodedText.empty())
            {
                return mapped;
            }

            const std::filesystem::path resolvedPath = resolveConfiguredPath(decodedText, context);
            const std::filesystem::path configuredPath(expandEnvironmentVariables(decodedText));

            if (!resolvedPath.empty())
            {
                if (isExecutableFile && isScriptExtenderLoaderName(context, resolvedPath))
                {
                    const std::filesystem::path gameRootLoader =
                        context.gamePath / resolvedPath.filename();
                    if (isRegularFile(gameRootLoader))
                    {
                        mapped.sourcePath = canonicalOrAbsolute(gameRootLoader);
                        mapped.text = slashNormalized(resolvedPath.filename());
                        return mapped;
                    }
                }

                for (const PathMapping& mapping : mappings)
                {
                    std::filesystem::path relative;
                    if (!tryRelativePath(resolvedPath, mapping.sourceRoot, relative))
                    {
                        continue;
                    }

                    mapped.sourcePath = resolvedPath;
                    mapped.targetPath = mapping.targetRoot / relative;
                    mapped.isLocalProjectPath = true;
                    mapped.requiresCopy = !mapping.alreadyCopied;

                    std::filesystem::path targetRelative = mapping.targetRelativeRoot / relative;
                    mapped.text = slashNormalized(targetRelative.empty() ? relative : targetRelative);
                    if (mapped.text.empty())
                    {
                        mapped.text = slashNormalized(relative);
                    }

                    const bool sourceIsMappingRoot = areSamePath(resolvedPath.parent_path(), mapping.sourceRoot);
                    mapped.copySingleFile = isExecutableFile && mapped.requiresCopy && sourceIsMappingRoot;
                    return mapped;
                }

                if (isLikelyGameDirectory(context.gamePath))
                {
                    std::filesystem::path relative;
                    if (tryRelativePath(resolvedPath, context.gamePath, relative))
                    {
                        mapped.sourcePath = resolvedPath;
                        mapped.text = slashNormalized(relative);
                        return mapped;
                    }
                }
            }

            if (configuredPath.is_relative())
            {
                mapped.text = slashNormalized(configuredPath);
            }
            else
            {
                mapped.text = canonicalOrAbsolute(configuredPath).wstring();
            }

            return mapped;
        }

        void addCopyRootForMappedPath(
            std::vector<ModOrganizerExecutableCopyRoot>& copyRoots,
            const MappedPath& mapped,
            const std::vector<PathMapping>& mappings,
            bool isExecutableFile)
        {
            if (!mapped.requiresCopy || mapped.sourcePath.empty() || mapped.targetPath.empty())
            {
                return;
            }

            if (isPathUnderAnyAlreadyCopiedRoot(mapped.sourcePath, mappings))
            {
                return;
            }

            if (isExecutableFile && mapped.copySingleFile)
            {
                addCopyRoot(copyRoots, ModOrganizerExecutableCopyRoot{
                    mapped.sourcePath.parent_path(),
                    mapped.targetPath.parent_path(),
                    mapped.sourcePath
                });
                return;
            }

            const std::filesystem::path sourceDirectory = isExecutableFile
                ? mapped.sourcePath.parent_path()
                : mapped.sourcePath;
            const std::filesystem::path destinationDirectory = isExecutableFile
                ? mapped.targetPath.parent_path()
                : mapped.targetPath;

            if (sourceDirectory.empty() || destinationDirectory.empty() ||
                isPathUnderAnyAlreadyCopiedRoot(sourceDirectory, mappings))
            {
                return;
            }

            addCopyRoot(copyRoots, ModOrganizerExecutableCopyRoot{
                sourceDirectory,
                destinationDirectory,
                std::nullopt
            });
        }

        void replacePathVariants(std::wstring& text, const std::filesystem::path& source, const std::filesystem::path& target)
        {
            if (text.empty() || source.empty() || target.empty())
            {
                return;
            }

            const std::filesystem::path canonicalSource = canonicalOrAbsolute(source);
            const std::wstring sourceNative = canonicalSource.wstring();
            const std::wstring targetNative = canonicalOrAbsolute(target).wstring();
            std::wstring sourceForward = sourceNative;
            std::wstring targetForward = targetNative;
            std::replace(sourceForward.begin(), sourceForward.end(), L'\\', L'/');
            std::replace(targetForward.begin(), targetForward.end(), L'\\', L'/');

            replaceAllIgnoreCase(text, sourceNative, targetNative);
            replaceAllIgnoreCase(text, sourceForward, targetForward);

            const std::wstring sourceShort = shortPathText(canonicalSource);
            if (!sourceShort.empty() && !equalsIgnoreCase(sourceShort, sourceNative))
            {
                std::wstring sourceShortForward = sourceShort;
                std::replace(sourceShortForward.begin(), sourceShortForward.end(), L'\\', L'/');
                replaceAllIgnoreCase(text, sourceShort, targetNative);
                replaceAllIgnoreCase(text, sourceShortForward, targetForward);
            }
        }

        void replaceConfiguredPathText(
            std::wstring& text,
            std::wstring sourceText,
            const std::filesystem::path& target)
        {
            sourceText = expandEnvironmentVariables(decodeQtIniValue(std::move(sourceText)));
            if (text.empty() || sourceText.empty() || target.empty())
            {
                return;
            }

            const std::wstring targetNative = canonicalOrAbsolute(target).wstring();
            std::wstring sourceForward = sourceText;
            std::wstring sourceBackward = sourceText;
            std::wstring targetForward = targetNative;
            std::replace(sourceForward.begin(), sourceForward.end(), L'\\', L'/');
            std::replace(sourceBackward.begin(), sourceBackward.end(), L'/', L'\\');
            std::replace(targetForward.begin(), targetForward.end(), L'\\', L'/');

            replaceAllIgnoreCase(text, sourceText, targetNative);
            replaceAllIgnoreCase(text, sourceForward, targetForward);
            replaceAllIgnoreCase(text, sourceBackward, targetNative);
        }

        std::wstring remapArguments(
            std::wstring arguments,
            const std::vector<PathMapping>& mappings,
            const std::vector<ModOrganizerExecutableCopyRoot>& copyRoots)
        {
            for (const PathMapping& mapping : mappings)
            {
                if (mapping.alreadyCopied)
                {
                    replacePathVariants(arguments, mapping.sourceRoot, mapping.targetRoot);
                }
            }

            for (const ModOrganizerExecutableCopyRoot& root : copyRoots)
            {
                replacePathVariants(arguments, root.sourceDirectory, root.destinationDirectory);
                if (root.onlyFile.has_value())
                {
                    replacePathVariants(
                        arguments,
                        root.onlyFile.value(),
                        root.destinationDirectory / root.onlyFile->filename());
                }
            }

            return arguments;
        }

        std::uintmax_t copyRootSize(const ModOrganizerExecutableCopyRoot& root)
        {
            if (root.onlyFile.has_value())
            {
                return fileSize(root.onlyFile.value());
            }

            return directorySize(root.sourceDirectory, [](const std::filesystem::path&) { return false; });
        }

        std::vector<GameExecutable> normalizeImportedExecutables(
            const std::vector<GameExecutable>& executables)
        {
            std::vector<GameExecutable> normalized;
            normalized.reserve(executables.size());

            std::map<std::wstring, int> idCounts;
            for (int index = 0; index < static_cast<int>(executables.size()); ++index)
            {
                GameExecutable executable = executables[static_cast<std::size_t>(index)];
                executable.id = trim(std::move(executable.id));
                executable.displayName = trim(std::move(executable.displayName));
                executable.executablePath = trim(std::move(executable.executablePath));
                executable.arguments = trim(std::move(executable.arguments));
                executable.workingDirectory = trim(std::move(executable.workingDirectory));

                if (executable.executablePath.empty() || !hasExecutableExtension(executable.executablePath))
                {
                    continue;
                }

                if (executable.displayName.empty())
                {
                    executable.displayName = fileNameWithoutExtension(executable.executablePath);
                }

                const std::wstring baseId = slugifyExecutableId(executable, index);
                int& count = idCounts[baseId];
                ++count;
                executable.id = count == 1 ? baseId : baseId + L"-" + std::to_wstring(count);
                normalized.push_back(std::move(executable));
            }

            return normalized;
        }
    }

    ModOrganizerExecutableImportPlan ModOrganizerExecutableImportService::createPlan(
        const std::map<std::wstring, std::wstring>& organizerIni,
        const ModOrganizerExecutableImportContext& context)
    {
        const std::vector<PathMapping> mappings = createMappings(context);
        std::vector<ModOrganizerExecutableCopyRoot> copyRoots;
        std::vector<GameExecutable> executables;

        for (const RawExecutableEntry& entry : readRawExecutables(organizerIni))
        {
            std::wstring binaryText = entry.binary;
            if (binaryText.empty())
            {
                const std::filesystem::path fallback = fallbackBinaryForTitle(entry, context);
                if (!fallback.empty())
                {
                    binaryText = fallback.wstring();
                }
            }

            MappedPath binary = mapConfiguredPath(binaryText, context, mappings, true);
            if (binary.text.empty() || !hasExecutableExtension(binary.text))
            {
                continue;
            }

            MappedPath workingDirectory =
                mapConfiguredPath(entry.workingDirectory, context, mappings, false);
            addCopyRootForMappedPath(copyRoots, binary, mappings, true);
            if (!workingDirectory.sourcePath.empty() && isDirectory(workingDirectory.sourcePath))
            {
                addCopyRootForMappedPath(copyRoots, workingDirectory, mappings, false);
            }

            std::wstring arguments = entry.arguments;
            if (!binary.targetPath.empty())
            {
                replaceConfiguredPathText(arguments, binaryText, binary.targetPath);
            }
            if (!workingDirectory.targetPath.empty())
            {
                replaceConfiguredPathText(arguments, entry.workingDirectory, workingDirectory.targetPath);
            }

            executables.push_back(GameExecutable{
                {},
                entry.title.empty() ? fileNameWithoutExtension(binary.text) : entry.title,
                binary.text,
                arguments,
                workingDirectory.text
            });
        }

        for (GameExecutable& executable : executables)
        {
            executable.arguments = remapArguments(std::move(executable.arguments), mappings, copyRoots);
        }

        if (const std::optional<GameExecutable> defaultExecutable = defaultGameExecutable(context);
            defaultExecutable.has_value() &&
            !hasExecutablePath(executables, defaultExecutable->executablePath))
        {
            executables.insert(executables.begin(), defaultExecutable.value());
        }

        ModOrganizerExecutableImportPlan plan;
        plan.executables = normalizeImportedExecutables(executables);
        plan.copyRoots = std::move(copyRoots);
        for (const ModOrganizerExecutableCopyRoot& root : plan.copyRoots)
        {
            plan.totalCopyBytes += copyRootSize(root);
        }

        return plan;
    }
}
