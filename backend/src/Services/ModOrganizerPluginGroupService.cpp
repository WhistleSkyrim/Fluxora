#include "FluxoraCore/Services/ModOrganizerPluginGroupService.hpp"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fluxora
{
    namespace
    {
        constexpr std::wstring_view groupsFileName = L"plugingroups.txt";
        constexpr std::wstring_view pluginsFileName = L"plugins.txt";
        constexpr std::wstring_view loadOrderFileName = L"loadorder.txt";
        constexpr std::wstring_view pluginKind = L"plugin";
        constexpr std::wstring_view separatorKind = L"separator";

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

        std::wstring fromBytes(const std::string& value)
        {
#ifdef _WIN32
            if (value.empty())
            {
                return {};
            }

            int size = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0);
            UINT codePage = CP_UTF8;
            DWORD flags = MB_ERR_INVALID_CHARS;
            if (size <= 0)
            {
                codePage = CP_ACP;
                flags = 0;
                size = MultiByteToWideChar(
                    codePage,
                    flags,
                    value.data(),
                    static_cast<int>(value.size()),
                    nullptr,
                    0);
            }

            if (size <= 0)
            {
                throw std::invalid_argument("Text file encoding is not supported.");
            }

            std::wstring out(static_cast<std::size_t>(size), L'\0');
            MultiByteToWideChar(
                codePage,
                flags,
                value.data(),
                static_cast<int>(value.size()),
                out.data(),
                size);
            return out;
#else
            return std::wstring(value.begin(), value.end());
#endif
        }

        std::wstring readWideTextFile(const std::filesystem::path& path)
        {
            std::wstring text = fromBytes(readTextFile(path));
            if (!text.empty() && text.front() == 0xFEFF)
            {
                text.erase(text.begin());
            }

            return text;
        }

        template <typename Callback>
        void forEachLine(const std::wstring& text, Callback&& callback)
        {
            std::size_t offset = 0;
            while (offset <= text.size())
            {
                const std::size_t lineEnd = text.find_first_of(L"\r\n", offset);
                std::wstring line = lineEnd == std::wstring::npos
                    ? text.substr(offset)
                    : text.substr(offset, lineEnd - offset);

                callback(std::move(line));

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
        }

        std::wstring pluginFileName(std::wstring value)
        {
            value = trim(std::move(value));
            if (value.empty())
            {
                return {};
            }

            return std::filesystem::path(value).filename().wstring();
        }

        std::vector<std::wstring> readPluginNamesFromFile(
            const std::filesystem::path& path,
            bool hasEnabledPrefix)
        {
            std::vector<std::wstring> plugins;
            const std::wstring text = readWideTextFile(path);
            if (text.empty())
            {
                return plugins;
            }

            std::set<std::wstring> seen;
            forEachLine(text, [&plugins, &seen, hasEnabledPrefix](std::wstring line)
            {
                line = trim(std::move(line));
                if (line.empty() || line.front() == L'#')
                {
                    return;
                }

                if (hasEnabledPrefix && line.front() == L'*')
                {
                    line = trim(line.substr(1));
                }

                const std::wstring name = pluginFileName(std::move(line));
                const std::wstring key = toLower(name);
                if (!name.empty() && seen.insert(key).second)
                {
                    plugins.push_back(name);
                }
            });

            return plugins;
        }

        std::vector<std::wstring> readPluginOrder(
            const std::filesystem::path& profileDirectory,
            const BuildTemplate& resolvedTemplate)
        {
            std::vector<std::wstring> plugins;
            std::set<std::wstring> seen;

            const auto append = [&plugins, &seen](const std::wstring& pluginName)
            {
                const std::wstring normalized = pluginFileName(pluginName);
                const std::wstring key = toLower(normalized);
                if (!normalized.empty() && seen.insert(key).second)
                {
                    plugins.push_back(normalized);
                }
            };

            for (const std::wstring& basePlugin : resolvedTemplate.basePlugins)
            {
                append(basePlugin);
            }

            const std::vector<std::wstring> loadOrderPlugins =
                readPluginNamesFromFile(profileDirectory / std::filesystem::path(loadOrderFileName), false);
            const std::vector<std::wstring> storedPlugins =
                readPluginNamesFromFile(profileDirectory / std::filesystem::path(pluginsFileName), true);

            const std::vector<std::wstring>& orderedPlugins = loadOrderPlugins.empty()
                ? storedPlugins
                : loadOrderPlugins;
            for (const std::wstring& plugin : orderedPlugins)
            {
                if (!isBasePlugin(resolvedTemplate, plugin))
                {
                    append(plugin);
                }
            }

            for (const std::wstring& plugin : storedPlugins)
            {
                if (!isBasePlugin(resolvedTemplate, plugin))
                {
                    append(plugin);
                }
            }

            return plugins;
        }

        std::map<std::wstring, std::wstring> readPluginGroups(const std::filesystem::path& path)
        {
            std::map<std::wstring, std::wstring> groups;
            const std::wstring text = readWideTextFile(path);
            if (text.empty())
            {
                return groups;
            }

            forEachLine(text, [&groups](std::wstring line)
            {
                line = trim(std::move(line));
                if (line.empty() || line.front() == L'#')
                {
                    return;
                }

                const std::size_t separator = line.find(L'|');
                if (separator == std::wstring::npos ||
                    line.find(L'|', separator + 1) != std::wstring::npos)
                {
                    return;
                }

                const std::wstring pluginName = pluginFileName(line.substr(0, separator));
                const std::wstring group = trim(line.substr(separator + 1));
                if (!pluginName.empty() && !group.empty())
                {
                    groups[toLower(pluginName)] = group;
                }
            });

            return groups;
        }
    }

    std::vector<ProfilePluginOrderImportItemRecord> ModOrganizerPluginGroupService::read(
        const std::filesystem::path& profileDirectory,
        const BuildTemplate& resolvedTemplate)
    {
        const std::map<std::wstring, std::wstring> groupByPlugin =
            readPluginGroups(profileDirectory / std::filesystem::path(groupsFileName));
        if (groupByPlugin.empty())
        {
            return {};
        }

        const std::vector<std::wstring> pluginNames = readPluginOrder(profileDirectory, resolvedTemplate);
        if (pluginNames.empty())
        {
            return {};
        }

        std::vector<ProfilePluginOrderImportItemRecord> items;
        items.reserve(pluginNames.size() + groupByPlugin.size());

        bool importedAnyGroup = false;
        std::wstring currentGroup;
        for (const std::wstring& pluginName : pluginNames)
        {
            std::wstring group;
            if (const auto match = groupByPlugin.find(toLower(pluginName)); match != groupByPlugin.end())
            {
                group = match->second;
            }

            if (group.empty())
            {
                currentGroup.clear();
            }
            else if (group != currentGroup)
            {
                items.push_back(ProfilePluginOrderImportItemRecord{
                    std::wstring(separatorKind),
                    {},
                    group
                });
                currentGroup = group;
                importedAnyGroup = true;
            }

            items.push_back(ProfilePluginOrderImportItemRecord{
                std::wstring(pluginKind),
                pluginName,
                {}
            });
        }

        return importedAnyGroup ? items : std::vector<ProfilePluginOrderImportItemRecord>{};
    }
}
