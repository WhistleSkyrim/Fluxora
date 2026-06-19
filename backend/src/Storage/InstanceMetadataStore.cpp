#include "FluxoraCore/Storage/InstanceMetadataStore.hpp"

#include "FluxoraCore/Storage/AtomicFileStore.hpp"
#include "FluxoraCore/Support/JsonReader.hpp"
#include "FluxoraCore/Support/JsonWriter.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <cwctype>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

#ifndef _WIN32

namespace fluxora
{
    void InstanceMetadataStore::ensureInstance(
        const std::filesystem::path&,
        std::wstring_view)
    {
        throw std::runtime_error("Fluxora instance metadata storage requires SQLite on Windows.");
    }

    std::wstring InstanceMetadataStore::gameId(
        const std::filesystem::path&)
    {
        throw std::runtime_error("Fluxora instance metadata storage requires SQLite on Windows.");
    }

    std::vector<InstalledModRecord> InstanceMetadataStore::listInstalledMods(
        const std::filesystem::path&,
        const std::filesystem::path&)
    {
        throw std::runtime_error("Fluxora instance metadata storage requires SQLite on Windows.");
    }

    std::vector<ProfileOrderItemRecord> InstanceMetadataStore::listProfileOrderItems(
        const std::filesystem::path&,
        std::wstring_view,
        const std::filesystem::path&)
    {
        throw std::runtime_error("Fluxora instance metadata storage requires SQLite on Windows.");
    }

    std::vector<ProfileOrderItemRecord> InstanceMetadataStore::createProfileOrderSeparator(
        const std::filesystem::path&,
        std::wstring_view,
        std::wstring_view,
        int,
        const std::filesystem::path&)
    {
        throw std::runtime_error("Fluxora instance metadata storage requires SQLite on Windows.");
    }

    std::vector<ProfileOrderItemRecord> InstanceMetadataStore::deleteProfileOrderSeparator(
        const std::filesystem::path&,
        std::wstring_view,
        std::wstring_view,
        const std::filesystem::path&)
    {
        throw std::runtime_error("Fluxora instance metadata storage requires SQLite on Windows.");
    }

    std::vector<ProfileOrderItemRecord> InstanceMetadataStore::moveProfileOrderItem(
        const std::filesystem::path&,
        std::wstring_view,
        std::wstring_view,
        int,
        const std::filesystem::path&)
    {
        throw std::runtime_error("Fluxora instance metadata storage requires SQLite on Windows.");
    }

    void InstanceMetadataStore::replaceProfileOrderItems(
        const std::filesystem::path&,
        std::wstring_view,
        const std::vector<ProfileOrderImportItemRecord>&)
    {
        throw std::runtime_error("Fluxora instance metadata storage requires SQLite on Windows.");
    }

    std::vector<ProfilePluginOrderItemRecord> InstanceMetadataStore::listProfilePluginOrderItems(
        const std::filesystem::path&,
        std::wstring_view,
        const std::vector<std::wstring>&)
    {
        throw std::runtime_error("Fluxora instance metadata storage requires SQLite on Windows.");
    }

    void InstanceMetadataStore::replaceProfilePluginOrderItems(
        const std::filesystem::path&,
        std::wstring_view,
        const std::vector<ProfilePluginOrderImportItemRecord>&)
    {
        throw std::runtime_error("Fluxora instance metadata storage requires SQLite on Windows.");
    }

    std::vector<ProfilePluginOrderItemRecord> InstanceMetadataStore::createProfilePluginOrderSeparator(
        const std::filesystem::path&,
        std::wstring_view,
        const std::vector<std::wstring>&,
        std::wstring_view,
        int)
    {
        throw std::runtime_error("Fluxora instance metadata storage requires SQLite on Windows.");
    }

    std::vector<ProfilePluginOrderItemRecord> InstanceMetadataStore::deleteProfilePluginOrderSeparator(
        const std::filesystem::path&,
        std::wstring_view,
        const std::vector<std::wstring>&,
        std::wstring_view)
    {
        throw std::runtime_error("Fluxora instance metadata storage requires SQLite on Windows.");
    }

    std::vector<ProfilePluginOrderItemRecord> InstanceMetadataStore::moveProfilePluginOrderItem(
        const std::filesystem::path&,
        std::wstring_view,
        const std::vector<std::wstring>&,
        std::wstring_view,
        int)
    {
        throw std::runtime_error("Fluxora instance metadata storage requires SQLite on Windows.");
    }

    InstalledModRecord InstanceMetadataStore::registerInstalledMod(
        const std::filesystem::path&,
        const std::filesystem::path&,
        std::wstring_view,
        std::wstring_view,
        const ModSourceRecord&)
    {
        throw std::runtime_error("Fluxora instance metadata storage requires SQLite on Windows.");
    }

    void InstanceMetadataStore::deleteInstalledMod(
        const std::filesystem::path&,
        const std::filesystem::path&)
    {
        throw std::runtime_error("Fluxora instance metadata storage requires SQLite on Windows.");
    }

    void InstanceMetadataStore::recordRemoteCheck(
        const std::filesystem::path&,
        const RemoteCheckRecord&,
        const std::filesystem::path&)
    {
        throw std::runtime_error("Fluxora instance metadata storage requires SQLite on Windows.");
    }

    ModFileSummary InstanceMetadataStore::summarizeModFiles(
        const std::filesystem::path&,
        const std::filesystem::path&,
        const std::filesystem::path&)
    {
        throw std::runtime_error("Fluxora instance metadata storage requires SQLite on Windows.");
    }

    std::vector<ModFileSummaryRecord> InstanceMetadataStore::summarizeInstalledModFiles(
        const std::filesystem::path&,
        const std::filesystem::path&)
    {
        throw std::runtime_error("Fluxora instance metadata storage requires SQLite on Windows.");
    }

    std::vector<ModFileSummaryRecord> InstanceMetadataStore::summarizeProfileModFiles(
        const std::filesystem::path&,
        std::wstring_view,
        const std::filesystem::path&)
    {
        throw std::runtime_error("Fluxora instance metadata storage requires SQLite on Windows.");
    }

    std::vector<ModFileTreeEntry> InstanceMetadataStore::listModFileTree(
        const std::filesystem::path&,
        const std::filesystem::path&,
        std::wstring_view,
        const std::filesystem::path&)
    {
        throw std::runtime_error("Fluxora instance metadata storage requires SQLite on Windows.");
    }
}

#else

struct sqlite3;
struct sqlite3_stmt;

namespace fluxora
{
    namespace
    {
        constexpr int sqliteOk = 0;
        constexpr int sqliteRow = 100;
        constexpr int sqliteDone = 101;
        constexpr std::wstring_view manifestDirectoryName = L".flow";
        constexpr std::wstring_view manifestFileName = L"manifest.json";
        constexpr std::wstring_view fallbackProfileName = L"Default";
        constexpr std::wstring_view profileOrderModKind = L"mod";
        constexpr std::wstring_view profileOrderSeparatorKind = L"separator";
        constexpr std::wstring_view profilePluginOrderPluginKind = L"plugin";
        constexpr std::wstring_view profilePluginOrderSeparatorKind = L"separator";

        using SqliteDestructor = void (*)(void*);

        std::mutex& metadataStoreMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        std::string toUtf8(const std::wstring& value)
        {
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
            if (size <= 0)
            {
                throw std::runtime_error("Failed to encode text as UTF-8.");
            }

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
        }

        std::wstring fromUtf8(const std::string& value)
        {
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
                throw std::invalid_argument("Metadata manifest is not valid UTF-8.");
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
        }

        std::string readTextFile(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::in | std::ios::binary);
            if (!file)
            {
                throw std::runtime_error("Failed to open metadata manifest.");
            }

            return std::string(
                std::istreambuf_iterator<char>(file),
                std::istreambuf_iterator<char>());
        }

        void recoverMetadataManifest(const std::filesystem::path& path)
        {
            static_cast<void>(AtomicFileStore().recoverFile(
                path,
                AtomicFileWriteOptions{
                    L"generated mod metadata",
                    ProjectStateValidation::JsonObject
                }));
        }

        void writeTextFile(const std::filesystem::path& path, const std::string& content)
        {
            AtomicFileStore().writeTextFile(
                path,
                content,
                AtomicFileWriteOptions{
                    L"generated mod metadata",
                    ProjectStateValidation::JsonObject,
                    {},
                    false
                });
        }

        std::wstring nowUtcText()
        {
            const auto now = std::chrono::system_clock::now();
            const std::time_t time = std::chrono::system_clock::to_time_t(now);

            std::tm utc{};
            gmtime_s(&utc, &time);

            std::wostringstream stream;
            stream << std::put_time(&utc, L"%Y-%m-%dT%H:%M:%SZ");
            return stream.str();
        }

        std::wstring generateUuid()
        {
            std::array<unsigned char, 16> bytes{};
            std::random_device random;
            for (auto& byte : bytes)
            {
                byte = static_cast<unsigned char>(random());
            }

            bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40);
            bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);

            std::wostringstream stream;
            stream << std::hex << std::setfill(L'0');
            for (std::size_t index = 0; index < bytes.size(); ++index)
            {
                if (index == 4 || index == 6 || index == 8 || index == 10)
                {
                    stream << L'-';
                }

                stream << std::setw(2) << static_cast<int>(bytes[index]);
            }

            return stream.str();
        }

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
            const auto first = value.find_first_not_of(L" \t\r\n");
            if (first == std::wstring::npos)
            {
                return {};
            }

            const auto last = value.find_last_not_of(L" \t\r\n");
            return value.substr(first, last - first + 1);
        }

        std::wstring profileNameOrDefault(std::wstring_view profileName)
        {
            std::wstring normalized = trim(std::wstring(profileName));
            return normalized.empty() ? std::wstring(fallbackProfileName) : normalized;
        }

        std::wstring normalizeRelativePath(const std::filesystem::path& path)
        {
            return path.generic_wstring();
        }

        std::wstring pathKey(std::wstring_view relativePath)
        {
            return toLower(std::wstring(relativePath));
        }

        std::filesystem::path instanceDatabasePath(const std::filesystem::path& projectDirectory)
        {
            return projectDirectory / L"instance.db";
        }

        std::filesystem::path modsDirectory(
            const std::filesystem::path& projectDirectory,
            const std::filesystem::path& configuredDirectory = {})
        {
            return configuredDirectory.empty()
                ? projectDirectory / L"mods"
                : configuredDirectory;
        }

        std::filesystem::path manifestPathForMod(const std::filesystem::path& modDirectory)
        {
            return modDirectory / std::filesystem::path(manifestDirectoryName) /
                std::filesystem::path(manifestFileName);
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

        ModSourceRecord readSourceFromManifest(const JsonValue& object)
        {
            const JsonValue* source = object.find(L"source");
            if (source == nullptr || !source->isObject())
            {
                return {};
            }

            return ModSourceRecord{
                readStringOrDefault(*source, L"provider"),
                readStringOrDefault(*source, L"gameDomain"),
                readStringOrDefault(*source, L"remoteModId"),
                readStringOrDefault(*source, L"remoteFileId"),
                readStringOrDefault(*source, L"url"),
                readStringOrDefault(*source, L"lastCheckedAt"),
                readStringOrDefault(*source, L"latestVersion")
            };
        }

        std::optional<InstalledModRecord> readManifestRecord(
            const std::filesystem::path& modDirectory)
        {
            const std::filesystem::path path = manifestPathForMod(modDirectory);
            if (!std::filesystem::exists(path))
            {
                return std::nullopt;
            }

            recoverMetadataManifest(path);
            const JsonValue root = JsonReader::parse(fromUtf8(readTextFile(path)));
            if (!root.isObject())
            {
                return std::nullopt;
            }

            InstalledModRecord record;
            record.uuid = readStringOrDefault(root, L"modUuid");
            record.gameId = readStringOrDefault(root, L"gameId");
            record.folderName = readStringOrDefault(root, L"folderName", modDirectory.filename().wstring());
            record.displayName = readStringOrDefault(root, L"displayName", record.folderName);
            record.version = readStringOrDefault(root, L"version");
            record.installedAt = readStringOrDefault(root, L"installedAt");
            record.updatedAt = readStringOrDefault(root, L"updatedAt");
            record.state = readStringOrDefault(root, L"state", L"installed");
            record.contentFingerprint = readStringOrDefault(root, L"contentFingerprint");
            record.path = modDirectory;
            record.source = readSourceFromManifest(root);
            return record;
        }

        bool portableManifestNeedsWrite(const InstalledModRecord& record, bool stateChanged)
        {
            if (stateChanged)
            {
                return true;
            }

            try
            {
                const std::optional<InstalledModRecord> manifestRecord = readManifestRecord(record.path);
                return !manifestRecord.has_value() || manifestRecord->state != record.state;
            }
            catch (const std::exception&)
            {
                return true;
            }
        }

        bool portableManifestIsMissing(const InstalledModRecord& record)
        {
            std::error_code error;
            return !std::filesystem::is_regular_file(manifestPathForMod(record.path), error);
        }

        bool portableManifestNeedsBulkWrite(const InstalledModRecord& record, bool stateChanged)
        {
            if (portableManifestIsMissing(record))
            {
                return true;
            }

            if (stateChanged)
            {
                return false;
            }

            return portableManifestNeedsWrite(record, false);
        }

        void mixHash(std::uint64_t& hash, std::uint64_t value)
        {
            hash ^= value;
            hash *= 1099511628211ULL;
        }

        void mixHash(std::uint64_t& hash, std::wstring_view value)
        {
            for (wchar_t character : value)
            {
                mixHash(hash, static_cast<std::uint64_t>(character));
            }
        }

        std::wstring computeContentFingerprint(const std::filesystem::path& modDirectory)
        {
            std::uint64_t hash = 1469598103934665603ULL;
            std::uintmax_t fileCount = 0;
            std::uintmax_t totalBytes = 0;

            if (!std::filesystem::exists(modDirectory))
            {
                return {};
            }

            std::error_code error;
            std::filesystem::recursive_directory_iterator iterator(
                modDirectory,
                std::filesystem::directory_options::skip_permission_denied,
                error);
            const std::filesystem::recursive_directory_iterator end;

            while (!error && iterator != end)
            {
                const std::filesystem::path current = iterator->path();
                if (iterator->is_directory(error) && current.filename().wstring() == manifestDirectoryName)
                {
                    iterator.disable_recursion_pending();
                    iterator.increment(error);
                    continue;
                }

                if (iterator->is_regular_file(error))
                {
                    ++fileCount;
                    const auto relative = std::filesystem::relative(current, modDirectory, error);
                    mixHash(hash, error ? current.wstring() : relative.generic_wstring());

                    const std::uintmax_t size = iterator->file_size(error);
                    if (!error)
                    {
                        totalBytes += size;
                        mixHash(hash, size);
                    }

                    const auto writeTime = iterator->last_write_time(error);
                    if (!error)
                    {
                        mixHash(
                            hash,
                            static_cast<std::uint64_t>(
                                writeTime.time_since_epoch().count()));
                    }
                }

                iterator.increment(error);
            }

            std::wostringstream stream;
            stream << L"v1:" << fileCount << L":" << totalBytes << L":"
                   << std::hex << std::setw(16) << std::setfill(L'0') << hash;
            return stream.str();
        }

        std::wstring normalizeProvider(ModSourceRecord source)
        {
            if (!source.provider.empty())
            {
                return source.provider;
            }

            if (!source.gameDomain.empty() || !source.remoteModId.empty() || !source.remoteFileId.empty())
            {
                return L"nexus";
            }

            return source.url.empty() ? L"local" : L"manual";
        }

        class SqliteApi final
        {
        public:
            using Open16Fn = int (__cdecl *)(const void*, sqlite3**);
            using CloseFn = int (__cdecl *)(sqlite3*);
            using ExecFn = int (__cdecl *)(sqlite3*, const char*, int (*)(void*, int, char**, char**), void*, char**);
            using PrepareFn = int (__cdecl *)(sqlite3*, const char*, int, sqlite3_stmt**, const char**);
            using StepFn = int (__cdecl *)(sqlite3_stmt*);
            using FinalizeFn = int (__cdecl *)(sqlite3_stmt*);
            using BindText16Fn = int (__cdecl *)(sqlite3_stmt*, int, const void*, int, SqliteDestructor);
            using BindIntFn = int (__cdecl *)(sqlite3_stmt*, int, int);
            using BindInt64Fn = int (__cdecl *)(sqlite3_stmt*, int, long long);
            using BindNullFn = int (__cdecl *)(sqlite3_stmt*, int);
            using ColumnText16Fn = const void* (__cdecl *)(sqlite3_stmt*, int);
            using ColumnBytes16Fn = int (__cdecl *)(sqlite3_stmt*, int);
            using ColumnIntFn = int (__cdecl *)(sqlite3_stmt*, int);
            using ColumnInt64Fn = long long (__cdecl *)(sqlite3_stmt*, int);
            using LastInsertRowIdFn = long long (__cdecl *)(sqlite3*);
            using ErrmsgFn = const char* (__cdecl *)(sqlite3*);
            using BusyTimeoutFn = int (__cdecl *)(sqlite3*, int);
            using FreeFn = void (__cdecl *)(void*);

            SqliteApi()
            {
                module_ = LoadLibraryW(L"winsqlite3.dll");
                if (module_ == nullptr)
                {
                    throw std::runtime_error("winsqlite3.dll is not available.");
                }

                open16 = load<Open16Fn>("sqlite3_open16");
                close = load<CloseFn>("sqlite3_close");
                exec = load<ExecFn>("sqlite3_exec");
                prepare = load<PrepareFn>("sqlite3_prepare_v2");
                step = load<StepFn>("sqlite3_step");
                finalize = load<FinalizeFn>("sqlite3_finalize");
                bindText16 = load<BindText16Fn>("sqlite3_bind_text16");
                bindInt = load<BindIntFn>("sqlite3_bind_int");
                bindInt64 = load<BindInt64Fn>("sqlite3_bind_int64");
                bindNull = load<BindNullFn>("sqlite3_bind_null");
                columnText16 = load<ColumnText16Fn>("sqlite3_column_text16");
                columnBytes16 = load<ColumnBytes16Fn>("sqlite3_column_bytes16");
                columnInt = load<ColumnIntFn>("sqlite3_column_int");
                columnInt64 = load<ColumnInt64Fn>("sqlite3_column_int64");
                lastInsertRowId = load<LastInsertRowIdFn>("sqlite3_last_insert_rowid");
                errmsg = load<ErrmsgFn>("sqlite3_errmsg");
                busyTimeout = load<BusyTimeoutFn>("sqlite3_busy_timeout");
                free = load<FreeFn>("sqlite3_free");
            }

            ~SqliteApi()
            {
                if (module_ != nullptr)
                {
                    FreeLibrary(module_);
                }
            }

            SqliteApi(const SqliteApi&) = delete;
            SqliteApi& operator=(const SqliteApi&) = delete;

            Open16Fn open16{};
            CloseFn close{};
            ExecFn exec{};
            PrepareFn prepare{};
            StepFn step{};
            FinalizeFn finalize{};
            BindText16Fn bindText16{};
            BindIntFn bindInt{};
            BindInt64Fn bindInt64{};
            BindNullFn bindNull{};
            ColumnText16Fn columnText16{};
            ColumnBytes16Fn columnBytes16{};
            ColumnIntFn columnInt{};
            ColumnInt64Fn columnInt64{};
            LastInsertRowIdFn lastInsertRowId{};
            ErrmsgFn errmsg{};
            BusyTimeoutFn busyTimeout{};
            FreeFn free{};

        private:
            template <typename T>
            T load(const char* name)
            {
                FARPROC address = GetProcAddress(module_, name);
                if (address == nullptr)
                {
                    throw std::runtime_error("winsqlite3.dll is missing a required SQLite entry point.");
                }

                return reinterpret_cast<T>(address);
            }

            HMODULE module_{nullptr};
        };

        SqliteApi& sqlite()
        {
            static SqliteApi api;
            return api;
        }

        constexpr int sqliteBusyTimeoutMs = 15000;

        std::string sqliteError(sqlite3* handle)
        {
            const char* message = sqlite().errmsg(handle);
            return message == nullptr ? "SQLite error." : std::string(message);
        }

        class Statement final
        {
        public:
            Statement(sqlite3* handle, const char* sql)
                : handle_(handle)
            {
                const int result = sqlite().prepare(handle_, sql, -1, &statement_, nullptr);
                if (result != sqliteOk)
                {
                    throw std::runtime_error(sqliteError(handle_));
                }
            }

            ~Statement()
            {
                finalize();
            }

            Statement(const Statement&) = delete;
            Statement& operator=(const Statement&) = delete;

            void bindText(int index, std::wstring_view value)
            {
                const int result = sqlite().bindText16(
                    statement_,
                    index,
                    value.data(),
                    static_cast<int>(value.size() * sizeof(wchar_t)),
                    reinterpret_cast<SqliteDestructor>(-1));
                if (result != sqliteOk)
                {
                    throw std::runtime_error(sqliteError(handle_));
                }
            }

            void bindInt(int index, int value)
            {
                const int result = sqlite().bindInt(statement_, index, value);
                if (result != sqliteOk)
                {
                    throw std::runtime_error(sqliteError(handle_));
                }
            }

            void bindInt64(int index, std::int64_t value)
            {
                const int result = sqlite().bindInt64(statement_, index, static_cast<long long>(value));
                if (result != sqliteOk)
                {
                    throw std::runtime_error(sqliteError(handle_));
                }
            }

            void bindNull(int index)
            {
                const int result = sqlite().bindNull(statement_, index);
                if (result != sqliteOk)
                {
                    throw std::runtime_error(sqliteError(handle_));
                }
            }

            bool stepRow()
            {
                const int result = sqlite().step(statement_);
                if (result == sqliteRow)
                {
                    return true;
                }
                if (result == sqliteDone)
                {
                    finalize();
                    return false;
                }

                throw std::runtime_error(sqliteError(handle_));
            }

            void stepDone()
            {
                const int result = sqlite().step(statement_);
                if (result != sqliteDone)
                {
                    throw std::runtime_error(sqliteError(handle_));
                }

                finalize();
            }

            std::wstring columnText(int index) const
            {
                const void* text = sqlite().columnText16(statement_, index);
                if (text == nullptr)
                {
                    return {};
                }

                const int bytes = sqlite().columnBytes16(statement_, index);
                return std::wstring(
                    static_cast<const wchar_t*>(text),
                    static_cast<std::size_t>(bytes / sizeof(wchar_t)));
            }

            int columnInt(int index) const
            {
                return sqlite().columnInt(statement_, index);
            }

            std::int64_t columnInt64(int index) const
            {
                return static_cast<std::int64_t>(sqlite().columnInt64(statement_, index));
            }

            std::int64_t lastInsertRowId() const
            {
                return static_cast<std::int64_t>(sqlite().lastInsertRowId(handle_));
            }

        private:
            void finalize() noexcept
            {
                if (statement_ != nullptr)
                {
                    sqlite().finalize(statement_);
                    statement_ = nullptr;
                }
            }

            sqlite3* handle_{nullptr};
            sqlite3_stmt* statement_{nullptr};
        };

        class Database final
        {
        public:
            explicit Database(const std::filesystem::path& path)
            {
                const std::wstring text = path.wstring();
                const int result = sqlite().open16(text.c_str(), &handle_);
                if (result != sqliteOk)
                {
                    std::string message = handle_ == nullptr
                        ? "Failed to open instance database."
                        : sqliteError(handle_);
                    if (handle_ != nullptr)
                    {
                        sqlite().close(handle_);
                        handle_ = nullptr;
                    }

                    throw std::runtime_error(message);
                }

                const int timeoutResult = sqlite().busyTimeout(handle_, sqliteBusyTimeoutMs);
                if (timeoutResult != sqliteOk)
                {
                    std::string message = sqliteError(handle_);
                    sqlite().close(handle_);
                    handle_ = nullptr;
                    throw std::runtime_error(message);
                }
            }

            ~Database()
            {
                if (handle_ != nullptr)
                {
                    sqlite().close(handle_);
                }
            }

            Database(const Database&) = delete;
            Database& operator=(const Database&) = delete;
            Database(Database&& other) noexcept
                : handle_(std::exchange(other.handle_, nullptr))
            {
            }

            Database& operator=(Database&& other) noexcept
            {
                if (this != &other)
                {
                    if (handle_ != nullptr)
                    {
                        sqlite().close(handle_);
                    }

                    handle_ = std::exchange(other.handle_, nullptr);
                }

                return *this;
            }

            void exec(const char* sql)
            {
                char* error = nullptr;
                const int result = sqlite().exec(handle_, sql, nullptr, nullptr, &error);
                if (result != sqliteOk)
                {
                    std::string message = error == nullptr ? sqliteError(handle_) : std::string(error);
                    if (error != nullptr)
                    {
                        sqlite().free(error);
                    }

                    throw std::runtime_error(message);
                }
            }

            [[nodiscard]] Statement prepare(const char* sql)
            {
                return Statement(handle_, sql);
            }

        private:
            sqlite3* handle_{nullptr};
        };

        class Transaction final
        {
        public:
            explicit Transaction(Database& database)
                : database_(database)
            {
                database_.exec("BEGIN IMMEDIATE;");
            }

            ~Transaction()
            {
                if (!committed_)
                {
                    try
                    {
                        database_.exec("ROLLBACK;");
                    }
                    catch (...)
                    {
                    }
                }
            }

            Transaction(const Transaction&) = delete;
            Transaction& operator=(const Transaction&) = delete;

            void commit()
            {
                database_.exec("COMMIT;");
                committed_ = true;
            }

        private:
            Database& database_;
            bool committed_{false};
        };

        void ensureSchema(Database& database)
        {
            database.exec("PRAGMA busy_timeout = 15000;");
            database.exec("PRAGMA foreign_keys = ON;");
            database.exec("PRAGMA journal_mode = WAL;");
            database.exec("PRAGMA synchronous = NORMAL;");
            database.exec(
                "CREATE TABLE IF NOT EXISTS instance_metadata ("
                "key TEXT PRIMARY KEY NOT NULL,"
                "value TEXT NOT NULL"
                ");");
            database.exec(
                "CREATE TABLE IF NOT EXISTS mods ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "uuid TEXT NOT NULL UNIQUE,"
                "game_id TEXT NOT NULL DEFAULT '',"
                "folder_name TEXT NOT NULL UNIQUE,"
                "display_name TEXT NOT NULL,"
                "version TEXT NOT NULL DEFAULT '',"
                "installed_at TEXT NOT NULL,"
                "updated_at TEXT NOT NULL,"
                "state TEXT NOT NULL DEFAULT 'installed',"
                "content_fingerprint TEXT NOT NULL DEFAULT ''"
                ");");
            database.exec(
                "CREATE TABLE IF NOT EXISTS mod_sources ("
                "mod_id INTEGER PRIMARY KEY NOT NULL REFERENCES mods(id) ON DELETE CASCADE,"
                "provider TEXT NOT NULL DEFAULT 'manual',"
                "game_domain TEXT NOT NULL DEFAULT '',"
                "remote_mod_id TEXT NOT NULL DEFAULT '',"
                "remote_file_id TEXT NOT NULL DEFAULT '',"
                "url TEXT NOT NULL DEFAULT '',"
                "last_checked_at TEXT NOT NULL DEFAULT '',"
                "latest_version TEXT NOT NULL DEFAULT ''"
                ");");
            database.exec(
                "CREATE TABLE IF NOT EXISTS remote_cache ("
                "provider TEXT NOT NULL,"
                "game_domain TEXT NOT NULL DEFAULT '',"
                "remote_mod_id TEXT NOT NULL DEFAULT '',"
                "remote_file_id TEXT NOT NULL DEFAULT '',"
                "latest_version TEXT NOT NULL DEFAULT '',"
                "payload_json TEXT NOT NULL DEFAULT '',"
                "checked_at TEXT NOT NULL,"
                "PRIMARY KEY(provider, game_domain, remote_mod_id, remote_file_id)"
                ");");
            database.exec(
                "CREATE TABLE IF NOT EXISTS mod_file_cache ("
                "mod_id INTEGER NOT NULL REFERENCES mods(id) ON DELETE CASCADE,"
                "relative_path TEXT NOT NULL,"
                "parent_path TEXT NOT NULL DEFAULT '',"
                "path_key TEXT NOT NULL,"
                "parent_key TEXT NOT NULL DEFAULT '',"
                "name TEXT NOT NULL,"
                "kind TEXT NOT NULL,"
                "size INTEGER NOT NULL DEFAULT 0,"
                "modified_at TEXT NOT NULL DEFAULT '',"
                "PRIMARY KEY(mod_id, path_key)"
                ");");
            database.exec(
                "CREATE TABLE IF NOT EXISTS profile_order_items ("
                "id TEXT PRIMARY KEY NOT NULL,"
                "profile_name TEXT NOT NULL DEFAULT 'Default',"
                "kind TEXT NOT NULL CHECK(kind IN ('mod', 'separator')),"
                "mod_id INTEGER REFERENCES mods(id) ON DELETE CASCADE,"
                "separator_title TEXT NOT NULL DEFAULT '',"
                "position INTEGER NOT NULL DEFAULT 0,"
                "created_at TEXT NOT NULL,"
                "updated_at TEXT NOT NULL,"
                "CHECK((kind = 'mod' AND mod_id IS NOT NULL) OR (kind = 'separator' AND mod_id IS NULL)),"
                "UNIQUE(profile_name, mod_id)"
                ");");
            database.exec(
                "CREATE TABLE IF NOT EXISTS profile_plugin_order_items ("
                "id TEXT PRIMARY KEY NOT NULL,"
                "profile_name TEXT NOT NULL DEFAULT 'Default',"
                "kind TEXT NOT NULL CHECK(kind IN ('plugin', 'separator')),"
                "plugin_name TEXT NOT NULL DEFAULT '',"
                "separator_title TEXT NOT NULL DEFAULT '',"
                "position INTEGER NOT NULL DEFAULT 0,"
                "created_at TEXT NOT NULL,"
                "updated_at TEXT NOT NULL,"
                "CHECK((kind = 'plugin' AND plugin_name <> '') OR (kind = 'separator' AND plugin_name = ''))"
                ");");
            database.exec("CREATE INDEX IF NOT EXISTS idx_mods_state ON mods(state);");
            database.exec("CREATE INDEX IF NOT EXISTS idx_mods_display_name ON mods(display_name COLLATE NOCASE);");
            database.exec("CREATE INDEX IF NOT EXISTS idx_mod_sources_remote ON mod_sources(provider, game_domain, remote_mod_id, remote_file_id);");
            database.exec("CREATE INDEX IF NOT EXISTS idx_remote_cache_checked ON remote_cache(checked_at);");
            database.exec("CREATE INDEX IF NOT EXISTS idx_mod_file_cache_path ON mod_file_cache(path_key);");
            database.exec("CREATE INDEX IF NOT EXISTS idx_mod_file_cache_parent ON mod_file_cache(mod_id, parent_key, kind, name COLLATE NOCASE);");
            database.exec("CREATE INDEX IF NOT EXISTS idx_profile_order_profile_position ON profile_order_items(profile_name, position);");
            database.exec("CREATE INDEX IF NOT EXISTS idx_profile_plugin_order_profile_position ON profile_plugin_order_items(profile_name, position);");
            database.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_profile_plugin_order_unique_plugin ON profile_plugin_order_items(profile_name, plugin_name) WHERE kind = 'plugin';");
            database.exec("PRAGMA user_version = 4;");
        }

        Database openInstanceDatabase(const std::filesystem::path& projectDirectory)
        {
            if (projectDirectory.empty())
            {
                throw std::invalid_argument("Project directory is required.");
            }

            std::filesystem::create_directories(projectDirectory);
            Database database(instanceDatabasePath(projectDirectory));
            ensureSchema(database);
            return database;
        }

        void setMetadataValue(Database& database, std::wstring_view key, std::wstring_view value)
        {
            Statement statement = database.prepare(
                "INSERT INTO instance_metadata(key, value) VALUES(?, ?) "
                "ON CONFLICT(key) DO UPDATE SET value = excluded.value;");
            statement.bindText(1, key);
            statement.bindText(2, value);
            statement.stepDone();
        }

        std::wstring readMetadataValue(Database& database, std::wstring_view key)
        {
            Statement statement = database.prepare("SELECT value FROM instance_metadata WHERE key = ?;");
            statement.bindText(1, key);
            return statement.stepRow() ? statement.columnText(0) : std::wstring{};
        }

        std::wstring existingUuidForFolder(Database& database, std::wstring_view folderName)
        {
            Statement statement = database.prepare("SELECT uuid FROM mods WHERE folder_name = ? LIMIT 1;");
            statement.bindText(1, folderName);
            return statement.stepRow() ? statement.columnText(0) : std::wstring{};
        }

        void writePortableManifest(const InstalledModRecord& record)
        {
            JsonWriter writer;
            writer.beginObject();
            writer.field(L"schemaVersion", 1);
            writer.field(L"modUuid", record.uuid);
            writer.field(L"gameId", record.gameId);
            writer.field(L"folderName", record.folderName);
            writer.field(L"displayName", record.displayName);
            writer.field(L"version", record.version);
            writer.field(L"installedAt", record.installedAt);
            writer.field(L"updatedAt", record.updatedAt);
            writer.field(L"state", record.state);
            writer.field(L"contentFingerprint", record.contentFingerprint);
            writer.key(L"source").beginObject();
            writer.field(L"provider", record.source.provider);
            writer.field(L"gameDomain", record.source.gameDomain);
            writer.field(L"remoteModId", record.source.remoteModId);
            writer.field(L"remoteFileId", record.source.remoteFileId);
            writer.field(L"url", record.source.url);
            writer.field(L"lastCheckedAt", record.source.lastCheckedAt);
            writer.field(L"latestVersion", record.source.latestVersion);
            writer.endObject();
            writer.endObject();

            writeTextFile(manifestPathForMod(record.path), toUtf8(writer.str()));
        }

        void upsertModRecord(Database& database, InstalledModRecord& record)
        {
            if (record.folderName.empty())
            {
                throw std::invalid_argument("Mod folder name is required.");
            }
            if (record.displayName.empty())
            {
                record.displayName = record.folderName;
            }
            if (record.uuid.empty())
            {
                record.uuid = existingUuidForFolder(database, record.folderName);
            }
            if (record.uuid.empty())
            {
                record.uuid = generateUuid();
            }
            if (record.installedAt.empty())
            {
                record.installedAt = nowUtcText();
            }
            if (record.updatedAt.empty())
            {
                record.updatedAt = record.installedAt;
            }
            if (record.state.empty())
            {
                record.state = L"installed";
            }
            if (record.gameId.empty())
            {
                record.gameId = readMetadataValue(database, L"game_id");
            }

            record.source.provider = normalizeProvider(record.source);

            Statement mod = database.prepare(
                "INSERT INTO mods("
                "uuid, game_id, folder_name, display_name, version, installed_at, updated_at, state, content_fingerprint"
                ") VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?) "
                "ON CONFLICT(folder_name) DO UPDATE SET "
                "game_id = CASE WHEN excluded.game_id = '' THEN mods.game_id ELSE excluded.game_id END,"
                "display_name = excluded.display_name,"
                "version = excluded.version,"
                "updated_at = excluded.updated_at,"
                "state = excluded.state,"
                "content_fingerprint = excluded.content_fingerprint;");
            mod.bindText(1, record.uuid);
            mod.bindText(2, record.gameId);
            mod.bindText(3, record.folderName);
            mod.bindText(4, record.displayName);
            mod.bindText(5, record.version);
            mod.bindText(6, record.installedAt);
            mod.bindText(7, record.updatedAt);
            mod.bindText(8, record.state);
            mod.bindText(9, record.contentFingerprint);
            mod.stepDone();

            Statement id = database.prepare("SELECT id, uuid, installed_at FROM mods WHERE folder_name = ? LIMIT 1;");
            id.bindText(1, record.folderName);
            if (!id.stepRow())
            {
                throw std::runtime_error("Failed to read installed mod metadata.");
            }

            record.id = std::stoll(id.columnText(0));
            record.uuid = id.columnText(1);
            record.installedAt = id.columnText(2);

            Statement source = database.prepare(
                "INSERT INTO mod_sources("
                "mod_id, provider, game_domain, remote_mod_id, remote_file_id, url, last_checked_at, latest_version"
                ") VALUES(?, ?, ?, ?, ?, ?, ?, ?) "
                "ON CONFLICT(mod_id) DO UPDATE SET "
                "provider = excluded.provider,"
                "game_domain = excluded.game_domain,"
                "remote_mod_id = excluded.remote_mod_id,"
                "remote_file_id = excluded.remote_file_id,"
                "url = excluded.url,"
                "last_checked_at = excluded.last_checked_at,"
                "latest_version = excluded.latest_version;");
            source.bindInt64(1, record.id);
            source.bindText(2, record.source.provider);
            source.bindText(3, record.source.gameDomain);
            source.bindText(4, record.source.remoteModId);
            source.bindText(5, record.source.remoteFileId);
            source.bindText(6, record.source.url);
            source.bindText(7, record.source.lastCheckedAt);
            source.bindText(8, record.source.latestVersion);
            source.stepDone();
        }

        InstalledModRecord readRecordByFolder(
            Database& database,
            const std::filesystem::path& projectDirectory,
            std::wstring_view folderName,
            const std::filesystem::path& modsRoot = {})
        {
            Statement statement = database.prepare(
                "SELECT "
                "m.id, m.uuid, m.game_id, m.folder_name, m.display_name, m.version, "
                "m.installed_at, m.updated_at, m.state, m.content_fingerprint, "
                "COALESCE(s.provider, ''), COALESCE(s.game_domain, ''), "
                "COALESCE(s.remote_mod_id, ''), COALESCE(s.remote_file_id, ''), "
                "COALESCE(s.url, ''), COALESCE(s.last_checked_at, ''), COALESCE(s.latest_version, '') "
                "FROM mods m "
                "LEFT JOIN mod_sources s ON s.mod_id = m.id "
                "WHERE m.folder_name = ? "
                "LIMIT 1;");
            statement.bindText(1, folderName);
            if (!statement.stepRow())
            {
                throw std::runtime_error("Installed mod metadata was not found.");
            }

            InstalledModRecord record;
            record.id = std::stoll(statement.columnText(0));
            record.uuid = statement.columnText(1);
            record.gameId = statement.columnText(2);
            record.folderName = statement.columnText(3);
            record.displayName = statement.columnText(4);
            record.version = statement.columnText(5);
            record.installedAt = statement.columnText(6);
            record.updatedAt = statement.columnText(7);
            record.state = statement.columnText(8);
            record.contentFingerprint = statement.columnText(9);
            record.path = modsDirectory(projectDirectory, modsRoot) / std::filesystem::path(record.folderName);
            record.source = ModSourceRecord{
                statement.columnText(10),
                statement.columnText(11),
                statement.columnText(12),
                statement.columnText(13),
                statement.columnText(14),
                statement.columnText(15),
                statement.columnText(16)
            };
            return record;
        }

        std::vector<InstalledModRecord> readInstalledRecords(
            Database& database,
            const std::filesystem::path& projectDirectory,
            const std::filesystem::path& modsRoot = {})
        {
            Statement statement = database.prepare(
                "SELECT "
                "m.id, m.uuid, m.game_id, m.folder_name, m.display_name, m.version, "
                "m.installed_at, m.updated_at, m.state, m.content_fingerprint, "
                "COALESCE(s.provider, ''), COALESCE(s.game_domain, ''), "
                "COALESCE(s.remote_mod_id, ''), COALESCE(s.remote_file_id, ''), "
                "COALESCE(s.url, ''), COALESCE(s.last_checked_at, ''), COALESCE(s.latest_version, '') "
                "FROM mods m "
                "LEFT JOIN mod_sources s ON s.mod_id = m.id "
                "WHERE m.state IN ('installed', 'disabled') "
                "ORDER BY m.display_name COLLATE NOCASE, m.folder_name COLLATE NOCASE;");

            std::vector<InstalledModRecord> records;
            while (statement.stepRow())
            {
                InstalledModRecord record;
                record.id = std::stoll(statement.columnText(0));
                record.uuid = statement.columnText(1);
                record.gameId = statement.columnText(2);
                record.folderName = statement.columnText(3);
                record.displayName = statement.columnText(4);
                record.version = statement.columnText(5);
                record.installedAt = statement.columnText(6);
                record.updatedAt = statement.columnText(7);
                record.state = statement.columnText(8);
                record.contentFingerprint = statement.columnText(9);
                record.path = modsDirectory(projectDirectory, modsRoot) / std::filesystem::path(record.folderName);
                record.source = ModSourceRecord{
                    statement.columnText(10),
                    statement.columnText(11),
                    statement.columnText(12),
                    statement.columnText(13),
                    statement.columnText(14),
                    statement.columnText(15),
                    statement.columnText(16)
                };
                records.push_back(std::move(record));
            }

            return records;
        }

        int nextProfileOrderPosition(Database& database, std::wstring_view profileName)
        {
            Statement statement = database.prepare(
                "SELECT COALESCE(MAX(position), -1) + 1 "
                "FROM profile_order_items WHERE profile_name = ?;");
            statement.bindText(1, profileName);
            return statement.stepRow() ? statement.columnInt(0) : 0;
        }

        int profileOrderItemCount(Database& database, std::wstring_view profileName)
        {
            Statement statement = database.prepare(
                "SELECT COUNT(*) FROM profile_order_items WHERE profile_name = ?;");
            statement.bindText(1, profileName);
            return statement.stepRow() ? statement.columnInt(0) : 0;
        }

        void removeInactiveProfileModItems(Database& database, std::wstring_view profileName)
        {
            Statement remove = database.prepare(
                "DELETE FROM profile_order_items "
                "WHERE profile_name = ? "
                "AND kind = 'mod' "
                "AND (mod_id IS NULL OR mod_id NOT IN ("
                "SELECT id FROM mods WHERE state IN ('installed', 'disabled')"
                "));");
            remove.bindText(1, profileName);
            remove.stepDone();
        }

        void appendMissingProfileModItems(Database& database, std::wstring_view profileName)
        {
            Statement mods = database.prepare(
                "SELECT id FROM mods "
                "WHERE state IN ('installed', 'disabled') "
                "ORDER BY display_name COLLATE NOCASE, folder_name COLLATE NOCASE;");

            int nextPosition = nextProfileOrderPosition(database, profileName);
            const std::wstring now = nowUtcText();
            while (mods.stepRow())
            {
                Statement insert = database.prepare(
                    "INSERT OR IGNORE INTO profile_order_items("
                    "id, profile_name, kind, mod_id, separator_title, position, created_at, updated_at"
                    ") VALUES(?, ?, 'mod', ?, '', ?, ?, ?);");
                insert.bindText(1, generateUuid());
                insert.bindText(2, profileName);
                insert.bindInt64(3, mods.columnInt64(0));
                insert.bindInt(4, nextPosition);
                insert.bindText(5, now);
                insert.bindText(6, now);
                insert.stepDone();
                ++nextPosition;
            }
        }

        void compactProfileOrderPositions(Database& database, std::wstring_view profileName)
        {
            Statement select = database.prepare(
                "SELECT id FROM profile_order_items "
                "WHERE profile_name = ? "
                "ORDER BY position, rowid;");
            select.bindText(1, profileName);

            std::vector<std::wstring> ids;
            while (select.stepRow())
            {
                ids.push_back(select.columnText(0));
            }

            const std::wstring now = nowUtcText();
            for (int index = 0; index < static_cast<int>(ids.size()); ++index)
            {
                Statement update = database.prepare(
                    "UPDATE profile_order_items "
                    "SET position = ?, updated_at = ? "
                    "WHERE id = ?;");
                update.bindInt(1, index);
                update.bindText(2, now);
                update.bindText(3, ids[static_cast<std::size_t>(index)]);
                update.stepDone();
            }
        }

        void syncProfileOrderItems(Database& database, std::wstring_view profileName)
        {
            removeInactiveProfileModItems(database, profileName);
            appendMissingProfileModItems(database, profileName);
            compactProfileOrderPositions(database, profileName);
        }

        std::vector<ProfileOrderItemRecord> readProfileOrderItems(
            Database& database,
            const std::filesystem::path& projectDirectory,
            std::wstring_view profileName,
            const std::filesystem::path& modsRoot = {})
        {
            Statement statement = database.prepare(
                "SELECT "
                "oi.id, oi.profile_name, oi.kind, oi.position, oi.separator_title, "
                "m.id, COALESCE(m.uuid, ''), COALESCE(m.game_id, ''), "
                "COALESCE(m.folder_name, ''), COALESCE(m.display_name, ''), "
                "COALESCE(m.version, ''), COALESCE(m.installed_at, ''), "
                "COALESCE(m.updated_at, ''), COALESCE(m.state, ''), "
                "COALESCE(m.content_fingerprint, ''), "
                "COALESCE(s.provider, ''), COALESCE(s.game_domain, ''), "
                "COALESCE(s.remote_mod_id, ''), COALESCE(s.remote_file_id, ''), "
                "COALESCE(s.url, ''), COALESCE(s.last_checked_at, ''), COALESCE(s.latest_version, '') "
                "FROM profile_order_items oi "
                "LEFT JOIN mods m ON m.id = oi.mod_id AND m.state IN ('installed', 'disabled') "
                "LEFT JOIN mod_sources s ON s.mod_id = m.id "
                "WHERE oi.profile_name = ? "
                "AND (oi.kind = 'separator' OR m.id IS NOT NULL) "
                "ORDER BY oi.position, oi.rowid;");
            statement.bindText(1, profileName);

            std::vector<ProfileOrderItemRecord> records;
            while (statement.stepRow())
            {
                ProfileOrderItemRecord record;
                record.id = statement.columnText(0);
                record.profileName = statement.columnText(1);
                record.kind = statement.columnText(2);
                record.position = statement.columnInt(3);
                record.separatorTitle = statement.columnText(4);
                record.hasMod = record.kind == profileOrderModKind;

                if (record.hasMod)
                {
                    record.mod.id = statement.columnInt64(5);
                    record.mod.uuid = statement.columnText(6);
                    record.mod.gameId = statement.columnText(7);
                    record.mod.folderName = statement.columnText(8);
                    record.mod.displayName = statement.columnText(9);
                    record.mod.version = statement.columnText(10);
                    record.mod.installedAt = statement.columnText(11);
                    record.mod.updatedAt = statement.columnText(12);
                    record.mod.state = statement.columnText(13);
                    record.mod.contentFingerprint = statement.columnText(14);
                    record.mod.path =
                        modsDirectory(projectDirectory, modsRoot) / std::filesystem::path(record.mod.folderName);
                    record.mod.source = ModSourceRecord{
                        statement.columnText(15),
                        statement.columnText(16),
                        statement.columnText(17),
                        statement.columnText(18),
                        statement.columnText(19),
                        statement.columnText(20),
                        statement.columnText(21)
                    };
                }

                records.push_back(std::move(record));
            }

            return records;
        }

        int nextProfilePluginOrderPosition(Database& database, std::wstring_view profileName)
        {
            Statement statement = database.prepare(
                "SELECT COALESCE(MAX(position), -1) + 1 "
                "FROM profile_plugin_order_items WHERE profile_name = ?;");
            statement.bindText(1, profileName);
            return statement.stepRow() ? statement.columnInt(0) : 0;
        }

        int profilePluginOrderItemCount(Database& database, std::wstring_view profileName)
        {
            Statement statement = database.prepare(
                "SELECT COUNT(*) FROM profile_plugin_order_items WHERE profile_name = ?;");
            statement.bindText(1, profileName);
            return statement.stepRow() ? statement.columnInt(0) : 0;
        }

        std::set<std::wstring> pluginNameKeys(const std::vector<std::wstring>& pluginNames)
        {
            std::set<std::wstring> keys;
            for (const std::wstring& pluginName : pluginNames)
            {
                const std::wstring normalized = trim(pluginName);
                if (!normalized.empty())
                {
                    keys.insert(toLower(normalized));
                }
            }

            return keys;
        }

        void removeMissingProfilePluginItems(
            Database& database,
            std::wstring_view profileName,
            const std::vector<std::wstring>& pluginNames)
        {
            const std::set<std::wstring> validPlugins = pluginNameKeys(pluginNames);
            Statement select = database.prepare(
                "SELECT id, plugin_name FROM profile_plugin_order_items "
                "WHERE profile_name = ? AND kind = 'plugin';");
            select.bindText(1, profileName);

            std::vector<std::wstring> idsToRemove;
            while (select.stepRow())
            {
                const std::wstring pluginName = select.columnText(1);
                if (!validPlugins.contains(toLower(pluginName)))
                {
                    idsToRemove.push_back(select.columnText(0));
                }
            }

            for (const std::wstring& id : idsToRemove)
            {
                Statement remove = database.prepare(
                    "DELETE FROM profile_plugin_order_items "
                    "WHERE profile_name = ? AND id = ? AND kind = 'plugin';");
                remove.bindText(1, profileName);
                remove.bindText(2, id);
                remove.stepDone();
            }
        }

        void appendMissingProfilePluginItems(
            Database& database,
            std::wstring_view profileName,
            const std::vector<std::wstring>& pluginNames)
        {
            int nextPosition = nextProfilePluginOrderPosition(database, profileName);
            const std::wstring now = nowUtcText();
            for (const std::wstring& pluginName : pluginNames)
            {
                const std::wstring normalized = trim(pluginName);
                if (normalized.empty())
                {
                    continue;
                }

                Statement insert = database.prepare(
                    "INSERT OR IGNORE INTO profile_plugin_order_items("
                    "id, profile_name, kind, plugin_name, separator_title, position, created_at, updated_at"
                    ") VALUES(?, ?, 'plugin', ?, '', ?, ?, ?);");
                insert.bindText(1, generateUuid());
                insert.bindText(2, profileName);
                insert.bindText(3, normalized);
                insert.bindInt(4, nextPosition);
                insert.bindText(5, now);
                insert.bindText(6, now);
                insert.stepDone();
                ++nextPosition;
            }
        }

        void compactProfilePluginOrderPositions(Database& database, std::wstring_view profileName)
        {
            Statement select = database.prepare(
                "SELECT id FROM profile_plugin_order_items "
                "WHERE profile_name = ? "
                "ORDER BY position, rowid;");
            select.bindText(1, profileName);

            std::vector<std::wstring> ids;
            while (select.stepRow())
            {
                ids.push_back(select.columnText(0));
            }

            const std::wstring now = nowUtcText();
            for (int index = 0; index < static_cast<int>(ids.size()); ++index)
            {
                Statement update = database.prepare(
                    "UPDATE profile_plugin_order_items "
                    "SET position = ?, updated_at = ? "
                    "WHERE id = ?;");
                update.bindInt(1, index);
                update.bindText(2, now);
                update.bindText(3, ids[static_cast<std::size_t>(index)]);
                update.stepDone();
            }
        }

        void syncProfilePluginOrderItems(
            Database& database,
            std::wstring_view profileName,
            const std::vector<std::wstring>& pluginNames)
        {
            removeMissingProfilePluginItems(database, profileName, pluginNames);
            appendMissingProfilePluginItems(database, profileName, pluginNames);
            compactProfilePluginOrderPositions(database, profileName);
        }

        std::vector<ProfilePluginOrderItemRecord> readProfilePluginOrderItems(
            Database& database,
            std::wstring_view profileName)
        {
            Statement statement = database.prepare(
                "SELECT id, profile_name, kind, position, plugin_name, separator_title "
                "FROM profile_plugin_order_items "
                "WHERE profile_name = ? "
                "ORDER BY position, rowid;");
            statement.bindText(1, profileName);

            std::vector<ProfilePluginOrderItemRecord> records;
            while (statement.stepRow())
            {
                ProfilePluginOrderItemRecord record;
                record.id = statement.columnText(0);
                record.profileName = statement.columnText(1);
                record.kind = statement.columnText(2);
                record.position = statement.columnInt(3);
                record.pluginName = statement.columnText(4);
                record.separatorTitle = statement.columnText(5);
                records.push_back(std::move(record));
            }

            return records;
        }

        struct ProfileOrderStorageItem
        {
            std::wstring id;
            std::wstring kind;
        };

        std::vector<ProfileOrderStorageItem> readProfileOrderStorageItems(
            Database& database,
            std::wstring_view profileName,
            const char* tableName)
        {
            const std::string sql = std::string(
                "SELECT id, kind FROM ") + tableName +
                " WHERE profile_name = ? "
                "ORDER BY position, rowid;";
            Statement select = database.prepare(sql.c_str());
            select.bindText(1, profileName);

            std::vector<ProfileOrderStorageItem> items;
            while (select.stepRow())
            {
                items.push_back(ProfileOrderStorageItem{
                    select.columnText(0),
                    select.columnText(1)
                });
            }

            return items;
        }

        int profileOrderMoveBlockEnd(
            const std::vector<ProfileOrderStorageItem>& items,
            int sourceIndex,
            std::wstring_view separatorKind)
        {
            if (items[static_cast<std::size_t>(sourceIndex)].kind != separatorKind)
            {
                return sourceIndex + 1;
            }

            for (int index = sourceIndex + 1; index < static_cast<int>(items.size()); ++index)
            {
                if (items[static_cast<std::size_t>(index)].kind == separatorKind)
                {
                    return index;
                }
            }

            return static_cast<int>(items.size());
        }

        bool reorderProfileOrderStorageItems(
            std::vector<ProfileOrderStorageItem>& items,
            std::wstring_view orderItemId,
            int targetIndex,
            std::wstring_view separatorKind)
        {
            const auto source = std::find_if(
                items.begin(),
                items.end(),
                [orderItemId](const ProfileOrderStorageItem& item)
                {
                    return item.id == orderItemId;
                });
            if (source == items.end())
            {
                throw std::invalid_argument("Profile order item was not found.");
            }

            if (items.size() <= 1)
            {
                return false;
            }

            const int sourceIndex = static_cast<int>(std::distance(items.begin(), source));
            const int blockEnd = profileOrderMoveBlockEnd(items, sourceIndex, separatorKind);
            const int blockLength = blockEnd - sourceIndex;

            if (blockLength <= 1)
            {
                const int clampedTarget = std::clamp(
                    targetIndex,
                    0,
                    static_cast<int>(items.size() - 1));
                if (sourceIndex == clampedTarget)
                {
                    return false;
                }

                ProfileOrderStorageItem moving = std::move(items[static_cast<std::size_t>(sourceIndex)]);
                items.erase(items.begin() + sourceIndex);
                items.insert(items.begin() + clampedTarget, std::move(moving));
                return true;
            }

            if (targetIndex >= sourceIndex && targetIndex < blockEnd)
            {
                return false;
            }

            const int maxDestination = static_cast<int>(items.size()) - blockLength;
            const int desiredDestination = targetIndex > sourceIndex
                ? targetIndex + 1 - blockLength
                : targetIndex;
            const int destination = std::clamp(desiredDestination, 0, maxDestination);
            if (destination == sourceIndex)
            {
                return false;
            }

            std::vector<ProfileOrderStorageItem> moving(
                std::make_move_iterator(items.begin() + sourceIndex),
                std::make_move_iterator(items.begin() + blockEnd));
            items.erase(items.begin() + sourceIndex, items.begin() + blockEnd);
            items.insert(
                items.begin() + destination,
                std::make_move_iterator(moving.begin()),
                std::make_move_iterator(moving.end()));
            return true;
        }

        void writeProfileOrderStorageItemPositions(
            Database& database,
            std::wstring_view profileName,
            const char* tableName,
            const std::vector<ProfileOrderStorageItem>& items)
        {
            const std::wstring now = nowUtcText();
            const std::string sql = std::string(
                "UPDATE ") + tableName +
                " SET position = ?, updated_at = ? "
                "WHERE profile_name = ? AND id = ?;";

            for (int index = 0; index < static_cast<int>(items.size()); ++index)
            {
                Statement update = database.prepare(sql.c_str());
                update.bindInt(1, index);
                update.bindText(2, now);
                update.bindText(3, profileName);
                update.bindText(4, items[static_cast<std::size_t>(index)].id);
                update.stepDone();
            }
        }

        void moveProfileOrderStorageItems(
            Database& database,
            std::wstring_view profileName,
            const char* tableName,
            std::wstring_view orderItemId,
            int targetIndex,
            std::wstring_view separatorKind)
        {
            std::vector<ProfileOrderStorageItem> items =
                readProfileOrderStorageItems(database, profileName, tableName);
            if (reorderProfileOrderStorageItems(items, orderItemId, targetIndex, separatorKind))
            {
                writeProfileOrderStorageItemPositions(database, profileName, tableName, items);
            }
        }

        void syncInstalledModsFromDisk(
            Database& database,
            const std::filesystem::path& projectDirectory,
            const std::filesystem::path& modsRoot = {});

        int cachedFileCount(Database& database, std::int64_t modId)
        {
            Statement statement = database.prepare(
                "SELECT COUNT(*) FROM mod_file_cache WHERE mod_id = ? AND kind = 'file';");
            statement.bindInt64(1, modId);
            return statement.stepRow() ? statement.columnInt(0) : 0;
        }

        int cachedEntryCount(Database& database, std::int64_t modId)
        {
            Statement statement = database.prepare(
                "SELECT COUNT(*) FROM mod_file_cache WHERE mod_id = ?;");
            statement.bindInt64(1, modId);
            return statement.stepRow() ? statement.columnInt(0) : 0;
        }

        std::wstring fileTimeCacheText(const std::filesystem::directory_entry& entry)
        {
            std::error_code error;
            const auto writeTime = entry.last_write_time(error);
            return error
                ? std::wstring()
                : std::to_wstring(writeTime.time_since_epoch().count());
        }

        void insertFileCacheEntry(
            Database& database,
            std::int64_t modId,
            std::wstring_view relativePath,
            std::wstring_view parentPath,
            std::wstring_view name,
            std::wstring_view kind,
            std::uintmax_t size,
            std::wstring_view modifiedAt)
        {
            Statement insert = database.prepare(
                "INSERT INTO mod_file_cache("
                "mod_id, relative_path, parent_path, path_key, parent_key, name, kind, size, modified_at"
                ") VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?) "
                "ON CONFLICT(mod_id, path_key) DO UPDATE SET "
                "relative_path = excluded.relative_path,"
                "parent_path = excluded.parent_path,"
                "parent_key = excluded.parent_key,"
                "name = excluded.name,"
                "kind = excluded.kind,"
                "size = excluded.size,"
                "modified_at = excluded.modified_at;");
            insert.bindInt64(1, modId);
            insert.bindText(2, relativePath);
            insert.bindText(3, parentPath);
            insert.bindText(4, pathKey(relativePath));
            insert.bindText(5, pathKey(parentPath));
            insert.bindText(6, name);
            insert.bindText(7, kind);
            insert.bindInt64(8, static_cast<std::int64_t>(size));
            insert.bindText(9, modifiedAt);
            insert.stepDone();
        }

        void rebuildFileCache(Database& database, InstalledModRecord& record)
        {
            Statement remove = database.prepare("DELETE FROM mod_file_cache WHERE mod_id = ?;");
            remove.bindInt64(1, record.id);
            remove.stepDone();

            if (!std::filesystem::exists(record.path) || !std::filesystem::is_directory(record.path))
            {
                return;
            }

            std::error_code error;
            std::filesystem::recursive_directory_iterator iterator(
                record.path,
                std::filesystem::directory_options::skip_permission_denied,
                error);
            const std::filesystem::recursive_directory_iterator end;

            while (!error && iterator != end)
            {
                const std::filesystem::path current = iterator->path();
                std::error_code entryError;
                const bool isDirectory = iterator->is_directory(entryError);
                if (!entryError && isDirectory && current.filename().wstring() == manifestDirectoryName)
                {
                    iterator.disable_recursion_pending();
                    iterator.increment(error);
                    continue;
                }

                const bool isFile = !isDirectory && iterator->is_regular_file(entryError);
                if (!entryError && (isDirectory || isFile))
                {
                    std::error_code relativeError;
                    const std::filesystem::path relative = std::filesystem::relative(current, record.path, relativeError);
                    if (!relativeError && !relative.empty())
                    {
                        std::error_code sizeError;
                        const std::wstring relativeText = normalizeRelativePath(relative);
                        const std::wstring parentText = normalizeRelativePath(relative.parent_path());
                        const std::uintmax_t size = isFile ? iterator->file_size(sizeError) : 0;
                        insertFileCacheEntry(
                            database,
                            record.id,
                            relativeText,
                            parentText,
                            current.filename().wstring(),
                            isDirectory ? L"directory" : L"file",
                            sizeError ? 0 : size,
                            fileTimeCacheText(*iterator));
                    }
                }

                iterator.increment(error);
            }
        }

        void ensureFileCacheFresh(Database& database, InstalledModRecord& record)
        {
            const std::wstring currentFingerprint = computeContentFingerprint(record.path);
            if (currentFingerprint == record.contentFingerprint && cachedFileCount(database, record.id) > 0)
            {
                return;
            }

            rebuildFileCache(database, record);
            record.contentFingerprint = currentFingerprint;

            Statement update = database.prepare(
                "UPDATE mods SET content_fingerprint = ?, updated_at = ? WHERE id = ?;");
            update.bindText(1, record.contentFingerprint);
            update.bindText(2, nowUtcText());
            update.bindInt64(3, record.id);
            update.stepDone();
        }

        void ensureFileCachePrepared(Database& database, InstalledModRecord& record)
        {
            if (!record.contentFingerprint.empty() && cachedEntryCount(database, record.id) > 0)
            {
                return;
            }

            ensureFileCacheFresh(database, record);
        }

        struct ConflictOwner
        {
            std::int64_t modId{0};
            std::wstring displayName;
        };

        std::vector<ConflictOwner> conflictOwnersForPath(Database& database, std::wstring_view key)
        {
            Statement statement = database.prepare(
                "SELECT f.mod_id, m.display_name "
                "FROM mod_file_cache f "
                "JOIN mods m ON m.id = f.mod_id "
                "WHERE f.path_key = ? AND f.kind = 'file' AND m.state = 'installed' "
                "ORDER BY m.id ASC;");
            statement.bindText(1, key);

            std::vector<ConflictOwner> owners;
            while (statement.stepRow())
            {
                owners.push_back(ConflictOwner{
                    std::stoll(statement.columnText(0)),
                    statement.columnText(1)
                });
            }

            return owners;
        }

        std::wstring conflictStateForOwners(
            const std::vector<ConflictOwner>& owners,
            std::int64_t modId)
        {
            if (owners.size() <= 1)
            {
                return {};
            }

            for (std::size_t index = 0; index < owners.size(); ++index)
            {
                if (owners[index].modId != modId)
                {
                    continue;
                }

                if (index == owners.size() - 1)
                {
                    return L"overwrites";
                }
                if (index == 0)
                {
                    return L"overwritten";
                }

                return L"conflict";
            }

            return {};
        }

        std::vector<std::wstring> ownerNames(const std::vector<ConflictOwner>& owners)
        {
            std::vector<std::wstring> names;
            names.reserve(owners.size());
            for (const ConflictOwner& owner : owners)
            {
                names.push_back(owner.displayName);
            }
            return names;
        }

        bool hasCachedChildren(Database& database, std::int64_t modId, std::wstring_view parentKey)
        {
            Statement statement = database.prepare(
                "SELECT 1 FROM mod_file_cache WHERE mod_id = ? AND parent_key = ? LIMIT 1;");
            statement.bindInt64(1, modId);
            statement.bindText(2, parentKey);
            return statement.stepRow();
        }

        ModFileSummary summarizeCachedModFiles(Database& database, const InstalledModRecord& record)
        {
            Statement statement = database.prepare(
                "SELECT path_key FROM mod_file_cache WHERE mod_id = ? AND kind = 'file';");
            statement.bindInt64(1, record.id);

            ModFileSummary summary;
            while (statement.stepRow())
            {
                ++summary.fileCount;
                if (record.state == L"disabled")
                {
                    continue;
                }

                const std::vector<ConflictOwner> owners = conflictOwnersForPath(database, statement.columnText(0));
                if (owners.size() <= 1)
                {
                    continue;
                }

                ++summary.conflictingFileCount;
                const std::wstring state = conflictStateForOwners(owners, record.id);
                if (state == L"overwrites")
                {
                    ++summary.overwritingFileCount;
                }
                else if (state == L"overwritten")
                {
                    ++summary.overwrittenFileCount;
                }
                else if (state == L"conflict")
                {
                    ++summary.overwritingFileCount;
                    ++summary.overwrittenFileCount;
                }
            }

            return summary;
        }

        void ensureAllFileCachesFresh(
            Database& database,
            const std::filesystem::path& projectDirectory,
            const std::filesystem::path& modsRoot = {})
        {
            syncInstalledModsFromDisk(database, projectDirectory, modsRoot);
            std::vector<InstalledModRecord> records = readInstalledRecords(database, projectDirectory, modsRoot);

            Transaction transaction(database);
            for (InstalledModRecord& record : records)
            {
                ensureFileCacheFresh(database, record);
            }
            transaction.commit();
        }

        void ensureAllFileCachesPrepared(
            Database& database,
            const std::filesystem::path& projectDirectory,
            const std::filesystem::path& modsRoot = {})
        {
            syncInstalledModsFromDisk(database, projectDirectory, modsRoot);
            std::vector<InstalledModRecord> records = readInstalledRecords(database, projectDirectory, modsRoot);

            Transaction transaction(database);
            for (InstalledModRecord& record : records)
            {
                ensureFileCachePrepared(database, record);
            }
            transaction.commit();
        }

        struct ProfileFileOwner
        {
            std::int64_t modId{0};
            bool active{false};
        };

        void applyProfileConflictGroup(
            const std::vector<ProfileFileOwner>& owners,
            const std::map<std::int64_t, std::size_t>& summaryIndexes,
            std::vector<ModFileSummaryRecord>& summaries)
        {
            std::vector<std::int64_t> activeOwnerIds;
            activeOwnerIds.reserve(owners.size());
            for (const ProfileFileOwner& owner : owners)
            {
                if (owner.active)
                {
                    activeOwnerIds.push_back(owner.modId);
                }
            }

            if (activeOwnerIds.size() <= 1)
            {
                return;
            }

            for (std::size_t index = 0; index < activeOwnerIds.size(); ++index)
            {
                const auto summaryIndex = summaryIndexes.find(activeOwnerIds[index]);
                if (summaryIndex == summaryIndexes.end())
                {
                    continue;
                }

                ModFileSummary& summary = summaries[summaryIndex->second].summary;
                ++summary.conflictingFileCount;
                if (index == 0)
                {
                    ++summary.overwrittenFileCount;
                }
                else if (index == activeOwnerIds.size() - 1)
                {
                    ++summary.overwritingFileCount;
                }
                else
                {
                    ++summary.overwrittenFileCount;
                    ++summary.overwritingFileCount;
                }
            }
        }

        std::vector<ModFileSummaryRecord> summarizeCachedProfileModFiles(
            Database& database,
            const std::filesystem::path& projectDirectory,
            std::wstring_view profileName,
            const std::filesystem::path& modsRoot = {})
        {
            const std::wstring normalizedProfileName = profileNameOrDefault(profileName);

            Transaction transaction(database);
            syncProfileOrderItems(database, normalizedProfileName);
            transaction.commit();

            const std::vector<ProfileOrderItemRecord> orderItems =
                readProfileOrderItems(database, projectDirectory, normalizedProfileName, modsRoot);

            std::vector<ModFileSummaryRecord> summaries;
            summaries.reserve(orderItems.size());
            std::map<std::int64_t, std::size_t> summaryIndexes;
            for (const ProfileOrderItemRecord& item : orderItems)
            {
                if (item.kind != profileOrderModKind || !item.hasMod)
                {
                    continue;
                }

                const std::size_t index = summaries.size();
                summaryIndexes.emplace(item.mod.id, index);
                summaries.push_back(ModFileSummaryRecord{
                    item.mod.folderName,
                    item.mod.path,
                    ModFileSummary{}
                });
            }

            Statement statement = database.prepare(
                "SELECT f.path_key, f.mod_id, m.state "
                "FROM mod_file_cache f "
                "JOIN profile_order_items oi ON oi.mod_id = f.mod_id "
                "JOIN mods m ON m.id = f.mod_id "
                "WHERE oi.profile_name = ? "
                "AND oi.kind = 'mod' "
                "AND f.kind = 'file' "
                "AND m.state IN ('installed', 'disabled') "
                "ORDER BY f.path_key, oi.position, oi.rowid;");
            statement.bindText(1, normalizedProfileName);

            std::wstring currentPathKey;
            std::vector<ProfileFileOwner> owners;
            while (statement.stepRow())
            {
                const std::wstring itemPathKey = statement.columnText(0);
                if (!currentPathKey.empty() && itemPathKey != currentPathKey)
                {
                    applyProfileConflictGroup(owners, summaryIndexes, summaries);
                    owners.clear();
                }

                currentPathKey = itemPathKey;

                const std::int64_t modId = statement.columnInt64(1);
                const auto summaryIndex = summaryIndexes.find(modId);
                if (summaryIndex != summaryIndexes.end())
                {
                    ++summaries[summaryIndex->second].summary.fileCount;
                }

                owners.push_back(ProfileFileOwner{
                    modId,
                    statement.columnText(2) == L"installed"
                });
            }

            if (!owners.empty())
            {
                applyProfileConflictGroup(owners, summaryIndexes, summaries);
            }

            return summaries;
        }

        std::set<std::wstring> activeInstalledModFolders(Database& database)
        {
            Statement statement = database.prepare(
                "SELECT folder_name FROM mods WHERE state IN ('installed', 'disabled');");

            std::set<std::wstring> folders;
            while (statement.stepRow())
            {
                folders.insert(statement.columnText(0));
            }

            return folders;
        }

        void markInstalledModsMissingFromDiskDeleted(
            Database& database,
            const std::set<std::wstring>& diskFolders)
        {
            Statement statement = database.prepare(
                "SELECT folder_name FROM mods WHERE state IN ('installed', 'disabled');");

            std::vector<std::wstring> missingFolders;
            while (statement.stepRow())
            {
                const std::wstring folderName = statement.columnText(0);
                if (!diskFolders.contains(folderName))
                {
                    missingFolders.push_back(folderName);
                }
            }

            if (missingFolders.empty())
            {
                return;
            }

            const std::wstring now = nowUtcText();
            for (const std::wstring& folderName : missingFolders)
            {
                Statement update = database.prepare(
                    "UPDATE mods SET state = 'deleted', updated_at = ? WHERE folder_name = ?;");
                update.bindText(1, now);
                update.bindText(2, folderName);
                update.stepDone();
            }
        }

        bool isTransientModDirectoryName(std::wstring_view folderName)
        {
            if (folderName.empty() || folderName.front() == L'.')
            {
                return true;
            }

            constexpr std::array<std::wstring_view, 3> suffixes{
                L".fomod-package",
                L".installing",
                L".replacing"
            };
            for (std::wstring_view suffix : suffixes)
            {
                if (folderName.size() >= suffix.size() &&
                    std::equal(
                        suffix.rbegin(),
                        suffix.rend(),
                        folderName.rbegin(),
                        [](wchar_t left, wchar_t right)
                        {
                            return std::towlower(left) == std::towlower(right);
                        }))
                {
                    return true;
                }
            }

            return false;
        }

        void syncInstalledModsFromDisk(
            Database& database,
            const std::filesystem::path& projectDirectory,
            const std::filesystem::path& modsRoot)
        {
            const std::filesystem::path directory = modsDirectory(projectDirectory, modsRoot);
            if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory))
            {
                return;
            }

            const std::set<std::wstring> activeFolders = activeInstalledModFolders(database);
            std::set<std::wstring> diskFolders;

            Transaction transaction(database);
            for (const auto& entry : std::filesystem::directory_iterator(directory))
            {
                if (!entry.is_directory())
                {
                    continue;
                }

                const std::wstring folderName = entry.path().filename().wstring();
                if (isTransientModDirectoryName(folderName))
                {
                    continue;
                }

                diskFolders.insert(folderName);
                if (activeFolders.contains(folderName))
                {
                    continue;
                }

                std::optional<InstalledModRecord> manifestRecord;
                try
                {
                    manifestRecord = readManifestRecord(entry.path());
                }
                catch (const std::exception&)
                {
                    manifestRecord = std::nullopt;
                }

                InstalledModRecord record = manifestRecord.value_or(InstalledModRecord{
                    0,
                    {},
                    readMetadataValue(database, L"game_id"),
                    folderName,
                    folderName,
                    {},
                    nowUtcText(),
                    nowUtcText(),
                    L"installed",
                    {},
                    entry.path(),
                    ModSourceRecord{L"manual"}
                });

                record.folderName = folderName;
                record.path = entry.path();
                if (record.contentFingerprint.empty())
                {
                    record.contentFingerprint = computeContentFingerprint(entry.path());
                }

                upsertModRecord(database, record);
                writePortableManifest(record);
            }

            markInstalledModsMissingFromDiskDeleted(database, diskFolders);
            transaction.commit();
        }
    }

    void InstanceMetadataStore::ensureInstance(
        const std::filesystem::path& projectDirectory,
        std::wstring_view gameId)
    {
        const std::lock_guard metadataLock(metadataStoreMutex());

        Database database = openInstanceDatabase(projectDirectory);

        Transaction transaction(database);
        if (readMetadataValue(database, L"created_at").empty())
        {
            setMetadataValue(database, L"created_at", nowUtcText());
        }
        setMetadataValue(database, L"schema_version", L"2");
        if (!gameId.empty())
        {
            setMetadataValue(database, L"game_id", gameId);
        }
        transaction.commit();
    }

    std::wstring InstanceMetadataStore::gameId(
        const std::filesystem::path& projectDirectory)
    {
        const std::lock_guard metadataLock(metadataStoreMutex());

        Database database = openInstanceDatabase(projectDirectory);
        return readMetadataValue(database, L"game_id");
    }

    std::vector<InstalledModRecord> InstanceMetadataStore::listInstalledMods(
        const std::filesystem::path& projectDirectory,
        const std::filesystem::path& modsRoot)
    {
        const std::lock_guard metadataLock(metadataStoreMutex());

        Database database = openInstanceDatabase(projectDirectory);
        syncInstalledModsFromDisk(database, projectDirectory, modsRoot);
        return readInstalledRecords(database, projectDirectory, modsRoot);
    }

    std::vector<ProfileOrderItemRecord> InstanceMetadataStore::listProfileOrderItems(
        const std::filesystem::path& projectDirectory,
        std::wstring_view profileName,
        const std::filesystem::path& modsRoot)
    {
        const std::lock_guard metadataLock(metadataStoreMutex());

        if (projectDirectory.empty())
        {
            throw std::invalid_argument("Project directory is required.");
        }

        const std::wstring normalizedProfileName = profileNameOrDefault(profileName);
        Database database = openInstanceDatabase(projectDirectory);
        syncInstalledModsFromDisk(database, projectDirectory, modsRoot);

        Transaction transaction(database);
        syncProfileOrderItems(database, normalizedProfileName);
        transaction.commit();

        return readProfileOrderItems(database, projectDirectory, normalizedProfileName, modsRoot);
    }

    std::vector<ProfileOrderItemRecord> InstanceMetadataStore::createProfileOrderSeparator(
        const std::filesystem::path& projectDirectory,
        std::wstring_view profileName,
        std::wstring_view title,
        int targetIndex,
        const std::filesystem::path& modsRoot)
    {
        const std::lock_guard metadataLock(metadataStoreMutex());

        if (projectDirectory.empty())
        {
            throw std::invalid_argument("Project directory is required.");
        }

        const std::wstring normalizedProfileName = profileNameOrDefault(profileName);
        const std::wstring normalizedTitle = trim(std::wstring(title));
        if (normalizedTitle.empty())
        {
            throw std::invalid_argument("Separator title is required.");
        }

        Database database = openInstanceDatabase(projectDirectory);
        syncInstalledModsFromDisk(database, projectDirectory, modsRoot);

        Transaction transaction(database);
        syncProfileOrderItems(database, normalizedProfileName);

        const int count = profileOrderItemCount(database, normalizedProfileName);
        const int position = std::clamp(targetIndex, 0, count);
        const std::wstring now = nowUtcText();

        Statement shift = database.prepare(
            "UPDATE profile_order_items "
            "SET position = position + 1, updated_at = ? "
            "WHERE profile_name = ? AND position >= ?;");
        shift.bindText(1, now);
        shift.bindText(2, normalizedProfileName);
        shift.bindInt(3, position);
        shift.stepDone();

        Statement insert = database.prepare(
            "INSERT INTO profile_order_items("
            "id, profile_name, kind, mod_id, separator_title, position, created_at, updated_at"
            ") VALUES(?, ?, 'separator', NULL, ?, ?, ?, ?);");
        insert.bindText(1, generateUuid());
        insert.bindText(2, normalizedProfileName);
        insert.bindText(3, normalizedTitle);
        insert.bindInt(4, position);
        insert.bindText(5, now);
        insert.bindText(6, now);
        insert.stepDone();

        compactProfileOrderPositions(database, normalizedProfileName);
        transaction.commit();

        return readProfileOrderItems(database, projectDirectory, normalizedProfileName, modsRoot);
    }

    std::vector<ProfileOrderItemRecord> InstanceMetadataStore::deleteProfileOrderSeparator(
        const std::filesystem::path& projectDirectory,
        std::wstring_view profileName,
        std::wstring_view separatorId,
        const std::filesystem::path& modsRoot)
    {
        const std::lock_guard metadataLock(metadataStoreMutex());

        if (projectDirectory.empty())
        {
            throw std::invalid_argument("Project directory is required.");
        }

        const std::wstring normalizedProfileName = profileNameOrDefault(profileName);
        const std::wstring id = trim(std::wstring(separatorId));
        if (id.empty())
        {
            throw std::invalid_argument("Separator id is required.");
        }

        Database database = openInstanceDatabase(projectDirectory);
        Transaction transaction(database);
        syncProfileOrderItems(database, normalizedProfileName);

        Statement remove = database.prepare(
            "DELETE FROM profile_order_items "
            "WHERE profile_name = ? AND id = ? AND kind = 'separator';");
        remove.bindText(1, normalizedProfileName);
        remove.bindText(2, id);
        remove.stepDone();

        compactProfileOrderPositions(database, normalizedProfileName);
        transaction.commit();

        return readProfileOrderItems(database, projectDirectory, normalizedProfileName, modsRoot);
    }

    std::vector<ProfileOrderItemRecord> InstanceMetadataStore::moveProfileOrderItem(
        const std::filesystem::path& projectDirectory,
        std::wstring_view profileName,
        std::wstring_view orderItemId,
        int targetIndex,
        const std::filesystem::path& modsRoot)
    {
        const std::lock_guard metadataLock(metadataStoreMutex());

        if (projectDirectory.empty())
        {
            throw std::invalid_argument("Project directory is required.");
        }

        const std::wstring normalizedProfileName = profileNameOrDefault(profileName);
        const std::wstring id = trim(std::wstring(orderItemId));
        if (id.empty())
        {
            throw std::invalid_argument("Profile order item id is required.");
        }

        Database database = openInstanceDatabase(projectDirectory);

        Transaction transaction(database);
        syncProfileOrderItems(database, normalizedProfileName);

        moveProfileOrderStorageItems(
            database,
            normalizedProfileName,
            "profile_order_items",
            id,
            targetIndex,
            profileOrderSeparatorKind);

        transaction.commit();
        return readProfileOrderItems(database, projectDirectory, normalizedProfileName, modsRoot);
    }

    void InstanceMetadataStore::replaceProfileOrderItems(
        const std::filesystem::path& projectDirectory,
        std::wstring_view profileName,
        const std::vector<ProfileOrderImportItemRecord>& items)
    {
        const std::lock_guard metadataLock(metadataStoreMutex());

        if (projectDirectory.empty())
        {
            throw std::invalid_argument("Project directory is required.");
        }

        const std::wstring normalizedProfileName = profileNameOrDefault(profileName);
        Database database = openInstanceDatabase(projectDirectory);
        syncInstalledModsFromDisk(database, projectDirectory);

        Transaction transaction(database);

        Statement remove = database.prepare("DELETE FROM profile_order_items WHERE profile_name = ?;");
        remove.bindText(1, normalizedProfileName);
        remove.stepDone();

        const std::wstring now = nowUtcText();
        int position = 0;
        for (const ProfileOrderImportItemRecord& item : items)
        {
            const std::wstring kind = trim(item.kind);
            if (kind == profileOrderSeparatorKind)
            {
                const std::wstring title = trim(item.separatorTitle);
                if (title.empty())
                {
                    continue;
                }

                Statement insert = database.prepare(
                    "INSERT INTO profile_order_items("
                    "id, profile_name, kind, mod_id, separator_title, position, created_at, updated_at"
                    ") VALUES(?, ?, 'separator', NULL, ?, ?, ?, ?);");
                insert.bindText(1, generateUuid());
                insert.bindText(2, normalizedProfileName);
                insert.bindText(3, title);
                insert.bindInt(4, position);
                insert.bindText(5, now);
                insert.bindText(6, now);
                insert.stepDone();
                ++position;
                continue;
            }

            if (kind != profileOrderModKind)
            {
                continue;
            }

            const std::wstring folderName = trim(item.folderName);
            if (folderName.empty())
            {
                continue;
            }

            Statement select = database.prepare(
                "SELECT id FROM mods "
                "WHERE folder_name = ? COLLATE NOCASE "
                "AND state IN ('installed', 'disabled') "
                "ORDER BY folder_name = ? DESC "
                "LIMIT 1;");
            select.bindText(1, folderName);
            select.bindText(2, folderName);
            if (!select.stepRow())
            {
                continue;
            }

            Statement insert = database.prepare(
                "INSERT OR IGNORE INTO profile_order_items("
                "id, profile_name, kind, mod_id, separator_title, position, created_at, updated_at"
                ") VALUES(?, ?, 'mod', ?, '', ?, ?, ?);");
            insert.bindText(1, generateUuid());
            insert.bindText(2, normalizedProfileName);
            insert.bindInt64(3, select.columnInt64(0));
            insert.bindInt(4, position);
            insert.bindText(5, now);
            insert.bindText(6, now);
            insert.stepDone();
            ++position;
        }

        appendMissingProfileModItems(database, normalizedProfileName);
        compactProfileOrderPositions(database, normalizedProfileName);
        transaction.commit();
    }

    std::vector<ProfilePluginOrderItemRecord> InstanceMetadataStore::listProfilePluginOrderItems(
        const std::filesystem::path& projectDirectory,
        std::wstring_view profileName,
        const std::vector<std::wstring>& pluginNames)
    {
        const std::lock_guard metadataLock(metadataStoreMutex());

        if (projectDirectory.empty())
        {
            throw std::invalid_argument("Project directory is required.");
        }

        const std::wstring normalizedProfileName = profileNameOrDefault(profileName);
        Database database = openInstanceDatabase(projectDirectory);

        Transaction transaction(database);
        syncProfilePluginOrderItems(database, normalizedProfileName, pluginNames);
        transaction.commit();

        return readProfilePluginOrderItems(database, normalizedProfileName);
    }

    void InstanceMetadataStore::replaceProfilePluginOrderItems(
        const std::filesystem::path& projectDirectory,
        std::wstring_view profileName,
        const std::vector<ProfilePluginOrderImportItemRecord>& items)
    {
        const std::lock_guard metadataLock(metadataStoreMutex());

        if (projectDirectory.empty())
        {
            throw std::invalid_argument("Project directory is required.");
        }

        const std::wstring normalizedProfileName = profileNameOrDefault(profileName);
        Database database = openInstanceDatabase(projectDirectory);

        Transaction transaction(database);

        Statement remove = database.prepare("DELETE FROM profile_plugin_order_items WHERE profile_name = ?;");
        remove.bindText(1, normalizedProfileName);
        remove.stepDone();

        const std::wstring now = nowUtcText();
        std::set<std::wstring> seenPlugins;
        int position = 0;
        for (const ProfilePluginOrderImportItemRecord& item : items)
        {
            const std::wstring kind = trim(item.kind);
            if (kind == profilePluginOrderSeparatorKind)
            {
                const std::wstring title = trim(item.separatorTitle);
                if (title.empty())
                {
                    continue;
                }

                Statement insert = database.prepare(
                    "INSERT INTO profile_plugin_order_items("
                    "id, profile_name, kind, plugin_name, separator_title, position, created_at, updated_at"
                    ") VALUES(?, ?, 'separator', '', ?, ?, ?, ?);");
                insert.bindText(1, generateUuid());
                insert.bindText(2, normalizedProfileName);
                insert.bindText(3, title);
                insert.bindInt(4, position);
                insert.bindText(5, now);
                insert.bindText(6, now);
                insert.stepDone();
                ++position;
                continue;
            }

            if (kind != profilePluginOrderPluginKind)
            {
                continue;
            }

            const std::wstring pluginName = trim(item.pluginName);
            const std::wstring key = toLower(pluginName);
            if (pluginName.empty() || !seenPlugins.insert(key).second)
            {
                continue;
            }

            Statement insert = database.prepare(
                "INSERT OR IGNORE INTO profile_plugin_order_items("
                "id, profile_name, kind, plugin_name, separator_title, position, created_at, updated_at"
                ") VALUES(?, ?, 'plugin', ?, '', ?, ?, ?);");
            insert.bindText(1, generateUuid());
            insert.bindText(2, normalizedProfileName);
            insert.bindText(3, pluginName);
            insert.bindInt(4, position);
            insert.bindText(5, now);
            insert.bindText(6, now);
            insert.stepDone();
            ++position;
        }

        compactProfilePluginOrderPositions(database, normalizedProfileName);
        transaction.commit();
    }

    std::vector<ProfilePluginOrderItemRecord> InstanceMetadataStore::createProfilePluginOrderSeparator(
        const std::filesystem::path& projectDirectory,
        std::wstring_view profileName,
        const std::vector<std::wstring>& pluginNames,
        std::wstring_view title,
        int targetIndex)
    {
        const std::lock_guard metadataLock(metadataStoreMutex());

        if (projectDirectory.empty())
        {
            throw std::invalid_argument("Project directory is required.");
        }

        const std::wstring normalizedProfileName = profileNameOrDefault(profileName);
        const std::wstring normalizedTitle = trim(std::wstring(title));
        if (normalizedTitle.empty())
        {
            throw std::invalid_argument("Separator title is required.");
        }

        Database database = openInstanceDatabase(projectDirectory);

        Transaction transaction(database);
        syncProfilePluginOrderItems(database, normalizedProfileName, pluginNames);

        const int count = profilePluginOrderItemCount(database, normalizedProfileName);
        const int position = std::clamp(targetIndex, 0, count);
        const std::wstring now = nowUtcText();

        Statement shift = database.prepare(
            "UPDATE profile_plugin_order_items "
            "SET position = position + 1, updated_at = ? "
            "WHERE profile_name = ? AND position >= ?;");
        shift.bindText(1, now);
        shift.bindText(2, normalizedProfileName);
        shift.bindInt(3, position);
        shift.stepDone();

        Statement insert = database.prepare(
            "INSERT INTO profile_plugin_order_items("
            "id, profile_name, kind, plugin_name, separator_title, position, created_at, updated_at"
            ") VALUES(?, ?, 'separator', '', ?, ?, ?, ?);");
        insert.bindText(1, generateUuid());
        insert.bindText(2, normalizedProfileName);
        insert.bindText(3, normalizedTitle);
        insert.bindInt(4, position);
        insert.bindText(5, now);
        insert.bindText(6, now);
        insert.stepDone();

        compactProfilePluginOrderPositions(database, normalizedProfileName);
        transaction.commit();

        return readProfilePluginOrderItems(database, normalizedProfileName);
    }

    std::vector<ProfilePluginOrderItemRecord> InstanceMetadataStore::deleteProfilePluginOrderSeparator(
        const std::filesystem::path& projectDirectory,
        std::wstring_view profileName,
        const std::vector<std::wstring>& pluginNames,
        std::wstring_view separatorId)
    {
        const std::lock_guard metadataLock(metadataStoreMutex());

        if (projectDirectory.empty())
        {
            throw std::invalid_argument("Project directory is required.");
        }

        const std::wstring normalizedProfileName = profileNameOrDefault(profileName);
        const std::wstring id = trim(std::wstring(separatorId));
        if (id.empty())
        {
            throw std::invalid_argument("Separator id is required.");
        }

        Database database = openInstanceDatabase(projectDirectory);
        Transaction transaction(database);
        syncProfilePluginOrderItems(database, normalizedProfileName, pluginNames);

        Statement remove = database.prepare(
            "DELETE FROM profile_plugin_order_items "
            "WHERE profile_name = ? AND id = ? AND kind = 'separator';");
        remove.bindText(1, normalizedProfileName);
        remove.bindText(2, id);
        remove.stepDone();

        compactProfilePluginOrderPositions(database, normalizedProfileName);
        transaction.commit();

        return readProfilePluginOrderItems(database, normalizedProfileName);
    }

    std::vector<ProfilePluginOrderItemRecord> InstanceMetadataStore::moveProfilePluginOrderItem(
        const std::filesystem::path& projectDirectory,
        std::wstring_view profileName,
        const std::vector<std::wstring>& pluginNames,
        std::wstring_view orderItemId,
        int targetIndex)
    {
        const std::lock_guard metadataLock(metadataStoreMutex());

        if (projectDirectory.empty())
        {
            throw std::invalid_argument("Project directory is required.");
        }

        const std::wstring normalizedProfileName = profileNameOrDefault(profileName);
        const std::wstring id = trim(std::wstring(orderItemId));
        if (id.empty())
        {
            throw std::invalid_argument("Plugin order item id is required.");
        }

        Database database = openInstanceDatabase(projectDirectory);
        Transaction transaction(database);
        syncProfilePluginOrderItems(database, normalizedProfileName, pluginNames);

        moveProfileOrderStorageItems(
            database,
            normalizedProfileName,
            "profile_plugin_order_items",
            id,
            targetIndex,
            profilePluginOrderSeparatorKind);

        transaction.commit();
        return readProfilePluginOrderItems(database, normalizedProfileName);
    }

    InstalledModRecord InstanceMetadataStore::registerInstalledMod(
        const std::filesystem::path& projectDirectory,
        const std::filesystem::path& modDirectory,
        std::wstring_view displayName,
        std::wstring_view version,
        const ModSourceRecord& source)
    {
        const std::lock_guard metadataLock(metadataStoreMutex());

        if (modDirectory.empty() || !std::filesystem::exists(modDirectory) || !std::filesystem::is_directory(modDirectory))
        {
            throw std::invalid_argument("Installed mod directory is required.");
        }

        Database database = openInstanceDatabase(projectDirectory);

        InstalledModRecord record;
        record.gameId = readMetadataValue(database, L"game_id");
        record.folderName = modDirectory.filename().wstring();
        record.displayName = displayName.empty() ? record.folderName : std::wstring(displayName);
        record.version = std::wstring(version);
        record.installedAt = nowUtcText();
        record.updatedAt = record.installedAt;
        record.state = L"installed";
        record.contentFingerprint = computeContentFingerprint(modDirectory);
        record.path = modDirectory;
        record.source = source;

        Transaction transaction(database);
        upsertModRecord(database, record);
        transaction.commit();

        record = readRecordByFolder(database, projectDirectory, record.folderName, modDirectory.parent_path());
        writePortableManifest(record);
        return record;
    }

    void InstanceMetadataStore::registerInstalledMods(
        const std::filesystem::path& projectDirectory,
        const std::vector<InstalledModImportRecord>& mods,
        const InstalledModImportProgress& progress)
    {
        const std::lock_guard metadataLock(metadataStoreMutex());

        if (projectDirectory.empty())
        {
            throw std::invalid_argument("Project directory is required.");
        }

        if (mods.empty())
        {
            return;
        }

        std::wstring gameId;
        {
            Database database = openInstanceDatabase(projectDirectory);
            gameId = readMetadataValue(database, L"game_id");
        }

        const std::wstring now = nowUtcText();
        std::vector<InstalledModRecord> records;
        records.reserve(mods.size());
        std::vector<unsigned char> shouldComputeContentFingerprint;
        shouldComputeContentFingerprint.reserve(mods.size());

        for (const InstalledModImportRecord& import : mods)
        {
            if (import.modDirectory.empty() ||
                !std::filesystem::exists(import.modDirectory) ||
                !std::filesystem::is_directory(import.modDirectory))
            {
                throw std::invalid_argument("Installed mod directory is required.");
            }

            InstalledModRecord record;
            record.gameId = gameId;
            record.folderName = import.modDirectory.filename().wstring();
            record.displayName = import.displayName.empty() ? record.folderName : import.displayName;
            record.version = import.version;
            record.installedAt = now;
            record.updatedAt = now;
            record.state = import.isEnabled ? L"installed" : L"disabled";
            record.path = import.modDirectory;
            record.source = import.source;

            records.push_back(std::move(record));
            shouldComputeContentFingerprint.push_back(import.computeContentFingerprint ? 1 : 0);
        }

        std::atomic<std::size_t> nextIndex{0};
        std::exception_ptr firstError;
        std::mutex errorMutex;
        std::mutex progressMutex;
        std::size_t processed = 0;
        const unsigned int hardwareThreads = std::thread::hardware_concurrency();
        const std::size_t detectedWorkers = hardwareThreads == 0 ? 4 : hardwareThreads;
        const std::size_t workerCount = (std::max<std::size_t>)(
            1,
            (std::min<std::size_t>)(
                (std::min<std::size_t>)(detectedWorkers, 8),
                records.size()));

        std::vector<std::thread> workers;
        workers.reserve(workerCount);
        for (std::size_t worker = 0; worker < workerCount; ++worker)
        {
            workers.emplace_back([&]()
            {
                for (;;)
                {
                    const std::size_t index = nextIndex.fetch_add(1, std::memory_order_relaxed);
                    if (index >= records.size())
                    {
                        break;
                    }

                    try
                    {
                        if (shouldComputeContentFingerprint[index])
                        {
                            records[index].contentFingerprint = computeContentFingerprint(records[index].path);
                        }
                    }
                    catch (...)
                    {
                        std::lock_guard lock(errorMutex);
                        if (!firstError)
                        {
                            firstError = std::current_exception();
                        }
                        break;
                    }

                    if (progress)
                    {
                        std::lock_guard lock(progressMutex);
                        ++processed;
                        progress(processed, records.size(), records[index].folderName);
                    }
                }
            });
        }

        for (std::thread& worker : workers)
        {
            worker.join();
        }

        if (firstError)
        {
            std::rethrow_exception(firstError);
        }

        Database database = openInstanceDatabase(projectDirectory);
        Transaction transaction(database);
        for (InstalledModRecord& record : records)
        {
            upsertModRecord(database, record);
        }
        transaction.commit();

        for (const InstalledModRecord& record : records)
        {
            writePortableManifest(record);
        }
    }

    void InstanceMetadataStore::deleteInstalledMod(
        const std::filesystem::path& projectDirectory,
        const std::filesystem::path& modPath)
    {
        const std::lock_guard metadataLock(metadataStoreMutex());

        if (projectDirectory.empty() || modPath.empty())
        {
            throw std::invalid_argument("Project directory and mod path are required.");
        }

        Database database = openInstanceDatabase(projectDirectory);
        const std::wstring folderName = modPath.filename().wstring();

        Transaction transaction(database);
        Statement id = database.prepare("SELECT id FROM mods WHERE folder_name = ? LIMIT 1;");
        id.bindText(1, folderName);
        if (id.stepRow())
        {
            Statement removeCache = database.prepare("DELETE FROM mod_file_cache WHERE mod_id = ?;");
            removeCache.bindInt64(1, std::stoll(id.columnText(0)));
            removeCache.stepDone();
        }

        Statement statement = database.prepare(
            "UPDATE mods SET state = 'deleted', updated_at = ? WHERE folder_name = ?;");
        statement.bindText(1, nowUtcText());
        statement.bindText(2, folderName);
        statement.stepDone();
        transaction.commit();
    }

    void InstanceMetadataStore::setInstalledModEnabled(
        const std::filesystem::path& projectDirectory,
        const std::filesystem::path& modPath,
        bool isEnabled)
    {
        const std::lock_guard metadataLock(metadataStoreMutex());

        if (projectDirectory.empty() || modPath.empty())
        {
            throw std::invalid_argument("Project directory and mod path are required.");
        }

        Database database = openInstanceDatabase(projectDirectory);
        const std::wstring folderName = modPath.filename().wstring();
        InstalledModRecord record =
            readRecordByFolder(database, projectDirectory, folderName, modPath.parent_path());
        if (record.state != L"installed" && record.state != L"disabled")
        {
            throw std::invalid_argument("Only installed mods can be enabled or disabled.");
        }

        const std::wstring nextState = isEnabled ? L"installed" : L"disabled";
        const bool stateChanged = record.state != nextState;
        if (stateChanged)
        {
            record.state = nextState;
            record.updatedAt = nowUtcText();

            Transaction transaction(database);
            Statement statement = database.prepare(
                "UPDATE mods SET state = ?, updated_at = ? "
                "WHERE folder_name = ? AND state IN ('installed', 'disabled') AND state <> ?;");
            statement.bindText(1, record.state);
            statement.bindText(2, record.updatedAt);
            statement.bindText(3, folderName);
            statement.bindText(4, record.state);
            statement.stepDone();
            transaction.commit();

            record = readRecordByFolder(database, projectDirectory, folderName, modPath.parent_path());
        }

        if (portableManifestNeedsWrite(record, stateChanged))
        {
            writePortableManifest(record);
        }
    }

    void InstanceMetadataStore::setAllInstalledModsEnabled(
        const std::filesystem::path& projectDirectory,
        bool isEnabled,
        const std::filesystem::path& modsRoot)
    {
        const std::lock_guard metadataLock(metadataStoreMutex());

        if (projectDirectory.empty())
        {
            throw std::invalid_argument("Project directory is required.");
        }

        Database database = openInstanceDatabase(projectDirectory);
        syncInstalledModsFromDisk(database, projectDirectory, modsRoot);

        const std::wstring nextState = isEnabled ? L"installed" : L"disabled";
        const std::wstring updatedAt = nowUtcText();
        std::vector<InstalledModRecord> records = readInstalledRecords(database, projectDirectory, modsRoot);
        std::vector<InstalledModRecord> manifestsToWrite;
        for (InstalledModRecord& record : records)
        {
            const bool stateChanged = record.state != nextState;
            if (stateChanged)
            {
                record.state = nextState;
                record.updatedAt = updatedAt;
            }

            if (portableManifestNeedsBulkWrite(record, stateChanged))
            {
                manifestsToWrite.push_back(record);
            }
        }

        Transaction transaction(database);
        Statement statement = database.prepare(
            "UPDATE mods SET state = ?, updated_at = ? "
            "WHERE state IN ('installed', 'disabled') AND state <> ?;");
        statement.bindText(1, nextState);
        statement.bindText(2, updatedAt);
        statement.bindText(3, nextState);
        statement.stepDone();
        transaction.commit();

        for (const InstalledModRecord& record : manifestsToWrite)
        {
            writePortableManifest(record);
        }
    }

    void InstanceMetadataStore::recordRemoteCheck(
        const std::filesystem::path& projectDirectory,
        const RemoteCheckRecord& check,
        const std::filesystem::path& modsRoot)
    {
        const std::lock_guard metadataLock(metadataStoreMutex());

        if (projectDirectory.empty())
        {
            throw std::invalid_argument("Project directory is required.");
        }

        Database database = openInstanceDatabase(projectDirectory);
        const std::wstring checkedAt = check.checkedAt.empty() ? nowUtcText() : check.checkedAt;

        ModSourceRecord source = check.source;
        source.provider = normalizeProvider(source);
        source.lastCheckedAt = checkedAt;
        source.latestVersion = check.latestVersion;

        Transaction transaction(database);

        Statement cache = database.prepare(
            "INSERT INTO remote_cache("
            "provider, game_domain, remote_mod_id, remote_file_id, latest_version, payload_json, checked_at"
            ") VALUES(?, ?, ?, ?, ?, ?, ?) "
            "ON CONFLICT(provider, game_domain, remote_mod_id, remote_file_id) DO UPDATE SET "
            "latest_version = excluded.latest_version,"
            "payload_json = excluded.payload_json,"
            "checked_at = excluded.checked_at;");
        cache.bindText(1, source.provider);
        cache.bindText(2, source.gameDomain);
        cache.bindText(3, source.remoteModId);
        cache.bindText(4, source.remoteFileId);
        cache.bindText(5, source.latestVersion);
        cache.bindText(6, check.payloadJson);
        cache.bindText(7, checkedAt);
        cache.stepDone();

        if (!check.folderName.empty())
        {
            Statement updateSource = database.prepare(
                "UPDATE mod_sources SET "
                "provider = ?,"
                "game_domain = ?,"
                "remote_mod_id = ?,"
                "remote_file_id = ?,"
                "url = CASE WHEN ? = '' THEN url ELSE ? END,"
                "last_checked_at = ?,"
                "latest_version = ? "
                "WHERE mod_id = (SELECT id FROM mods WHERE folder_name = ? LIMIT 1);");
            updateSource.bindText(1, source.provider);
            updateSource.bindText(2, source.gameDomain);
            updateSource.bindText(3, source.remoteModId);
            updateSource.bindText(4, source.remoteFileId);
            updateSource.bindText(5, source.url);
            updateSource.bindText(6, source.url);
            updateSource.bindText(7, checkedAt);
            updateSource.bindText(8, source.latestVersion);
            updateSource.bindText(9, check.folderName);
            updateSource.stepDone();
        }

        transaction.commit();

        if (!check.folderName.empty())
        {
            try
            {
                InstalledModRecord record = readRecordByFolder(database, projectDirectory, check.folderName, modsRoot);
                writePortableManifest(record);
            }
            catch (const std::exception&)
            {
            }
        }
    }

    ModFileSummary InstanceMetadataStore::summarizeModFiles(
        const std::filesystem::path& projectDirectory,
        const std::filesystem::path& modPath,
        const std::filesystem::path& modsRoot)
    {
        const std::lock_guard metadataLock(metadataStoreMutex());

        if (projectDirectory.empty() || modPath.empty())
        {
            throw std::invalid_argument("Project directory and mod path are required.");
        }

        Database database = openInstanceDatabase(projectDirectory);
        const std::filesystem::path resolvedModsRoot = modsRoot.empty() ? modPath.parent_path() : modsRoot;
        ensureAllFileCachesFresh(database, projectDirectory, resolvedModsRoot);

        InstalledModRecord record =
            readRecordByFolder(database, projectDirectory, modPath.filename().wstring(), resolvedModsRoot);
        return summarizeCachedModFiles(database, record);
    }

    std::vector<ModFileSummaryRecord> InstanceMetadataStore::summarizeInstalledModFiles(
        const std::filesystem::path& projectDirectory,
        const std::filesystem::path& modsRoot)
    {
        const std::lock_guard metadataLock(metadataStoreMutex());

        if (projectDirectory.empty())
        {
            throw std::invalid_argument("Project directory is required.");
        }

        Database database = openInstanceDatabase(projectDirectory);
        ensureAllFileCachesFresh(database, projectDirectory, modsRoot);

        std::vector<ModFileSummaryRecord> summaries;
        for (const InstalledModRecord& record : readInstalledRecords(database, projectDirectory, modsRoot))
        {
            summaries.push_back(ModFileSummaryRecord{
                record.folderName,
                record.path,
                summarizeCachedModFiles(database, record)
            });
        }

        return summaries;
    }

    std::vector<ModFileSummaryRecord> InstanceMetadataStore::summarizeProfileModFiles(
        const std::filesystem::path& projectDirectory,
        std::wstring_view profileName,
        const std::filesystem::path& modsRoot)
    {
        const std::lock_guard metadataLock(metadataStoreMutex());

        if (projectDirectory.empty())
        {
            throw std::invalid_argument("Project directory is required.");
        }

        Database database = openInstanceDatabase(projectDirectory);
        ensureAllFileCachesPrepared(database, projectDirectory, modsRoot);
        return summarizeCachedProfileModFiles(database, projectDirectory, profileName, modsRoot);
    }

    std::vector<ModFileTreeEntry> InstanceMetadataStore::listModFileTree(
        const std::filesystem::path& projectDirectory,
        const std::filesystem::path& modPath,
        std::wstring_view relativeDirectory,
        const std::filesystem::path& modsRoot)
    {
        const std::lock_guard metadataLock(metadataStoreMutex());

        if (projectDirectory.empty() || modPath.empty())
        {
            throw std::invalid_argument("Project directory and mod path are required.");
        }

        std::filesystem::path requested(relativeDirectory);
        if (requested.is_absolute())
        {
            throw std::invalid_argument("Relative directory is required.");
        }

        requested = requested.lexically_normal();
        std::wstring parent = normalizeRelativePath(requested);
        if (parent == L".")
        {
            parent.clear();
        }

        Database database = openInstanceDatabase(projectDirectory);
        const std::filesystem::path resolvedModsRoot = modsRoot.empty() ? modPath.parent_path() : modsRoot;
        syncInstalledModsFromDisk(database, projectDirectory, resolvedModsRoot);

        InstalledModRecord record =
            readRecordByFolder(database, projectDirectory, modPath.filename().wstring(), resolvedModsRoot);

        Transaction transaction(database);
        ensureFileCacheFresh(database, record);
        transaction.commit();

        Statement statement = database.prepare(
            "SELECT name, relative_path, kind, size, path_key "
            "FROM mod_file_cache "
            "WHERE mod_id = ? AND parent_key = ? "
            "ORDER BY CASE kind WHEN 'directory' THEN 0 ELSE 1 END, name COLLATE NOCASE;");
        statement.bindInt64(1, record.id);
        statement.bindText(2, pathKey(parent));

        std::vector<ModFileTreeEntry> entries;
        while (statement.stepRow())
        {
            const std::wstring kind = statement.columnText(2);
            const bool isDirectory = kind == L"directory";
            const std::wstring itemPathKey = statement.columnText(4);
            std::vector<ConflictOwner> owners = isDirectory
                ? std::vector<ConflictOwner>()
                : conflictOwnersForPath(database, itemPathKey);

            entries.push_back(ModFileTreeEntry{
                statement.columnText(0),
                statement.columnText(1),
                isDirectory,
                isDirectory && hasCachedChildren(database, record.id, itemPathKey),
                static_cast<std::uintmax_t>(statement.columnInt64(3) < 0 ? 0 : statement.columnInt64(3)),
                isDirectory ? std::wstring() : conflictStateForOwners(owners, record.id),
                isDirectory ? std::vector<std::wstring>() : ownerNames(owners)
            });
        }

        return entries;
    }
}

#endif
