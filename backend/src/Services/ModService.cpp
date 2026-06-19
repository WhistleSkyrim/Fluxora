#include "FluxoraCore/Services/ModService.hpp"

#include "FluxoraCore/Services/AppSettingsService.hpp"
#include "FluxoraCore/Services/BuildPathSettingsService.hpp"
#include "FluxoraCore/Services/Logger.hpp"
#include "FluxoraCore/Support/JsonReader.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cwctype>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <initializer_list>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#include <winhttp.h>
#endif

namespace fluxora
{
    namespace
    {
        struct NexusLatestFile
        {
            std::wstring version;
            std::wstring fileId;
            std::wstring payloadJson;
        };

        std::wstring toLower(std::wstring value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character)
            {
                return static_cast<wchar_t>(std::towlower(character));
            });
            return value;
        }

        std::wstring trim(std::wstring value)
        {
            const auto first = value.find_first_not_of(L" \t\r\n.");
            if (first == std::wstring::npos)
            {
                return {};
            }

            const auto last = value.find_last_not_of(L" \t\r\n.");
            return value.substr(first, last - first + 1);
        }

        bool equalsIgnoreCase(std::wstring_view left, std::wstring_view right)
        {
            return toLower(std::wstring(left)) == toLower(std::wstring(right));
        }

        std::wstring nowUtcText()
        {
            const std::time_t now = std::time(nullptr);
            std::tm utc{};
#ifdef _WIN32
            gmtime_s(&utc, &now);
#else
            gmtime_r(&now, &utc);
#endif
            std::wstringstream stream;
            stream << std::put_time(&utc, L"%Y-%m-%dT%H:%M:%SZ");
            return stream.str();
        }

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
                throw std::invalid_argument("Text is not valid UTF-8.");
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

        std::wstring percentEncode(std::wstring_view value)
        {
            const std::string utf8 = toUtf8(std::wstring(value));
            std::wstringstream stream;
            stream << std::uppercase << std::hex;
            for (unsigned char character : utf8)
            {
                if ((character >= 'A' && character <= 'Z') ||
                    (character >= 'a' && character <= 'z') ||
                    (character >= '0' && character <= '9') ||
                    character == '-' || character == '_' || character == '.' || character == '~')
                {
                    stream << static_cast<wchar_t>(character);
                }
                else
                {
                    stream << L'%' << std::setw(2) << std::setfill(L'0') << static_cast<int>(character);
                }
            }

            return stream.str();
        }

#ifdef _WIN32
        unsigned char hexNibble(wchar_t character)
        {
            if (character >= L'0' && character <= L'9')
            {
                return static_cast<unsigned char>(character - L'0');
            }
            if (character >= L'a' && character <= L'f')
            {
                return static_cast<unsigned char>(10 + character - L'a');
            }
            if (character >= L'A' && character <= L'F')
            {
                return static_cast<unsigned char>(10 + character - L'A');
            }

            throw std::runtime_error("Invalid protected NexusMods OAuth token.");
        }

        std::vector<unsigned char> hexToBytes(std::wstring_view value)
        {
            if (value.size() % 2 != 0)
            {
                throw std::runtime_error("Invalid protected NexusMods OAuth token.");
            }

            std::vector<unsigned char> bytes(value.size() / 2);
            for (std::size_t index = 0; index < bytes.size(); ++index)
            {
                bytes[index] = static_cast<unsigned char>(
                    (hexNibble(value[index * 2]) << 4) |
                    hexNibble(value[index * 2 + 1]));
            }

            return bytes;
        }
#endif

        std::wstring unprotectSecret(std::wstring_view protectedValue)
        {
            if (protectedValue.empty())
            {
                return {};
            }

#ifdef _WIN32
            std::vector<unsigned char> bytes = hexToBytes(protectedValue);

            DATA_BLOB input{};
            input.pbData = bytes.data();
            input.cbData = static_cast<DWORD>(bytes.size());

            DATA_BLOB output{};
            if (!CryptUnprotectData(
                    &input,
                    nullptr,
                    nullptr,
                    nullptr,
                    nullptr,
                    CRYPTPROTECT_UI_FORBIDDEN,
                    &output))
            {
                throw std::runtime_error("Failed to unprotect NexusMods OAuth token.");
            }

            std::wstring value(
                reinterpret_cast<wchar_t*>(output.pbData),
                output.cbData / sizeof(wchar_t));
            LocalFree(output.pbData);
            return value;
#else
            return std::wstring(protectedValue);
#endif
        }

        std::string nexusAuthUnavailableMessage(const AppSettingsService& settings)
        {
            const NexusModsStoredAuth auth = settings.loadNexusModsAuth();
            if (!auth.linked || (auth.protectedAccessToken.empty() && auth.protectedApiKey.empty()))
            {
                return "NexusMods account is not linked. Connect NexusMods in settings.";
            }

            return "NexusMods authentication token is not available. Reconnect NexusMods in settings and try again.";
        }

        std::wstring buildNexusAuthHeader(const AppSettingsService& settings)
        {
            const NexusModsStoredAuth auth = settings.loadNexusModsAuth();
            if (!auth.linked)
            {
                return {};
            }

            if (!auth.protectedApiKey.empty())
            {
                const std::wstring apiKey = unprotectSecret(auth.protectedApiKey);
                if (!apiKey.empty())
                {
                    return L"apikey: " + apiKey + L"\r\n";
                }
            }

            const std::wstring accessToken = unprotectSecret(auth.protectedAccessToken);
            if (accessToken.empty())
            {
                return {};
            }

            std::wstring tokenType = trim(auth.tokenType);
            if (tokenType.empty())
            {
                tokenType = L"Bearer";
            }

            return L"Authorization: " + tokenType + L" " + accessToken + L"\r\n";
        }

#ifdef _WIN32
        std::string nexusHttpErrorMessage(DWORD statusCode)
        {
            if (statusCode == 401)
            {
                return "Nexus request returned HTTP 401. Reconnect NexusMods in settings and try again.";
            }

            return "Nexus request returned HTTP " + std::to_string(statusCode) + ".";
        }

        std::string winHttpGet(const std::wstring& url, std::wstring_view extraHeaders)
        {
            URL_COMPONENTS components{};
            components.dwStructSize = sizeof(components);
            components.dwSchemeLength = static_cast<DWORD>(-1);
            components.dwHostNameLength = static_cast<DWORD>(-1);
            components.dwUrlPathLength = static_cast<DWORD>(-1);
            components.dwExtraInfoLength = static_cast<DWORD>(-1);

            if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components))
            {
                throw std::runtime_error("Invalid Nexus request URL.");
            }

            std::wstring host(components.lpszHostName, components.dwHostNameLength);
            std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
            path.append(components.lpszExtraInfo, components.dwExtraInfoLength);

            HINTERNET session = WinHttpOpen(
                L"FluxoraModManager/1.0",
                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS,
                0);
            if (session == nullptr)
            {
                throw std::runtime_error("Failed to initialize Nexus HTTP session.");
            }

            HINTERNET connection = WinHttpConnect(session, host.c_str(), components.nPort, 0);
            if (connection == nullptr)
            {
                WinHttpCloseHandle(session);
                throw std::runtime_error("Failed to connect to Nexus.");
            }

            const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
            HINTERNET request = WinHttpOpenRequest(
                connection,
                L"GET",
                path.c_str(),
                nullptr,
                WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                flags);
            if (request == nullptr)
            {
                WinHttpCloseHandle(connection);
                WinHttpCloseHandle(session);
                throw std::runtime_error("Failed to open Nexus request.");
            }

            std::wstring headers =
                L"Accept: application/json\r\n"
                L"Application-Name: Fluxora\r\n"
                L"Application-Version: 1.0\r\n";
            headers += extraHeaders;

            if (!WinHttpSendRequest(
                    request,
                    headers.c_str(),
                    static_cast<DWORD>(headers.size()),
                    WINHTTP_NO_REQUEST_DATA,
                    0,
                    0,
                    0) ||
                !WinHttpReceiveResponse(request, nullptr))
            {
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connection);
                WinHttpCloseHandle(session);
                throw std::runtime_error("Nexus request failed.");
            }

            DWORD statusCode{};
            DWORD statusCodeSize = sizeof(statusCode);
            WinHttpQueryHeaders(
                request,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,
                &statusCode,
                &statusCodeSize,
                WINHTTP_NO_HEADER_INDEX);
            if (statusCode < 200 || statusCode >= 300)
            {
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connection);
                WinHttpCloseHandle(session);
                throw std::runtime_error(nexusHttpErrorMessage(statusCode));
            }

            std::string body;
            while (true)
            {
                DWORD available{};
                if (!WinHttpQueryDataAvailable(request, &available) || available == 0)
                {
                    break;
                }

                std::string buffer(available, '\0');
                DWORD read{};
                if (!WinHttpReadData(request, buffer.data(), available, &read))
                {
                    break;
                }
                buffer.resize(read);
                body += buffer;
            }

            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return body;
        }
#endif

        bool canCheckNexusUpdates(const InstalledModRecord& mod)
        {
            return equalsIgnoreCase(mod.source.provider, L"nexus") &&
                !mod.source.gameDomain.empty() &&
                !mod.source.remoteModId.empty();
        }

        std::wstring readStringCandidate(const JsonValue& object, std::initializer_list<const wchar_t*> keys)
        {
            for (const wchar_t* key : keys)
            {
                if (const JsonValue* value = object.find(key); value != nullptr && value->isString())
                {
                    const std::wstring text = trim(value->asString());
                    if (!text.empty())
                    {
                        return text;
                    }
                }
            }

            return {};
        }

        long long readIntegerCandidate(const JsonValue& object, std::initializer_list<const wchar_t*> keys)
        {
            for (const wchar_t* key : keys)
            {
                const JsonValue* value = object.find(key);
                if (value == nullptr)
                {
                    continue;
                }

                try
                {
                    if (value->isNumber())
                    {
                        return std::stoll(value->asNumber());
                    }
                    if (value->isString())
                    {
                        return std::stoll(value->asString());
                    }
                }
                catch (const std::exception&)
                {
                }
            }

            return 0;
        }

        std::wstring extractVersionFromFileObject(const JsonValue& object)
        {
            return readStringCandidate(
                object,
                {L"version", L"Version", L"mod_version", L"file_version", L"fileVersion"});
        }

        bool isOldOrArchivedFile(const JsonValue& object)
        {
            const std::wstring category = toLower(readStringCandidate(
                object,
                {L"category_name", L"categoryName", L"category"}));
            return category.find(L"old") != std::wstring::npos ||
                category.find(L"archiv") != std::wstring::npos ||
                category.find(L"delete") != std::wstring::npos;
        }

        bool sameRemoteFile(const JsonValue& object, std::wstring_view fileId)
        {
            return !fileId.empty() &&
                std::to_wstring(readIntegerCandidate(object, {L"file_id", L"fileId", L"id"})) == fileId;
        }

        const JsonValue* filesArrayOrNull(const JsonValue& root)
        {
            if (root.isArray())
            {
                return &root;
            }

            if (!root.isObject())
            {
                return nullptr;
            }

            if (const JsonValue* files = root.find(L"files"); files != nullptr && files->isArray())
            {
                return files;
            }

            if (const JsonValue* files = root.find(L"Files"); files != nullptr && files->isArray())
            {
                return files;
            }

            return nullptr;
        }

        NexusLatestFile selectLatestFile(const JsonValue& root, const ModSourceRecord& source, std::wstring payloadJson)
        {
            NexusLatestFile latest;
            latest.payloadJson = std::move(payloadJson);

            const JsonValue* files = filesArrayOrNull(root);
            if (files == nullptr)
            {
                if (root.isObject())
                {
                    latest.version = extractVersionFromFileObject(root);
                    latest.fileId = readStringCandidate(root, {L"file_id", L"fileId", L"id"});
                }
                return latest;
            }

            std::wstring installedCategory;
            for (const JsonValue& item : files->asArray())
            {
                if (!item.isObject() || !sameRemoteFile(item, source.remoteFileId))
                {
                    continue;
                }

                installedCategory = readStringCandidate(item, {L"category_name", L"categoryName", L"category"});
                if (!installedCategory.empty())
                {
                    break;
                }
            }

            const JsonValue* best = nullptr;
            long long bestScore = -1;
            for (const JsonValue& item : files->asArray())
            {
                if (!item.isObject() || isOldOrArchivedFile(item))
                {
                    continue;
                }

                if (!installedCategory.empty())
                {
                    const std::wstring category = readStringCandidate(item, {L"category_name", L"categoryName", L"category"});
                    if (!equalsIgnoreCase(category, installedCategory))
                    {
                        continue;
                    }
                }

                const long long uploaded = readIntegerCandidate(
                    item,
                    {L"uploaded_timestamp", L"uploadedTimestamp", L"uploaded_time", L"uploadedTime"});
                const long long fileId = readIntegerCandidate(item, {L"file_id", L"fileId", L"id"});
                const long long score = uploaded > 0 ? uploaded : fileId;
                if (best == nullptr || score > bestScore)
                {
                    best = &item;
                    bestScore = score;
                }
            }

            if (best == nullptr)
            {
                for (const JsonValue& item : files->asArray())
                {
                    if (item.isObject() && sameRemoteFile(item, source.remoteFileId))
                    {
                        best = &item;
                        break;
                    }
                }
            }

            if (best != nullptr)
            {
                latest.version = extractVersionFromFileObject(*best);
                latest.fileId = std::to_wstring(readIntegerCandidate(*best, {L"file_id", L"fileId", L"id"}));
            }

            return latest;
        }

        NexusLatestFile fetchLatestNexusFile(
            const InstalledModRecord& mod,
            const AppSettingsService& settings)
        {
            if (!canCheckNexusUpdates(mod))
            {
                return {};
            }

#ifndef _WIN32
            (void)settings;
            return {};
#else
            const std::wstring authHeader = buildNexusAuthHeader(settings);
            if (authHeader.empty())
            {
                throw std::runtime_error(nexusAuthUnavailableMessage(settings));
            }

            const std::wstring endpoint =
                L"https://api.nexusmods.com/v1/games/" + percentEncode(mod.source.gameDomain) +
                L"/mods/" + percentEncode(mod.source.remoteModId) +
                L"/files.json";
            const std::string body = winHttpGet(endpoint, authHeader);
            const std::wstring payload = fromUtf8(body);
            const JsonValue root = JsonReader::parse(payload);
            return selectLatestFile(root, mod.source, payload);
#endif
        }

        bool isUnknownVersion(std::wstring_view value)
        {
            const std::wstring normalized = toLower(trim(std::wstring(value)));
            return normalized.empty() || normalized == L"unknown";
        }

        bool hasUpdate(const InstalledModRecord& mod)
        {
            return !isUnknownVersion(mod.version) &&
                !isUnknownVersion(mod.source.latestVersion) &&
                !equalsIgnoreCase(mod.version, mod.source.latestVersion);
        }

        std::wstring updateStatusText(const InstalledModRecord& mod)
        {
            if (!canCheckNexusUpdates(mod))
            {
                return mod.source.provider.empty() || equalsIgnoreCase(mod.source.provider, L"local")
                    ? L"Локальный мод"
                    : L"Ручной источник";
            }

            if (mod.source.lastCheckedAt.empty())
            {
                return L"Не проверялся";
            }

            if (isUnknownVersion(mod.source.latestVersion))
            {
                return L"Проверено";
            }

            if (isUnknownVersion(mod.version))
            {
                return L"Последняя: " + mod.source.latestVersion;
            }

            return hasUpdate(mod)
                ? L"Доступно: " + mod.source.latestVersion
                : L"Актуально";
        }

        std::wstring conflictStatusText(const ModFileSummary& summary)
        {
            if (summary.fileCount < 0)
            {
                return L"Файлы не просканированы";
            }
            if (summary.fileCount == 0)
            {
                return L"Файлов нет";
            }
            if (summary.conflictingFileCount == 0)
            {
                return L"Конфликтов нет";
            }

            return std::to_wstring(summary.conflictingFileCount) +
                L" конфликтных; перекрывает " +
                std::to_wstring(summary.overwritingFileCount) +
                L", перекрыт " +
                std::to_wstring(summary.overwrittenFileCount);
        }

        InstalledModEntry entryFromRecord(
            const InstalledModRecord& mod,
            const ModFileSummary& summary)
        {
            return InstalledModEntry{
                mod.path,
                mod.displayName.empty() ? mod.folderName : mod.displayName,
                isUnknownVersion(mod.version) ? L"Unknown" : mod.version,
                mod.source.latestVersion,
                mod.source.lastCheckedAt,
                updateStatusText(mod),
                conflictStatusText(summary),
                summary.fileCount,
                summary.conflictingFileCount,
                summary.overwrittenFileCount,
                summary.overwritingFileCount,
                mod.state != L"disabled",
                canCheckNexusUpdates(mod),
                hasUpdate(mod)
            };
        }

        std::wstring normalizedPathText(const std::filesystem::path& path)
        {
            return toLower(std::filesystem::weakly_canonical(path).wstring());
        }

        bool isPathInsideDirectory(
            const std::filesystem::path& candidate,
            const std::filesystem::path& directory)
        {
            std::wstring candidateText = normalizedPathText(candidate);
            std::wstring directoryText = normalizedPathText(directory);
            if (candidateText == directoryText)
            {
                return false;
            }

            if (!directoryText.empty() &&
                directoryText.back() != L'\\' &&
                directoryText.back() != L'/')
            {
                directoryText.push_back(std::filesystem::path::preferred_separator);
            }

            return candidateText.starts_with(directoryText);
        }

        std::filesystem::path nativeDeletePath(const std::filesystem::path& path)
        {
#ifdef _WIN32
            std::wstring text = std::filesystem::absolute(path).lexically_normal().wstring();
            if (text.rfind(LR"(\\?\)", 0) == 0)
            {
                return std::filesystem::path(text);
            }

            if (text.rfind(LR"(\\)", 0) == 0)
            {
                return std::filesystem::path(LR"(\\?\UNC\)" + text.substr(2));
            }

            return std::filesystem::path(LR"(\\?\)" + text);
#else
            return path;
#endif
        }

        void clearReadonlyAttribute(const std::filesystem::path& path)
        {
#ifdef _WIN32
            const std::filesystem::path nativePath = nativeDeletePath(path);
            const DWORD attributes = GetFileAttributesW(nativePath.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_READONLY) == 0)
            {
                return;
            }

            SetFileAttributesW(nativePath.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
#else
            (void)path;
#endif
        }

        void removePathWithRetry(const std::filesystem::path& path)
        {
            constexpr int maxAttempts = 3;

            for (int attempt = 0; attempt < maxAttempts; ++attempt)
            {
                clearReadonlyAttribute(path);
                const std::filesystem::path nativePath = nativeDeletePath(path);
                std::error_code removeError;
                const bool removed = std::filesystem::remove(nativePath, removeError);
                std::error_code existsError;
                if (!removeError &&
                    (removed || !std::filesystem::exists(nativePath, existsError)))
                {
                    return;
                }

                if (attempt + 1 < maxAttempts)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(80));
                    continue;
                }

                const std::string reason = removeError
                    ? removeError.message()
                    : "path still exists";
                throw std::runtime_error(
                    "Failed to delete \"" + toUtf8(path.wstring()) + "\": " + reason);
            }
        }

        std::size_t pathDepth(const std::filesystem::path& path)
        {
            return static_cast<std::size_t>(
                std::distance(path.begin(), path.end()));
        }

        void sortDirectoriesDeepestFirst(std::vector<std::filesystem::path>& directories)
        {
            std::sort(directories.begin(), directories.end(), [](const auto& left, const auto& right)
            {
                const std::size_t leftDepth = pathDepth(left);
                const std::size_t rightDepth = pathDepth(right);
                if (leftDepth != rightDepth)
                {
                    return leftDepth > rightDepth;
                }

                return left.wstring().size() > right.wstring().size();
            });
        }

        void removeModFilesystemPath(const std::filesystem::path& modPath)
        {
            const std::filesystem::path nativeRoot = nativeDeletePath(modPath);
            std::error_code statusError;
            const std::filesystem::file_status rootStatus = std::filesystem::symlink_status(nativeRoot, statusError);
            if (statusError || !std::filesystem::exists(rootStatus))
            {
                return;
            }

            const bool isRootDirectory = std::filesystem::is_directory(rootStatus) &&
                !std::filesystem::is_symlink(rootStatus);
            if (!isRootDirectory)
            {
                removePathWithRetry(nativeRoot);
                return;
            }

            clearReadonlyAttribute(nativeRoot);

            std::vector<std::filesystem::path> files;
            std::vector<std::filesystem::path> directories;
            std::error_code iterateError;
            std::filesystem::recursive_directory_iterator iterator(
                nativeRoot,
                std::filesystem::directory_options::skip_permission_denied,
                iterateError);
            if (iterateError)
            {
                throw std::runtime_error("Failed to scan mod directory for deletion: " + iterateError.message());
            }

            const std::filesystem::recursive_directory_iterator end;
            for (; iterator != end; iterator.increment(iterateError))
            {
                if (iterateError)
                {
                    throw std::runtime_error("Failed to scan mod directory for deletion: " + iterateError.message());
                }

                const std::filesystem::path current = iterator->path();
                std::error_code entryError;
                const std::filesystem::file_status status = iterator->symlink_status(entryError);
                if (entryError)
                {
                    throw std::runtime_error("Failed to inspect mod item for deletion: " + entryError.message());
                }

                if (std::filesystem::is_directory(status) && !std::filesystem::is_symlink(status))
                {
                    directories.push_back(current);
                }
                else
                {
                    files.push_back(current);
                }
            }

            for (const std::filesystem::path& file : files)
            {
                removePathWithRetry(file);
            }

            sortDirectoriesDeepestFirst(directories);
            for (const std::filesystem::path& directory : directories)
            {
                removePathWithRetry(directory);
            }

            removePathWithRetry(nativeRoot);
        }

        ModFileSummary deferredFileSummary()
        {
            ModFileSummary summary;
            summary.fileCount = -1;
            return summary;
        }

    }

    ModService::ModService(
        Logger& logger,
        AppSettingsService& settings,
        const BuildPathSettingsService& pathSettings) noexcept
        : logger_(logger),
          settings_(settings),
          pathSettings_(pathSettings)
    {
    }

    void ModService::initialize()
    {
        if (initialized_)
        {
            return;
        }

        initialized_ = true;
        logger_.write(LogLevel::Info, "Mod service initialized.");
    }

    void ModService::shutdown()
    {
        if (!initialized_)
        {
            return;
        }

        mods_.clear();
        logger_.write(LogLevel::Info, "Mod service shut down.");
        initialized_ = false;
    }

    void ModService::registerMod(ModDescriptor descriptor)
    {
        mods_.push_back(std::move(descriptor));
    }

    const std::vector<ModDescriptor>& ModService::mods() const noexcept
    {
        return mods_;
    }

    std::vector<InstalledModEntry> ModService::listInstalledMods(
        const std::filesystem::path& projectDirectory) const
    {
        std::vector<InstalledModEntry> entries;
        const std::filesystem::path modsDirectory = pathSettings_.modsDirectory(projectDirectory);
        for (const InstalledModRecord& mod : InstanceMetadataStore::listInstalledMods(projectDirectory, modsDirectory))
        {
            entries.push_back(entryFromRecord(
                mod,
                deferredFileSummary()));
        }

        return entries;
    }

    std::vector<InstalledModEntry> ModService::checkInstalledModUpdates(
        const std::filesystem::path& projectDirectory) const
    {
        if (projectDirectory.empty())
        {
            throw std::invalid_argument("Project directory is required.");
        }

        const std::filesystem::path modsDirectory = pathSettings_.modsDirectory(projectDirectory);
        const std::vector<InstalledModRecord> mods =
            InstanceMetadataStore::listInstalledMods(projectDirectory, modsDirectory);
        int checkableCount = 0;
        int checkedCount = 0;
        std::string firstError;
        for (const InstalledModRecord& mod : mods)
        {
            if (!canCheckNexusUpdates(mod))
            {
                continue;
            }

            ++checkableCount;
            try
            {
                NexusLatestFile latest = fetchLatestNexusFile(mod, settings_);
                InstanceMetadataStore::recordRemoteCheck(
                    projectDirectory,
                    RemoteCheckRecord{
                        mod.folderName,
                        mod.source,
                        latest.version,
                        latest.payloadJson,
                        nowUtcText()
                    },
                    modsDirectory);
                ++checkedCount;
            }
            catch (const std::exception& exception)
            {
                if (firstError.empty())
                {
                    firstError = exception.what();
                }
                logger_.write(LogLevel::Warning, std::string("Failed to check Nexus update for mod: ") + exception.what());
            }
        }

        if (checkableCount > 0 && checkedCount == 0 && !firstError.empty())
        {
            throw std::runtime_error(firstError);
        }

        return listInstalledMods(projectDirectory);
    }

    std::vector<ModFileTreeEntry> ModService::listModFileTree(
        const std::filesystem::path& projectDirectory,
        const std::filesystem::path& modPath,
        std::wstring_view relativeDirectory) const
    {
        return InstanceMetadataStore::listModFileTree(
            projectDirectory,
            modPath,
            relativeDirectory,
            pathSettings_.modsDirectory(projectDirectory));
    }

    void ModService::deleteInstalledMod(
        const std::filesystem::path& projectDirectory,
        const std::filesystem::path& modPath) const
    {
        if (projectDirectory.empty() || modPath.empty())
        {
            throw std::invalid_argument("Project directory and mod path are required.");
        }

        const std::filesystem::path directory = pathSettings_.modsDirectory(projectDirectory);
        if (!std::filesystem::exists(modPath))
        {
            throw std::invalid_argument("Mod does not exist.");
        }

        if (!std::filesystem::exists(directory) || !isPathInsideDirectory(modPath, directory))
        {
            throw std::invalid_argument("Mod path is outside the project mods directory.");
        }

        try
        {
            removeModFilesystemPath(modPath);
        }
        catch (const std::exception& exception)
        {
            logger_.write(
                LogLevel::Warning,
                "ModDelete",
                "Failed to delete installed mod path=\"" + toUtf8(modPath.wstring()) +
                    "\", error=\"" + exception.what() + "\"");
            throw;
        }

        InstanceMetadataStore::deleteInstalledMod(projectDirectory, modPath);
        logger_.write(
            LogLevel::Info,
            "ModDelete",
            "Deleted installed mod path=\"" + toUtf8(modPath.wstring()) + "\"");
    }

    void ModService::setInstalledModEnabled(
        const std::filesystem::path& projectDirectory,
        const std::filesystem::path& modPath,
        bool isEnabled) const
    {
        if (projectDirectory.empty() || modPath.empty())
        {
            throw std::invalid_argument("Project directory and mod path are required.");
        }

        const std::filesystem::path directory = pathSettings_.modsDirectory(projectDirectory);
        if (!std::filesystem::exists(modPath))
        {
            throw std::invalid_argument("Mod does not exist.");
        }

        if (!std::filesystem::exists(directory) || !isPathInsideDirectory(modPath, directory))
        {
            throw std::invalid_argument("Mod path is outside the project mods directory.");
        }

        InstanceMetadataStore::setInstalledModEnabled(projectDirectory, modPath, isEnabled);
    }

    void ModService::setAllInstalledModsEnabled(
        const std::filesystem::path& projectDirectory,
        bool isEnabled) const
    {
        if (projectDirectory.empty())
        {
            throw std::invalid_argument("Project directory is required.");
        }

        InstanceMetadataStore::setAllInstalledModsEnabled(
            projectDirectory,
            isEnabled,
            pathSettings_.modsDirectory(projectDirectory));
    }

    bool ModService::isInitialized() const noexcept
    {
        return initialized_;
    }
}
