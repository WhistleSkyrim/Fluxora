#include "FluxoraCore/Services/ModOrganizerProfileOrderService.hpp"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iterator>
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
        constexpr std::wstring_view modKind = L"mod";
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

        bool endsWithIgnoreCase(std::wstring_view value, std::wstring_view suffix)
        {
            if (value.size() < suffix.size())
            {
                return false;
            }

            return equalsIgnoreCase(value.substr(value.size() - suffix.size()), suffix);
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

        bool isSeparatorName(std::wstring_view name)
        {
            return endsWithIgnoreCase(name, L"_separator");
        }

        std::wstring separatorTitle(std::wstring name)
        {
            if (isSeparatorName(name))
            {
                name = name.substr(0, name.size() - std::wstring_view(L"_separator").size());
            }

            std::replace(name.begin(), name.end(), L'_', L' ');
            name = trim(std::move(name));
            while (!name.empty() && (name.front() == L'+' || name.front() == L'-'))
            {
                name.erase(name.begin());
                name = trim(std::move(name));
            }

            return name.empty() ? std::wstring(L"Раздел") : name;
        }
    }

    ModOrganizerProfileOrder ModOrganizerProfileOrderService::read(
        const std::filesystem::path& profileDirectory)
    {
        ModOrganizerProfileOrder order;
        const std::filesystem::path modlistPath = profileDirectory / L"modlist.txt";
        const std::string bytes = readTextFile(modlistPath);
        if (bytes.empty())
        {
            return order;
        }

        std::wstring text = fromBytes(bytes);
        if (!text.empty() && text.front() == 0xFEFF)
        {
            text.erase(text.begin());
        }

        std::set<std::wstring> seenFolders;
        std::size_t lineStart = 0;
        while (lineStart <= text.size())
        {
            const std::size_t lineEnd = text.find_first_of(L"\r\n", lineStart);
            std::wstring line = lineEnd == std::wstring::npos
                ? text.substr(lineStart)
                : text.substr(lineStart, lineEnd - lineStart);
            lineStart = lineEnd == std::wstring::npos
                ? text.size() + 1
                : text.find_first_not_of(L"\r\n", lineEnd);
            if (lineStart == std::wstring::npos)
            {
                lineStart = text.size() + 1;
            }

            line = trim(std::move(line));
            if (line.empty() || line.front() == L'#')
            {
                continue;
            }

            bool enabled = true;
            if (line.front() == L'+' || line.front() == L'-')
            {
                enabled = line.front() != L'-';
                line.erase(line.begin());
                line = trim(std::move(line));
            }

            if (line.empty() || line.front() == L'*')
            {
                continue;
            }

            if (isSeparatorName(line))
            {
                order.items.push_back(ProfileOrderImportItemRecord{
                    std::wstring(separatorKind),
                    {},
                    separatorTitle(line)
                });
                continue;
            }

            const std::wstring key = toLower(line);
            if (seenFolders.insert(key).second)
            {
                order.enabledByFolder[key] = enabled;
                order.items.push_back(ProfileOrderImportItemRecord{
                    std::wstring(modKind),
                    line,
                    {}
                });
            }
        }

        // MO2 writes modlist.txt in reverse priority: the first mod entry wins last.
        // Fluxora stores profile order from lowest priority to highest so VFS can
        // walk the list in order and let later mods override earlier ones.
        std::reverse(order.items.begin(), order.items.end());
        return order;
    }
}
