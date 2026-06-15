#include "FluxoraCore/Services/AppSettingsService.hpp"

#include "FluxoraCore/Services/Logger.hpp"
#include "FluxoraCore/Support/JsonReader.hpp"
#include "FluxoraCore/Support/JsonWriter.hpp"

#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <cwctype>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fluxora
{
    namespace
    {
        std::string toUtf8(const std::wstring& value)
        {
#ifdef _WIN32
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
                throw std::invalid_argument("Settings file is not valid UTF-8.");
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
            std::filesystem::create_directories(path.parent_path());

            std::ofstream file(path, std::ios::out | std::ios::trunc | std::ios::binary);
            if (!file)
            {
                throw std::runtime_error("Failed to write Fluxora settings.");
            }

            file.write(content.data(), static_cast<std::streamsize>(content.size()));
        }

        std::wstring readStringOrDefault(
            const JsonValue& object,
            std::wstring_view field,
            std::wstring_view fallback = L"")
        {
            const JsonValue* value = object.find(field);
            if (value == nullptr || value->isNull())
            {
                return std::wstring(fallback);
            }

            if (!value->isString())
            {
                return std::wstring(fallback);
            }

            return value->asString();
        }

        bool readBoolOrDefault(const JsonValue& object, std::wstring_view field, bool fallback = false)
        {
            const JsonValue* value = object.find(field);
            if (value == nullptr || value->isNull() || value->type() != JsonValue::Type::Boolean)
            {
                return fallback;
            }

            return value->asBoolean();
        }

        std::wstring trim(std::wstring value)
        {
            while (!value.empty() && std::iswspace(value.front()))
            {
                value.erase(value.begin());
            }

            while (!value.empty() && std::iswspace(value.back()))
            {
                value.pop_back();
            }

            return value;
        }

        std::wstring toUpperInvariant(std::wstring value)
        {
            for (wchar_t& ch : value)
            {
                ch = static_cast<wchar_t>(std::towupper(ch));
            }

            return value;
        }

        std::wstring normalizeLanguageCode(std::wstring value)
        {
            value = trim(std::move(value));
            for (wchar_t& ch : value)
            {
                if (ch == L'_')
                {
                    ch = L'-';
                }
                else
                {
                    ch = static_cast<wchar_t>(std::towlower(ch));
                }
            }

            return value.empty() ? L"en" : value;
        }

        std::map<std::wstring, std::wstring> readIniValues(const std::filesystem::path& path)
        {
            std::map<std::wstring, std::wstring> values;
            const std::string content = readTextFile(path);
            if (content.empty())
            {
                return values;
            }

            std::wstring wide;
            try
            {
                wide = fromUtf8(content);
            }
            catch (const std::exception&)
            {
                return values;
            }

            std::wstring line;
            for (wchar_t ch : wide)
            {
                if (ch == L'\r')
                {
                    continue;
                }

                if (ch != L'\n')
                {
                    line.push_back(ch);
                    continue;
                }

                const std::size_t separator = line.find(L'=');
                if (separator != std::wstring::npos)
                {
                    const std::wstring key = toUpperInvariant(trim(line.substr(0, separator)));
                    const std::wstring value = trim(line.substr(separator + 1));
                    if (!key.empty())
                    {
                        values[key] = value;
                    }
                }

                line.clear();
            }

            if (!line.empty())
            {
                const std::size_t separator = line.find(L'=');
                if (separator != std::wstring::npos)
                {
                    const std::wstring key = toUpperInvariant(trim(line.substr(0, separator)));
                    const std::wstring value = trim(line.substr(separator + 1));
                    if (!key.empty())
                    {
                        values[key] = value;
                    }
                }
            }

            return values;
        }

        void writeIniValues(
            const std::filesystem::path& path,
            const std::map<std::wstring, std::wstring>& values)
        {
            std::wstring content;
            for (const auto& [key, value] : values)
            {
                content += key;
                content += L'=';
                content += value;
                content += L'\n';
            }

            writeTextFile(path, toUtf8(content));
        }
    }

    AppSettingsService::AppSettingsService(Logger& logger) noexcept
        : logger_(logger)
    {
    }

    void AppSettingsService::initialize()
    {
        if (initialized_)
        {
            return;
        }

        settingsPath_ = resolveSettingsPath();
        appConfigPath_ = resolveAppConfigPath();
        std::filesystem::create_directories(settingsPath_.parent_path());
        std::filesystem::create_directories(appConfigPath_.parent_path());
        (void)loadLanguageCode();
        initialized_ = true;
        logger_.write(LogLevel::Info, "App settings service initialized.");
    }

    void AppSettingsService::shutdown()
    {
        if (!initialized_)
        {
            return;
        }

        initialized_ = false;
        logger_.write(LogLevel::Info, "App settings service shut down.");
    }

    NexusModsStoredAuth AppSettingsService::loadNexusModsAuth() const
    {
        if (settingsPath_.empty() || !std::filesystem::exists(settingsPath_))
        {
            return {};
        }

        const std::string content = readTextFile(settingsPath_);
        if (content.empty())
        {
            return {};
        }

        try
        {
            const JsonValue root = JsonReader::parse(fromUtf8(content));
            const JsonValue* nexusMods = root.find(L"nexusMods");
            if (nexusMods == nullptr || !nexusMods->isObject())
            {
                return {};
            }

            NexusModsStoredAuth auth;
            auth.linked = readBoolOrDefault(*nexusMods, L"linked");
            auth.username = readStringOrDefault(*nexusMods, L"username");
            auth.userId = readStringOrDefault(*nexusMods, L"userId");
            auth.tokenType = readStringOrDefault(*nexusMods, L"tokenType");
            auth.expiresAtUtc = readStringOrDefault(*nexusMods, L"expiresAtUtc");
            auth.protectedAccessToken = readStringOrDefault(*nexusMods, L"protectedAccessToken");
            auth.protectedRefreshToken = readStringOrDefault(*nexusMods, L"protectedRefreshToken");
            auth.linked = auth.linked && !auth.protectedAccessToken.empty();
            return auth;
        }
        catch (const std::exception&)
        {
            return {};
        }
    }

    void AppSettingsService::saveNexusModsAuth(const NexusModsStoredAuth& auth) const
    {
        JsonWriter writer;
        writer.beginObject();
        writer.key(L"nexusMods").beginObject();
        writer.field(L"linked", auth.linked);
        writer.field(L"username", auth.username);
        writer.field(L"userId", auth.userId);
        writer.field(L"tokenType", auth.tokenType);
        writer.field(L"expiresAtUtc", auth.expiresAtUtc);
        writer.field(L"protectedAccessToken", auth.protectedAccessToken);
        writer.field(L"protectedRefreshToken", auth.protectedRefreshToken);
        writer.endObject();
        writer.endObject();

        writeTextFile(settingsPath_, toUtf8(writer.str()));
    }

    void AppSettingsService::clearNexusModsAuth() const
    {
        NexusModsStoredAuth empty;
        saveNexusModsAuth(empty);
    }

    std::wstring AppSettingsService::loadLanguageCode() const
    {
        std::map<std::wstring, std::wstring> values = readIniValues(appConfigPath_);
        const auto found = values.find(L"LANGUAGE");
        if (found != values.end() && !trim(found->second).empty())
        {
            return normalizeLanguageCode(found->second);
        }

        const auto legacyFound = values.find(L"LANGUAGES");
        if (legacyFound != values.end() && !trim(legacyFound->second).empty())
        {
            const std::wstring migratedLanguage = normalizeLanguageCode(legacyFound->second);
            values.erase(legacyFound);
            values[L"LANGUAGE"] = migratedLanguage;
            writeIniValues(appConfigPath_, values);
            return migratedLanguage;
        }

        const std::wstring defaultLanguage = resolveDefaultLanguageCode();
        values[L"LANGUAGE"] = defaultLanguage;
        writeIniValues(appConfigPath_, values);
        return defaultLanguage;
    }

    void AppSettingsService::saveLanguageCode(std::wstring_view languageCode) const
    {
        std::map<std::wstring, std::wstring> values = readIniValues(appConfigPath_);
        values.erase(L"LANGUAGES");
        values[L"LANGUAGE"] = normalizeLanguageCode(std::wstring(languageCode));
        writeIniValues(appConfigPath_, values);
    }

    const std::filesystem::path& AppSettingsService::settingsPath() const noexcept
    {
        return settingsPath_;
    }

    const std::filesystem::path& AppSettingsService::appConfigPath() const noexcept
    {
        return appConfigPath_;
    }

    bool AppSettingsService::isInitialized() const noexcept
    {
        return initialized_;
    }

    std::filesystem::path AppSettingsService::resolveSettingsPath() const
    {
#ifdef _WIN32
        wchar_t buffer[MAX_PATH]{};
        const DWORD length = GetEnvironmentVariableW(L"APPDATA", buffer, MAX_PATH);
        if (length > 0 && length < MAX_PATH)
        {
            return std::filesystem::path(buffer) / L"Fluxora" / L"settings.json";
        }
#endif

        return std::filesystem::current_path() / L"Fluxora" / L"settings.json";
    }

    std::filesystem::path AppSettingsService::resolveAppConfigPath() const
    {
#ifdef _WIN32
        wchar_t buffer[MAX_PATH]{};
        const DWORD length = GetEnvironmentVariableW(L"APPDATA", buffer, MAX_PATH);
        if (length > 0 && length < MAX_PATH)
        {
            return std::filesystem::path(buffer) / L"Fluxora" / L"settings.ini";
        }
#endif

        return std::filesystem::current_path() / L"Fluxora" / L"settings.ini";
    }

    std::wstring AppSettingsService::resolveDefaultLanguageCode() const
    {
#ifdef _WIN32
        wchar_t localeName[LOCALE_NAME_MAX_LENGTH]{};
        const int length = GetUserDefaultLocaleName(localeName, LOCALE_NAME_MAX_LENGTH);
        if (length > 0)
        {
            return normalizeLanguageCode(localeName);
        }
#endif

        return L"en";
    }
}
