#include "FluxoraVfs/VfsConfig.hpp"

#include "FluxoraVfs/VfsProtocol.hpp"
#include "FluxoraCore/Support/JsonReader.hpp"

#include <windows.h>

#include <fstream>
#include <iterator>

namespace fluxora::vfs
{
    namespace
    {
        std::wstring readEnvironment(const wchar_t* name)
        {
            const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
            if (needed == 0)
            {
                return {};
            }

            std::wstring value(needed, L'\0');
            const DWORD written = GetEnvironmentVariableW(name, value.data(), needed);
            value.resize(written);
            return value;
        }

        std::string readAllBytes(const std::wstring& path)
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

        std::wstring fromUtf8(const std::string& value)
        {
            if (value.empty())
            {
                return {};
            }

            const int size = MultiByteToWideChar(
                CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
            if (size <= 0)
            {
                return {};
            }

            std::wstring out(static_cast<size_t>(size), L'\0');
            MultiByteToWideChar(
                CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), size);
            return out;
        }

        std::wstring readString(const JsonValue& object, const wchar_t* field)
        {
            const JsonValue* value = object.find(field);
            return (value != nullptr && value->isString()) ? value->asString() : std::wstring{};
        }
    }

    bool loadVfsConfigFromEnvironment(VfsConfig& config)
    {
        const std::wstring path = readEnvironment(protocol::configEnvironmentVariable);
        if (path.empty())
        {
            return false;
        }

        const std::string bytes = readAllBytes(path);
        if (bytes.empty())
        {
            return false;
        }

        try
        {
            const JsonValue root = JsonReader::parse(fromUtf8(bytes));
            if (!root.isObject())
            {
                return false;
            }

            if (const JsonValue* schema = root.find(protocol::fields::schemaVersion);
                schema != nullptr && schema->isNumber())
            {
                config.schemaVersion = std::stoi(schema->asNumber());
            }

            config.target = readString(root, protocol::fields::target);
            config.overwrite = readString(root, protocol::fields::overwrite);
            config.logPath = readString(root, protocol::fields::logPath);
            config.hookDll = readString(root, protocol::fields::hookDll);

            if (const JsonValue* mods = root.find(protocol::fields::mods);
                mods != nullptr && mods->isArray())
            {
                for (const JsonValue& mod : mods->asArray())
                {
                    if (mod.isString() && !mod.asString().empty())
                    {
                        config.mods.push_back(mod.asString());
                    }
                }
            }
        }
        catch (...)
        {
            return false;
        }

        return config.isValid();
    }
}
