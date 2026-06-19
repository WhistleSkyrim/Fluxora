#include "FluxoraCore/Services/FluxPackService.hpp"

#include "FluxoraCore/Services/BuildPathSettingsService.hpp"
#include "FluxoraCore/Services/DownloadService.hpp"
#include "FluxoraCore/Services/Logger.hpp"
#include "FluxoraCore/Services/PathSafetyService.hpp"
#include "FluxoraCore/Services/ProjectService.hpp"
#include "FluxoraCore/Storage/AtomicFileStore.hpp"
#include "FluxoraCore/Storage/InstanceMetadataStore.hpp"
#include "FluxoraCore/Support/JsonReader.hpp"
#include "FluxoraCore/Support/JsonWriter.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <ctime>
#include <cwctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#endif

namespace fluxora
{
    namespace
    {
        constexpr std::wstring_view packageFormat = L"FluxPack";
        constexpr int packageFormatVersion = 1;
        constexpr std::wstring_view metadataExtension = L".fluxora.json";
        constexpr std::wstring_view defaultLocalGameDirectoryName = L"stock game";
        constexpr std::uintmax_t maxEmbeddedTextBytes = 256 * 1024;

        struct DownloadMetadata
        {
            std::wstring source;
            std::wstring gameDomain;
            std::wstring modId;
            std::wstring fileId;
            std::wstring nexusModName;
            std::wstring version;
            std::wstring latestVersion;
            std::wstring destinationFileName;
        };

        struct DownloadSourceFile
        {
            std::filesystem::path path;
            DownloadMetadata metadata;
            std::wstring sha256;
            std::uintmax_t size{0};
        };

        struct PackModReference
        {
            InstalledModRecord mod;
            std::optional<DownloadSourceFile> sourceArchive;
        };

        struct FileManifestEntry
        {
            std::filesystem::path path;
            std::wstring relativePath;
            std::wstring sha256;
            std::uintmax_t size{0};
            std::wstring textContent;
            bool embedsText{false};
        };

        struct FluxPackSourceReference
        {
            std::wstring folderName;
            std::wstring displayName;
            std::wstring version;
            std::wstring archiveFileName;
            std::wstring archiveSha256;
            std::uintmax_t archiveSize{0};
            bool enabled{true};
            bool requiresDownload{true};
            ModSourceRecord source;
        };

        struct FluxPackConfigReference
        {
            std::wstring relativePath;
            std::wstring text;
            bool embedsText{false};
        };

        struct FluxPackProfileOrderReference
        {
            std::wstring kind;
            std::wstring folderName;
            std::wstring separatorTitle;
        };

        struct FluxPackManifest
        {
            FluxPackSummary summary;
            std::wstring buildName;
            std::wstring templateId;
            std::filesystem::path gamePath;
            std::filesystem::path projectDirectoryHint;
            std::wstring defaultProfile;
            std::vector<FluxPackSourceReference> sourceArchives;
            std::vector<FluxPackConfigReference> customConfigs;
            std::vector<FluxPackProfileOrderReference> profileOrder;
        };

        struct ResolvedFluxPackGameDirectory
        {
            std::filesystem::path path;
            bool validateExistingGame{true};
        };

        struct ProviderInstallState
        {
            std::wstring id;
            std::wstring displayName;
            std::uintmax_t total{0};
            std::uintmax_t completed{0};
            std::uintmax_t pending{0};
            std::uintmax_t failed{0};
            std::wstring currentItem;
            std::wstring statusText;
        };

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
            if (size <= 0)
            {
                throw std::runtime_error("Failed to encode FluxPack text as UTF-8.");
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
                throw std::invalid_argument("FluxPack text is not valid UTF-8.");
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

        std::string pathForLog(const std::filesystem::path& path)
        {
            return toUtf8(path.wstring());
        }

        std::string readTextFile(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::in | std::ios::binary);
            if (!file)
            {
                throw std::runtime_error("FluxPack file could not be opened.");
            }

            return std::string(
                std::istreambuf_iterator<char>(file),
                std::istreambuf_iterator<char>());
        }

        std::string tryReadTextFile(const std::filesystem::path& path)
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

        std::wstring nowUtcText()
        {
            const auto now = std::chrono::system_clock::now();
            const std::time_t time = std::chrono::system_clock::to_time_t(now);

            std::tm utc{};
#ifdef _WIN32
            gmtime_s(&utc, &time);
#else
            gmtime_r(&time, &utc);
#endif

            std::wostringstream stream;
            stream << std::put_time(&utc, L"%Y-%m-%dT%H:%M:%SZ");
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

        bool equalsIgnoreCase(std::wstring_view left, std::wstring_view right)
        {
            return toLower(std::wstring(left)) == toLower(std::wstring(right));
        }

        bool pathExists(const std::filesystem::path& path)
        {
            std::error_code error;
            return std::filesystem::exists(path, error);
        }

        std::wstring normalizePathForComparison(const std::filesystem::path& path)
        {
            std::wstring text = std::filesystem::absolute(path).lexically_normal().wstring();
            while (text.size() > 1 && (text.back() == L'\\' || text.back() == L'/'))
            {
                text.pop_back();
            }

#ifdef _WIN32
            text = toLower(std::move(text));
#endif
            return text;
        }

        bool isSameOrInsidePath(
            const std::filesystem::path& candidate,
            const std::filesystem::path& root)
        {
            if (candidate.empty() || root.empty())
            {
                return false;
            }

            const std::wstring candidateText = normalizePathForComparison(candidate);
            const std::wstring rootText = normalizePathForComparison(root);
            if (candidateText == rootText)
            {
                return true;
            }
            if (candidateText.size() <= rootText.size())
            {
                return false;
            }

            const wchar_t separator = candidateText[rootText.size()];
            return (separator == L'\\' || separator == L'/') &&
                candidateText.compare(0, rootText.size(), rootText) == 0;
        }

        std::optional<std::filesystem::path> relativePathInsideRoot(
            const std::filesystem::path& candidate,
            const std::filesystem::path& root)
        {
            if (!isSameOrInsidePath(candidate, root))
            {
                return std::nullopt;
            }

            const std::filesystem::path relative =
                std::filesystem::absolute(candidate)
                    .lexically_normal()
                    .lexically_relative(std::filesystem::absolute(root).lexically_normal());
            if (relative.empty() || relative == L".")
            {
                return std::nullopt;
            }

            return relative.lexically_normal();
        }

        bool containsAny(std::wstring value, const std::vector<std::wstring_view>& needles)
        {
            value = toLower(std::move(value));
            for (std::wstring_view needle : needles)
            {
                if (value.find(needle) != std::wstring::npos)
                {
                    return true;
                }
            }

            return false;
        }

        bool isMetadataSidecar(const std::filesystem::path& path)
        {
            return path.filename().wstring().ends_with(metadataExtension);
        }

        bool isHex8(std::wstring_view value)
        {
            if (value.size() != 8)
            {
                return false;
            }

            return std::all_of(value.begin(), value.end(), [](wchar_t character)
            {
                return (character >= L'0' && character <= L'9') ||
                    (character >= L'a' && character <= L'f') ||
                    (character >= L'A' && character <= L'F');
            });
        }

        bool isAtomicBackupFile(const std::filesystem::path& path)
        {
            const std::wstring name = path.filename().wstring();
            return name.size() == 11 &&
                name.rfind(L".fb", 0) == 0 &&
                isHex8(std::wstring_view(name).substr(3));
        }

        bool isTransientDownloadFile(const std::filesystem::path& path)
        {
            const std::wstring fileName = toLower(path.filename().wstring());
            return fileName.ends_with(L".part") ||
                fileName.ends_with(L".tmp") ||
                fileName.ends_with(L".meta") ||
                fileName.ends_with(L".cancel") ||
                fileName.ends_with(metadataExtension) ||
                isAtomicBackupFile(path);
        }

        std::filesystem::path metadataPath(const std::filesystem::path& path)
        {
            return std::filesystem::path(path.wstring() + std::wstring(metadataExtension));
        }

        std::wstring readStringOrDefault(
            const JsonValue& object,
            std::wstring_view field,
            std::wstring_view fallback = L"")
        {
            const JsonValue* value = object.find(field);
            if (value == nullptr || !value->isString())
            {
                return std::wstring(fallback);
            }

            return value->asString();
        }

        bool readBoolOrDefault(
            const JsonValue& object,
            std::wstring_view field,
            bool fallback = false)
        {
            const JsonValue* value = object.find(field);
            return value != nullptr && value->type() == JsonValue::Type::Boolean
                ? value->asBoolean()
                : fallback;
        }

        std::uintmax_t readUnsignedOrDefault(
            const JsonValue& object,
            std::wstring_view field,
            std::uintmax_t fallback = 0)
        {
            const JsonValue* value = object.find(field);
            if (value == nullptr)
            {
                return fallback;
            }

            try
            {
                if (value->isNumber())
                {
                    return static_cast<std::uintmax_t>(std::stoull(value->asNumber()));
                }
                if (value->isString())
                {
                    return static_cast<std::uintmax_t>(std::stoull(value->asString()));
                }
            }
            catch (const std::exception&)
            {
            }

            return fallback;
        }

        std::uintmax_t arraySize(const JsonValue& object, std::wstring_view field)
        {
            const JsonValue* value = object.find(field);
            return value != nullptr && value->isArray()
                ? static_cast<std::uintmax_t>(value->asArray().size())
                : 0;
        }

        std::wstring readHashValueOrDefault(const JsonValue& object, std::wstring_view field)
        {
            const JsonValue* value = object.find(field);
            if (value == nullptr)
            {
                return {};
            }
            if (value->isObject())
            {
                return readStringOrDefault(*value, L"value");
            }
            if (value->isString())
            {
                return value->asString();
            }

            return {};
        }

        std::wstring sourceUrlForPack(const ModSourceRecord& source)
        {
            const std::wstring provider = toLower(source.provider);
            if ((provider == L"nexus" || provider.empty()) &&
                !source.gameDomain.empty() &&
                !source.remoteModId.empty())
            {
                if (!source.remoteFileId.empty())
                {
                    return L"nxm://" + source.gameDomain + L"/mods/" + source.remoteModId + L"/files/" + source.remoteFileId;
                }

                if (source.url.empty())
                {
                    return L"https://www.nexusmods.com/" + source.gameDomain + L"/mods/" + source.remoteModId;
                }
            }

            if (!source.url.empty())
            {
                return source.url;
            }

            return {};
        }

        DownloadMetadata readDownloadMetadata(const std::filesystem::path& archivePath)
        {
            const std::string content = tryReadTextFile(metadataPath(archivePath));
            if (content.empty())
            {
                return {};
            }

            try
            {
                const JsonValue root = JsonReader::parse(fromUtf8(content));
                if (!root.isObject())
                {
                    return {};
                }

                return DownloadMetadata{
                    readStringOrDefault(root, L"source"),
                    readStringOrDefault(root, L"gameDomain"),
                    readStringOrDefault(root, L"modId"),
                    readStringOrDefault(root, L"fileId"),
                    readStringOrDefault(root, L"nexusModName", readStringOrDefault(root, L"modName")),
                    readStringOrDefault(root, L"version"),
                    readStringOrDefault(root, L"latestVersion"),
                    readStringOrDefault(root, L"destinationFileName")
                };
            }
            catch (const std::exception&)
            {
                return {};
            }
        }

        std::wstring bytesToHex(const unsigned char* bytes, std::size_t size)
        {
            std::wostringstream stream;
            stream << std::hex << std::setfill(L'0');
            for (std::size_t index = 0; index < size; ++index)
            {
                stream << std::setw(2) << static_cast<int>(bytes[index]);
            }

            return stream.str();
        }

        std::wstring sha256File(const std::filesystem::path& path)
        {
#ifdef _WIN32
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            BCRYPT_HASH_HANDLE hash = nullptr;
            std::vector<unsigned char> object;
            std::vector<unsigned char> digest;
            try
            {
                if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
                {
                    throw std::runtime_error("Failed to open SHA-256 provider.");
                }

                DWORD objectLength = 0;
                DWORD returned = 0;
                if (BCryptGetProperty(
                        algorithm,
                        BCRYPT_OBJECT_LENGTH,
                        reinterpret_cast<PUCHAR>(&objectLength),
                        sizeof(objectLength),
                        &returned,
                        0) < 0)
                {
                    throw std::runtime_error("Failed to query SHA-256 object size.");
                }

                DWORD hashLength = 0;
                if (BCryptGetProperty(
                        algorithm,
                        BCRYPT_HASH_LENGTH,
                        reinterpret_cast<PUCHAR>(&hashLength),
                        sizeof(hashLength),
                        &returned,
                        0) < 0)
                {
                    throw std::runtime_error("Failed to query SHA-256 hash size.");
                }

                object.resize(objectLength);
                digest.resize(hashLength);
                if (BCryptCreateHash(
                        algorithm,
                        &hash,
                        object.data(),
                        static_cast<ULONG>(object.size()),
                        nullptr,
                        0,
                        0) < 0)
                {
                    throw std::runtime_error("Failed to create SHA-256 hash.");
                }

                std::ifstream file(path, std::ios::in | std::ios::binary);
                if (!file)
                {
                    throw std::runtime_error("File could not be opened for hashing.");
                }

                std::vector<char> buffer(64 * 1024);
                while (file)
                {
                    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                    const std::streamsize read = file.gcount();
                    if (read > 0 &&
                        BCryptHashData(
                            hash,
                            reinterpret_cast<PUCHAR>(buffer.data()),
                            static_cast<ULONG>(read),
                            0) < 0)
                    {
                        throw std::runtime_error("Failed to update SHA-256 hash.");
                    }
                }

                if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
                {
                    throw std::runtime_error("Failed to finalize SHA-256 hash.");
                }

                BCryptDestroyHash(hash);
                BCryptCloseAlgorithmProvider(algorithm, 0);
                return bytesToHex(digest.data(), digest.size());
            }
            catch (const std::exception&)
            {
                if (hash != nullptr)
                {
                    BCryptDestroyHash(hash);
                }
                if (algorithm != nullptr)
                {
                    BCryptCloseAlgorithmProvider(algorithm, 0);
                }
                throw;
            }
#else
            static_cast<void>(path);
            return {};
#endif
        }

        bool sourceHasRemoteIdentity(const ModSourceRecord& source)
        {
            const std::wstring provider = toLower(source.provider);
            return provider == L"nexus" ||
                provider == L"github" ||
                provider == L"mega" ||
                provider == L"modernflow" ||
                provider == L"modern-flow" ||
                !source.url.empty() ||
                !source.remoteModId.empty() ||
                !source.remoteFileId.empty();
        }

        bool sourceMatches(const DownloadMetadata& metadata, const ModSourceRecord& source)
        {
            if (!source.gameDomain.empty() &&
                !metadata.gameDomain.empty() &&
                !equalsIgnoreCase(source.gameDomain, metadata.gameDomain))
            {
                return false;
            }

            if (!source.remoteModId.empty() &&
                !metadata.modId.empty() &&
                !equalsIgnoreCase(source.remoteModId, metadata.modId))
            {
                return false;
            }

            if (!source.remoteFileId.empty() &&
                !metadata.fileId.empty() &&
                !equalsIgnoreCase(source.remoteFileId, metadata.fileId))
            {
                return false;
            }

            if (!source.remoteModId.empty() || !source.remoteFileId.empty())
            {
                if (!source.remoteModId.empty() && metadata.modId.empty())
                {
                    return false;
                }
                if (!source.remoteFileId.empty() && metadata.fileId.empty())
                {
                    return false;
                }

                return true;
            }

            if (!metadata.modId.empty() || !metadata.fileId.empty())
            {
                return false;
            }

            if (!source.url.empty() && !metadata.source.empty())
            {
                return equalsIgnoreCase(source.url, metadata.source);
            }

            return false;
        }

        std::vector<DownloadSourceFile> buildDownloadIndex(const std::filesystem::path& downloadsDirectory)
        {
            std::vector<DownloadSourceFile> files;
            std::error_code error;
            if (downloadsDirectory.empty() ||
                !std::filesystem::exists(downloadsDirectory, error) ||
                !std::filesystem::is_directory(downloadsDirectory, error))
            {
                return files;
            }

            for (const auto& entry : std::filesystem::directory_iterator(
                     downloadsDirectory,
                     std::filesystem::directory_options::skip_permission_denied,
                     error))
            {
                if (error)
                {
                    break;
                }

                std::error_code statusError;
                if (!entry.is_regular_file(statusError) ||
                    isMetadataSidecar(entry.path()) ||
                    isTransientDownloadFile(entry.path()))
                {
                    continue;
                }

                std::error_code sizeError;
                const std::uintmax_t size = entry.file_size(sizeError);
                files.push_back(DownloadSourceFile{
                    entry.path(),
                    readDownloadMetadata(entry.path()),
                    {},
                    sizeError ? 0 : size
                });
            }

            return files;
        }

        std::optional<DownloadSourceFile> matchSourceArchive(
            const InstalledModRecord& mod,
            std::vector<DownloadSourceFile>& downloads)
        {
            const auto match = std::find_if(
                downloads.begin(),
                downloads.end(),
                [&mod](const DownloadSourceFile& file)
                {
                    return sourceMatches(file.metadata, mod.source);
                });
            if (match == downloads.end())
            {
                return std::nullopt;
            }

            if (match->sha256.empty())
            {
                match->sha256 = sha256File(match->path);
            }

            return *match;
        }

        bool isGeneratedAssetMod(const InstalledModRecord& mod)
        {
            const std::wstring name = mod.folderName + L" " + mod.displayName;
            return containsAny(
                name,
                {
                    L"netlod",
                    L"netloda",
                    L"lodgen",
                    L"lod gen",
                    L"xlodgen",
                    L"dyndolod",
                    L"texgen",
                    L"loadgen",
                    L"load gen",
                    L"synthesis",
                    L"nemesis",
                    L"bodyslide",
                    L"body slide",
                    L"buddyslide",
                    L"pandora"
                });
        }

        bool isConfigFile(const std::filesystem::path& path)
        {
            const std::wstring extension = toLower(path.extension().wstring());
            const std::wstring fileName = toLower(path.filename().wstring());
            return extension == L".ini" ||
                extension == L".cfg" ||
                extension == L".toml" ||
                extension == L".json" ||
                extension == L".yaml" ||
                extension == L".yml" ||
                extension == L".xml" ||
                fileName == L"plugins.txt" ||
                fileName == L"loadorder.txt" ||
                fileName == L"modlist.txt";
        }

        std::wstring relativeToProject(
            const std::filesystem::path& path,
            const std::filesystem::path& projectDirectory)
        {
            std::error_code error;
            std::filesystem::path relative = std::filesystem::relative(path, projectDirectory, error);
            if (error || relative.empty())
            {
                relative = path.lexically_normal();
            }

            return relative.generic_wstring();
        }

        std::filesystem::path normalizePathForFluxPack(
            const std::filesystem::path& path,
            const std::filesystem::path& relativeRoot = {})
        {
            if (path.empty())
            {
                return {};
            }

            std::filesystem::path resolved = path;
            if (resolved.is_relative() && !relativeRoot.empty())
            {
                resolved = relativeRoot / resolved;
            }

            return std::filesystem::absolute(resolved).lexically_normal();
        }

        FileManifestEntry buildFileManifestEntry(
            const std::filesystem::path& path,
            const std::filesystem::path& projectDirectory,
            bool embedText)
        {
            std::error_code sizeError;
            const std::uintmax_t size = std::filesystem::file_size(path, sizeError);
            FileManifestEntry entry{
                path,
                relativeToProject(path, projectDirectory),
                sha256File(path),
                sizeError ? 0 : size,
                {},
                false
            };

            if (embedText && entry.size <= maxEmbeddedTextBytes)
            {
                try
                {
                    entry.textContent = fromUtf8(tryReadTextFile(path));
                    entry.embedsText = true;
                }
                catch (const std::exception&)
                {
                    entry.textContent.clear();
                    entry.embedsText = false;
                }
            }

            return entry;
        }

        std::vector<FileManifestEntry> scanFiles(
            const std::filesystem::path& root,
            const std::filesystem::path& projectDirectory,
            bool configOnly,
            bool embedText)
        {
            std::vector<FileManifestEntry> files;
            std::error_code error;
            if (root.empty() ||
                !std::filesystem::exists(root, error) ||
                !std::filesystem::is_directory(root, error))
            {
                return files;
            }

            for (const auto& entry : std::filesystem::recursive_directory_iterator(
                     root,
                     std::filesystem::directory_options::skip_permission_denied,
                     error))
            {
                if (error)
                {
                    break;
                }

                std::error_code statusError;
                if (!entry.is_regular_file(statusError))
                {
                    continue;
                }

                if (configOnly && !isConfigFile(entry.path()))
                {
                    continue;
                }

                files.push_back(buildFileManifestEntry(entry.path(), projectDirectory, embedText));
            }

            std::sort(files.begin(), files.end(), [](const FileManifestEntry& left, const FileManifestEntry& right)
            {
                return left.relativePath < right.relativePath;
            });
            return files;
        }

        void writeHash(
            JsonWriter& writer,
            std::wstring_view value,
            std::wstring_view status)
        {
            writer.beginObject();
            writer.field(L"algorithm", L"sha256");
            writer.field(L"value", value);
            writer.field(L"status", status);
            writer.endObject();
        }

        void writeSource(JsonWriter& writer, const ModSourceRecord& source)
        {
            writer.beginObject();
            writer.field(L"provider", source.provider);
            writer.field(L"gameDomain", source.gameDomain);
            writer.field(L"remoteModId", source.remoteModId);
            writer.field(L"remoteFileId", source.remoteFileId);
            writer.field(L"url", sourceUrlForPack(source));
            writer.field(L"latestVersion", source.latestVersion);
            writer.field(L"lastCheckedAt", source.lastCheckedAt);
            writer.endObject();
        }

        void writeFileEntry(JsonWriter& writer, const FileManifestEntry& file)
        {
            writer.beginObject();
            writer.field(L"relativePath", file.relativePath);
            writer.field(L"size", file.size);
            writer.key(L"hash");
            writeHash(writer, file.sha256, file.sha256.empty() ? L"unavailable" : L"matched");
            writer.field(L"embedsText", file.embedsText);
            if (file.embedsText)
            {
                writer.field(L"text", file.textContent);
            }
            writer.endObject();
        }

        void writeFileEntries(JsonWriter& writer, const std::vector<FileManifestEntry>& files)
        {
            writer.beginArray();
            for (const FileManifestEntry& file : files)
            {
                writeFileEntry(writer, file);
            }
            writer.endArray();
        }

        void writeModReference(
            JsonWriter& writer,
            const PackModReference& reference,
            bool includeFileManifest,
            bool requiresDownload,
            const std::filesystem::path& projectDirectory)
        {
            const InstalledModRecord& mod = reference.mod;
            writer.beginObject();
            writer.field(L"id", mod.uuid.empty() ? mod.folderName : mod.uuid);
            writer.field(L"folderName", mod.folderName);
            writer.field(L"displayName", mod.displayName.empty() ? mod.folderName : mod.displayName);
            writer.field(L"version", mod.version);
            writer.field(L"enabled", !equalsIgnoreCase(mod.state, L"disabled"));
            writer.field(L"contentFingerprint", mod.contentFingerprint);
            writer.field(L"installedAt", mod.installedAt);
            writer.field(L"updatedAt", mod.updatedAt);
            writer.key(L"source");
            writeSource(writer, mod.source);
            writer.key(L"archiveHash");
            if (reference.sourceArchive.has_value())
            {
                writeHash(writer, reference.sourceArchive->sha256, L"matched-local-download");
            }
            else
            {
                writeHash(writer, L"", L"source-archive-not-included");
            }
            if (reference.sourceArchive.has_value())
            {
                writer.field(L"archiveFileName", reference.sourceArchive->path.filename().wstring());
                writer.field(L"archiveSize", reference.sourceArchive->size);
            }
            else
            {
                writer.field(L"archiveFileName", L"");
                writer.field(L"archiveSize", static_cast<std::uintmax_t>(0));
            }
            writer.field(L"requiresDownload", requiresDownload);

            if (includeFileManifest)
            {
                writer.key(L"files");
                writeFileEntries(writer, scanFiles(mod.path, projectDirectory, false, false));
            }
            writer.endObject();
        }

        void writeModReferences(
            JsonWriter& writer,
            const std::vector<PackModReference>& references,
            bool includeFileManifest,
            bool requiresDownload,
            const std::filesystem::path& projectDirectory)
        {
            writer.beginArray();
            for (const PackModReference& reference : references)
            {
                writeModReference(writer, reference, includeFileManifest, requiresDownload, projectDirectory);
            }
            writer.endArray();
        }

        void writeProfileOrder(
            JsonWriter& writer,
            const std::vector<ProfileOrderItemRecord>& order)
        {
            writer.beginArray();
            for (const ProfileOrderItemRecord& item : order)
            {
                writer.beginObject();
                writer.field(L"kind", item.kind);
                writer.field(L"position", item.position);
                if (item.hasMod)
                {
                    writer.field(L"folderName", item.mod.folderName);
                    writer.field(L"enabled", !equalsIgnoreCase(item.mod.state, L"disabled"));
                }
                else
                {
                    writer.field(L"separatorTitle", item.separatorTitle);
                }
                writer.endObject();
            }
            writer.endArray();
        }

        void writeInstallPlan(
            JsonWriter& writer,
            const ProjectOpenResult& project,
            const BuildPathSettings& paths,
            const std::vector<ProfileOrderItemRecord>& profileOrder,
            bool includeGeneratedAssets)
        {
            const std::wstring defaultProfile = project.resolvedTemplate.defaultProfileName.empty()
                ? L"Default"
                : project.resolvedTemplate.defaultProfileName;

            writer.beginObject();
            writer.field(L"version", 1);
            writer.field(L"defaultProfile", defaultProfile);
            writer.key(L"stages").beginArray();

            writer.beginObject();
            writer.field(L"id", L"source-archives");
            writer.field(L"title", L"Download and verify source archives");
            writer.field(L"policy", L"reference-only");
            writer.stringArray(L"requires", {});
            writer.endObject();

            writer.beginObject();
            writer.field(L"id", L"generated-assets");
            writer.field(L"title", L"Restore approved generated assets");
            writer.field(L"policy", includeGeneratedAssets ? L"approved-manifest" : L"user-confirmation-required");
            writer.stringArray(L"requires", {L"source-archives"});
            writer.endObject();

            writer.beginObject();
            writer.field(L"id", L"custom-patches");
            writer.field(L"title", L"Restore custom patches");
            writer.field(L"policy", L"project-local-manifest");
            writer.stringArray(L"requires", {L"source-archives"});
            writer.endObject();

            writer.beginObject();
            writer.field(L"id", L"custom-configs");
            writer.field(L"title", L"Apply profiles and configuration presets");
            writer.field(L"policy", L"text-config-embedded-when-small");
            writer.stringArray(L"requires", {L"generated-assets", L"custom-patches"});
            writer.endObject();

            writer.endArray();
            writer.key(L"profileOrder");
            writeProfileOrder(writer, profileOrder);
            writer.key(L"targetPaths").beginObject();
            writer.field(L"modsDirectory", relativeToProject(paths.modsDirectory, project.project.projectDirectory));
            writer.field(L"profilesDirectory", relativeToProject(paths.profilesDirectory, project.project.projectDirectory));
            writer.field(L"downloadsDirectory", relativeToProject(paths.downloadsDirectory, project.project.projectDirectory));
            writer.field(L"overwriteDirectory", relativeToProject(paths.overwriteDirectory, project.project.projectDirectory));
            writer.endObject();
            writer.endObject();
        }

        std::wstring serializeFluxPack(
            const ProjectOpenResult& project,
            const BuildPathSettings& paths,
            const std::vector<PackModReference>& sourceArchives,
            const std::vector<PackModReference>& generatedAssets,
            const std::vector<PackModReference>& customPatches,
            const std::vector<FileManifestEntry>& customConfigs,
            const std::vector<ProfileOrderItemRecord>& profileOrder,
            bool includeGeneratedAssets)
        {
            JsonWriter writer;
            writer.beginObject();
            writer.field(L"format", packageFormat);
            writer.field(L"formatVersion", packageFormatVersion);
            writer.field(L"createdAtUtc", nowUtcText());
            writer.key(L"build").beginObject();
            writer.field(L"name", project.project.name);
            writer.field(L"templateId", project.project.templateId);
            writer.field(L"gameName", project.project.gameName);
            writer.field(
                L"gamePath",
                (paths.gameDirectory.empty() ? project.project.gamePath : paths.gameDirectory).wstring());
            writer.field(L"projectDirectoryHint", project.project.projectDirectory.wstring());
            writer.field(L"defaultProfile", project.resolvedTemplate.defaultProfileName);
            writer.endObject();

            writer.key(L"policies").beginObject();
            writer.field(L"sourceArchives", L"reference-only");
            writer.field(L"generatedAssets", includeGeneratedAssets ? L"approved-manifest" : L"confirm-before-including");
            writer.field(L"customPatches", L"project-local-manifest");
            writer.field(L"customConfigs", L"small-text-embedded");
            writer.endObject();

            writer.key(L"sourceArchives");
            writeModReferences(writer, sourceArchives, false, true, project.project.projectDirectory);
            writer.key(L"generatedAssets");
            writeModReferences(writer, generatedAssets, includeGeneratedAssets, false, project.project.projectDirectory);
            writer.key(L"customPatches");
            writeModReferences(writer, customPatches, true, false, project.project.projectDirectory);
            writer.key(L"customConfigs");
            writeFileEntries(writer, customConfigs);
            writer.key(L"installPlan");
            writeInstallPlan(writer, project, paths, profileOrder, includeGeneratedAssets);
            writer.endObject();
            return writer.str();
        }

        FluxPackSummary summaryFromJson(
            const JsonValue& root,
            const std::filesystem::path& path,
            std::uintmax_t manifestBytes)
        {
            if (!root.isObject())
            {
                throw std::invalid_argument("FluxPack manifest must be a JSON object.");
            }

            if (readStringOrDefault(root, L"format") != packageFormat)
            {
                throw std::invalid_argument("Selected file is not a FluxPack manifest.");
            }

            FluxPackSummary summary;
            summary.outputPath = path;
            summary.formatVersion = packageFormatVersion;
            summary.manifestBytes = manifestBytes;
            summary.sourceArchiveCount = arraySize(root, L"sourceArchives");
            summary.generatedAssetCount = arraySize(root, L"generatedAssets");
            summary.customPatchCount = arraySize(root, L"customPatches");
            summary.customConfigCount = arraySize(root, L"customConfigs");
            summary.installStepCount = 0;
            if (const JsonValue* installPlan = root.find(L"installPlan");
                installPlan != nullptr && installPlan->isObject())
            {
                summary.installPlanAvailable = true;
                summary.installStepCount = arraySize(*installPlan, L"stages");
            }

            if (const JsonValue* build = root.find(L"build");
                build != nullptr && build->isObject())
            {
                summary.buildName = readStringOrDefault(*build, L"name");
            }

            if (const JsonValue* policies = root.find(L"policies");
                policies != nullptr && policies->isObject())
            {
                summary.generatedAssetsIncluded =
                    readStringOrDefault(*policies, L"generatedAssets") == L"approved-manifest";
            }

            return summary;
        }

        bool startsWithIgnoreCase(std::wstring_view value, std::wstring_view prefix)
        {
            if (value.size() < prefix.size())
            {
                return false;
            }

            return toLower(std::wstring(value.substr(0, prefix.size()))) == toLower(std::wstring(prefix));
        }

        std::wstring trimWhitespace(std::wstring value)
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

        bool containsIgnoreCase(std::wstring_view value, std::wstring_view needle)
        {
            return toLower(std::wstring(value)).find(toLower(std::wstring(needle))) != std::wstring::npos;
        }

        ModSourceRecord readModSourceRecord(const JsonValue& value)
        {
            if (!value.isObject())
            {
                return {};
            }

            return ModSourceRecord{
                readStringOrDefault(value, L"provider"),
                readStringOrDefault(value, L"gameDomain"),
                readStringOrDefault(value, L"remoteModId"),
                readStringOrDefault(value, L"remoteFileId"),
                readStringOrDefault(value, L"url"),
                readStringOrDefault(value, L"lastCheckedAt"),
                readStringOrDefault(value, L"latestVersion")
            };
        }

        std::wstring providerIdForSource(const FluxPackSourceReference& reference)
        {
            const std::wstring provider = toLower(reference.source.provider);
            const std::wstring url = toLower(reference.source.url);
            if (provider == L"nexus" ||
                startsWithIgnoreCase(reference.source.url, L"nxm://") ||
                url.find(L"nexusmods.com") != std::wstring::npos ||
                (!reference.source.gameDomain.empty() &&
                 (!reference.source.remoteModId.empty() || !reference.source.remoteFileId.empty())))
            {
                return L"nexus";
            }
            if (provider == L"github" || url.find(L"github.com") != std::wstring::npos)
            {
                return L"github";
            }
            if (provider == L"mega" || url.find(L"mega.nz") != std::wstring::npos)
            {
                return L"mega";
            }
            if (provider == L"modernflow" || provider == L"modern-flow")
            {
                return L"modernflow";
            }
            if (!provider.empty())
            {
                return provider;
            }
            return reference.source.url.empty() ? L"unknown" : L"direct";
        }

        std::wstring providerDisplayName(std::wstring_view providerId)
        {
            const std::wstring id = toLower(std::wstring(providerId));
            if (id == L"nexus")
            {
                return L"Nexus Mods";
            }
            if (id == L"github")
            {
                return L"GitHub";
            }
            if (id == L"mega")
            {
                return L"MEGA";
            }
            if (id == L"modernflow")
            {
                return L"ModernFlow";
            }
            if (id == L"direct")
            {
                return L"Прямая ссылка";
            }
            return id.empty() || id == L"unknown" ? L"Другие источники" : std::wstring(providerId);
        }

        std::wstring sourceInstallName(const FluxPackSourceReference& reference)
        {
            if (!reference.displayName.empty())
            {
                return reference.displayName;
            }
            if (!reference.folderName.empty())
            {
                return reference.folderName;
            }
            if (!reference.source.remoteModId.empty())
            {
                return L"Mod " + reference.source.remoteModId;
            }
            return L"Мод";
        }

        std::wstring nxmLinkForSource(const FluxPackSourceReference& reference)
        {
            if (startsWithIgnoreCase(reference.source.url, L"nxm://"))
            {
                return reference.source.url;
            }

            if (!reference.source.gameDomain.empty() &&
                !reference.source.remoteModId.empty() &&
                !reference.source.remoteFileId.empty())
            {
                return L"nxm://" + reference.source.gameDomain +
                    L"/mods/" + reference.source.remoteModId +
                    L"/files/" + reference.source.remoteFileId;
            }

            return {};
        }

        std::wstring fluxPackDownloadFailureStatus(const std::vector<DownloadEntry>& downloaded)
        {
            if (downloaded.empty())
            {
                return L"Загрузка не стартовала";
            }

            std::wstring status = trimWhitespace(downloaded.front().status);
            if (status.empty())
            {
                return L"Загрузка не стартовала";
            }

            constexpr std::wstring_view waitingPrefix = L"Ожидает загрузки";
            if (startsWithIgnoreCase(status, waitingPrefix))
            {
                std::wstring details = trimWhitespace(status.substr(waitingPrefix.size()));
                if (!details.empty() && (details.front() == L':' || details.front() == L'-'))
                {
                    details.erase(details.begin());
                    details = trimWhitespace(std::move(details));
                }

                return details.empty()
                    ? L"Загрузка не стартовала"
                    : L"Ошибка загрузки: " + details;
            }

            return status;
        }

        std::vector<FluxPackSourceReference> readSourceReferences(const JsonValue& root)
        {
            const JsonValue* value = root.find(L"sourceArchives");
            if (value == nullptr || !value->isArray())
            {
                return {};
            }

            std::vector<FluxPackSourceReference> references;
            for (const JsonValue& item : value->asArray())
            {
                if (!item.isObject())
                {
                    continue;
                }

                FluxPackSourceReference reference;
                reference.folderName = readStringOrDefault(item, L"folderName");
                reference.displayName = readStringOrDefault(item, L"displayName", reference.folderName);
                reference.version = readStringOrDefault(item, L"version");
                reference.archiveFileName = readStringOrDefault(item, L"archiveFileName");
                reference.archiveSha256 = readHashValueOrDefault(item, L"archiveHash");
                reference.archiveSize = readUnsignedOrDefault(item, L"archiveSize");
                reference.enabled = readBoolOrDefault(item, L"enabled", true);
                reference.requiresDownload = readBoolOrDefault(item, L"requiresDownload", true);
                if (const JsonValue* source = item.find(L"source"); source != nullptr)
                {
                    reference.source = readModSourceRecord(*source);
                }

                references.push_back(std::move(reference));
            }

            return references;
        }

        std::vector<FluxPackConfigReference> readCustomConfigReferences(const JsonValue& root)
        {
            const JsonValue* value = root.find(L"customConfigs");
            if (value == nullptr || !value->isArray())
            {
                return {};
            }

            std::vector<FluxPackConfigReference> references;
            for (const JsonValue& item : value->asArray())
            {
                if (!item.isObject())
                {
                    continue;
                }

                references.push_back(FluxPackConfigReference{
                    readStringOrDefault(item, L"relativePath"),
                    readStringOrDefault(item, L"text"),
                    readBoolOrDefault(item, L"embedsText", false)
                });
            }

            return references;
        }

        std::vector<FluxPackProfileOrderReference> readProfileOrderReferences(const JsonValue& root)
        {
            const JsonValue* installPlan = root.find(L"installPlan");
            if (installPlan == nullptr || !installPlan->isObject())
            {
                return {};
            }

            const JsonValue* value = installPlan->find(L"profileOrder");
            if (value == nullptr || !value->isArray())
            {
                return {};
            }

            std::vector<FluxPackProfileOrderReference> references;
            for (const JsonValue& item : value->asArray())
            {
                if (!item.isObject())
                {
                    continue;
                }

                references.push_back(FluxPackProfileOrderReference{
                    readStringOrDefault(item, L"kind"),
                    readStringOrDefault(item, L"folderName"),
                    readStringOrDefault(item, L"separatorTitle")
                });
            }

            return references;
        }

        FluxPackManifest parseFluxPackManifest(
            const std::filesystem::path& absolutePath,
            const JsonValue& root,
            std::uintmax_t manifestBytes)
        {
            FluxPackManifest manifest;
            manifest.summary = summaryFromJson(root, absolutePath, manifestBytes);
            if (const JsonValue* build = root.find(L"build");
                build != nullptr && build->isObject())
            {
                manifest.buildName = readStringOrDefault(*build, L"name", manifest.summary.buildName);
                manifest.templateId = readStringOrDefault(*build, L"templateId");
                manifest.gamePath = std::filesystem::path(readStringOrDefault(*build, L"gamePath"));
                manifest.projectDirectoryHint =
                    std::filesystem::path(readStringOrDefault(*build, L"projectDirectoryHint"));
                manifest.defaultProfile = readStringOrDefault(*build, L"defaultProfile");
            }

            if (manifest.buildName.empty())
            {
                manifest.buildName = absolutePath.stem().wstring();
            }

            if (manifest.defaultProfile.empty())
            {
                if (const JsonValue* installPlan = root.find(L"installPlan");
                    installPlan != nullptr && installPlan->isObject())
                {
                    manifest.defaultProfile = readStringOrDefault(*installPlan, L"defaultProfile");
                }
            }
            if (manifest.defaultProfile.empty())
            {
                manifest.defaultProfile = L"Default";
            }

            manifest.sourceArchives = readSourceReferences(root);
            manifest.customConfigs = readCustomConfigReferences(root);
            manifest.profileOrder = readProfileOrderReferences(root);
            return manifest;
        }

        std::optional<std::filesystem::path> safeRelativePath(std::wstring_view value)
        {
            std::wstring text(value);
            std::replace(text.begin(), text.end(), L'/', std::filesystem::path::preferred_separator);
            const std::filesystem::path path(text);
            if (path.empty() || path.is_absolute())
            {
                return std::nullopt;
            }

            const std::filesystem::path normalized = path.lexically_normal();
            if (normalized.empty() || normalized == L".")
            {
                return std::nullopt;
            }

            for (const std::filesystem::path& part : normalized)
            {
                if (part == L"..")
                {
                    return std::nullopt;
                }
            }

            return normalized;
        }

        std::filesystem::path gameDirectoryFromCandidate(std::filesystem::path path)
        {
            if (equalsIgnoreCase(path.extension().wstring(), L".exe"))
            {
                path = path.parent_path();
            }

            return std::filesystem::absolute(path).lexically_normal();
        }

        std::filesystem::path localGameDirectoryFromRelative(
            const std::filesystem::path& projectDirectory,
            const std::filesystem::path& relativePath)
        {
            if (relativePath.empty())
            {
                return gameDirectoryFromCandidate(
                    projectDirectory / std::filesystem::path(std::wstring(defaultLocalGameDirectoryName)));
            }

            return gameDirectoryFromCandidate(projectDirectory / relativePath);
        }

        ResolvedFluxPackGameDirectory resolveInstallGameDirectory(
            const FluxPackManifest& manifest,
            const BuildPathSettingsService& pathSettings,
            Logger& logger,
            const std::filesystem::path& projectDirectory)
        {
            const std::filesystem::path projectDirectoryHint =
                normalizePathForFluxPack(manifest.projectDirectoryHint);
            const std::filesystem::path fallbackLocalGameDirectory =
                localGameDirectoryFromRelative(projectDirectory, {});

            if (!manifest.gamePath.empty())
            {
                if (manifest.gamePath.is_relative())
                {
                    return ResolvedFluxPackGameDirectory{
                        localGameDirectoryFromRelative(projectDirectory, manifest.gamePath),
                        false
                    };
                }

                const std::filesystem::path sourceGameDirectory =
                    gameDirectoryFromCandidate(normalizePathForFluxPack(manifest.gamePath, projectDirectoryHint));
                if (!projectDirectoryHint.empty())
                {
                    if (const std::optional<std::filesystem::path> relative =
                            relativePathInsideRoot(sourceGameDirectory, projectDirectoryHint))
                    {
                        return ResolvedFluxPackGameDirectory{
                            localGameDirectoryFromRelative(projectDirectory, relative.value()),
                            false
                        };
                    }
                }

                if (pathExists(sourceGameDirectory))
                {
                    return ResolvedFluxPackGameDirectory{
                        sourceGameDirectory,
                        true
                    };
                }

                const std::filesystem::path leaf = sourceGameDirectory.filename();
                if (!leaf.empty() && leaf != L"." && leaf != L"..")
                {
                    return ResolvedFluxPackGameDirectory{
                        localGameDirectoryFromRelative(projectDirectory, leaf),
                        false
                    };
                }
            }

            if (projectDirectoryHint.empty())
            {
                return ResolvedFluxPackGameDirectory{
                    fallbackLocalGameDirectory,
                    false
                };
            }

            std::error_code statusError;
            if (!std::filesystem::exists(projectDirectoryHint, statusError) ||
                !std::filesystem::is_directory(projectDirectoryHint, statusError))
            {
                return ResolvedFluxPackGameDirectory{
                    fallbackLocalGameDirectory,
                    false
                };
            }

            try
            {
                std::filesystem::path gameDirectory =
                    pathSettings.loadForProjectDirectory(projectDirectoryHint).gameDirectory;
                if (!gameDirectory.empty())
                {
                    gameDirectory = gameDirectoryFromCandidate(gameDirectory);
                    logger.writeOperation(
                        LogLevel::Warning,
                        "FluxPack",
                        "FluxPack legacy manifest did not include gamePath; recovered it from projectDirectoryHint.");
                    if (const std::optional<std::filesystem::path> relative =
                            relativePathInsideRoot(gameDirectory, projectDirectoryHint))
                    {
                        return ResolvedFluxPackGameDirectory{
                            localGameDirectoryFromRelative(projectDirectory, relative.value()),
                            false
                        };
                    }
                    if (pathExists(gameDirectory))
                    {
                        return ResolvedFluxPackGameDirectory{
                            gameDirectory,
                            true
                        };
                    }
                }
            }
            catch (const std::exception& exception)
            {
                logger.writeOperation(
                    LogLevel::Warning,
                    "FluxPack",
                    std::string("FluxPack legacy gamePath recovery failed: ") + exception.what());
            }

            return ResolvedFluxPackGameDirectory{
                fallbackLocalGameDirectory,
                false
            };
        }

        ProviderInstallState& providerStateFor(
            std::vector<ProviderInstallState>& providers,
            std::wstring providerId)
        {
            providerId = toLower(std::move(providerId));
            auto match = std::find_if(
                providers.begin(),
                providers.end(),
                [&providerId](const ProviderInstallState& provider)
                {
                    return provider.id == providerId;
                });
            if (match != providers.end())
            {
                return *match;
            }

            providers.push_back(ProviderInstallState{
                providerId,
                providerDisplayName(providerId)
            });
            return providers.back();
        }

        std::vector<ProviderInstallState> buildProviderStates(
            const std::vector<FluxPackSourceReference>& sourceArchives)
        {
            std::vector<ProviderInstallState> providers;
            for (const FluxPackSourceReference& source : sourceArchives)
            {
                ProviderInstallState& provider = providerStateFor(providers, providerIdForSource(source));
                ++provider.total;
            }

            return providers;
        }

        std::vector<FluxPackProviderProgress> providerProgressFromState(
            const std::vector<ProviderInstallState>& providers)
        {
            std::vector<FluxPackProviderProgress> progress;
            progress.reserve(providers.size());
            for (const ProviderInstallState& provider : providers)
            {
                const std::uintmax_t processed = provider.completed + provider.pending + provider.failed;
                const int percent = provider.total == 0
                    ? 0
                    : static_cast<int>((processed * 100) / provider.total);
                progress.push_back(FluxPackProviderProgress{
                    provider.id,
                    provider.displayName,
                    provider.total,
                    provider.completed,
                    provider.pending,
                    provider.failed,
                    provider.currentItem,
                    provider.statusText,
                    std::clamp(percent, 0, 100)
                });
            }

            return progress;
        }

        std::uintmax_t processedProviderSources(const std::vector<ProviderInstallState>& providers)
        {
            std::uintmax_t processed = 0;
            for (const ProviderInstallState& provider : providers)
            {
                processed += provider.completed + provider.pending + provider.failed;
            }
            return processed;
        }

        void publishInstallProgress(
            const std::function<void(const FluxPackInstallProgress&)>& callback,
            const std::vector<ProviderInstallState>& providers,
            std::wstring phase,
            std::wstring currentStep,
            std::wstring currentItem,
            std::wstring statusMessage,
            int overallPercent)
        {
            if (!callback)
            {
                return;
            }

            FluxPackInstallProgress progress;
            progress.phase = std::move(phase);
            progress.currentStep = std::move(currentStep);
            progress.currentItem = std::move(currentItem);
            progress.statusMessage = std::move(statusMessage);
            progress.overallPercent = std::clamp(overallPercent, 0, 100);
            progress.providers = providerProgressFromState(providers);
            for (const ProviderInstallState& provider : providers)
            {
                progress.totalSourceCount += provider.total;
                progress.installedSourceCount += provider.completed;
                progress.pendingSourceCount += provider.pending;
                progress.failedSourceCount += provider.failed;
            }

            callback(progress);
        }

        int sourceInstallOverallPercent(
            const std::vector<ProviderInstallState>& providers)
        {
            std::uintmax_t total = 0;
            for (const ProviderInstallState& provider : providers)
            {
                total += provider.total;
            }
            if (total == 0)
            {
                return 68;
            }

            const std::uintmax_t processed = processedProviderSources(providers);
            return 24 + static_cast<int>((processed * 52) / total);
        }

        std::wstring uniqueProjectName(
            const ProjectService& projects,
            const std::filesystem::path& installRoot,
            const std::wstring& requestedName)
        {
            std::wstring baseName = requestedName.empty() ? L"FluxPack Build" : requestedName;
            std::wstring candidate = baseName;
            for (int index = 2; std::filesystem::exists(projects.buildProjectDirectory(installRoot, candidate)); ++index)
            {
                candidate = baseName + L" " + std::to_wstring(index);
            }

            return candidate;
        }

        std::optional<std::filesystem::path> safeArchiveFileName(std::wstring_view fileName)
        {
            if (fileName.empty())
            {
                return std::nullopt;
            }

            std::filesystem::path path{std::wstring(fileName)};
            if (path.empty() ||
                path.has_root_name() ||
                path.has_root_directory() ||
                path.has_parent_path() ||
                path.filename() != path ||
                isTransientDownloadFile(path))
            {
                return std::nullopt;
            }

            const PathSafetyResult validation = PathSafetyService().validateRelativePath(path);
            return validation.safe() ? std::optional<std::filesystem::path>(path) : std::nullopt;
        }

        void addUniquePath(std::vector<std::filesystem::path>& paths, const std::filesystem::path& path)
        {
            if (path.empty())
            {
                return;
            }

            const std::filesystem::path normalized = std::filesystem::absolute(path).lexically_normal();
            const auto duplicate = std::find_if(
                paths.begin(),
                paths.end(),
                [&normalized](const std::filesystem::path& existing)
                {
                    return equalsIgnoreCase(existing.wstring(), normalized.wstring());
                });
            if (duplicate == paths.end())
            {
                paths.push_back(normalized);
            }
        }

        bool sourceArchiveMatchesManifest(
            const std::filesystem::path& candidate,
            const FluxPackSourceReference& source,
            Logger& logger)
        {
            std::error_code sizeError;
            const std::uintmax_t size = std::filesystem::file_size(candidate, sizeError);
            if (sizeError)
            {
                logger.writeOperation(
                    LogLevel::Warning,
                    "FluxPack",
                    "FluxPack local source archive size could not be read. path=\"" +
                        pathForLog(candidate) + "\", reason=\"" + sizeError.message() + "\"");
                return false;
            }

            if (source.archiveSize > 0 && size != source.archiveSize)
            {
                logger.writeOperation(
                    LogLevel::Warning,
                    "FluxPack",
                    "FluxPack local source archive size mismatch. path=\"" +
                        pathForLog(candidate) + "\", expected=" + std::to_string(source.archiveSize) +
                        ", actual=" + std::to_string(size));
                return false;
            }

            if (!source.archiveSha256.empty())
            {
                const std::wstring actualHash = sha256File(candidate);
                if (!equalsIgnoreCase(source.archiveSha256, actualHash))
                {
                    logger.writeOperation(
                        LogLevel::Warning,
                        "FluxPack",
                        "FluxPack local source archive hash mismatch. path=\"" +
                            pathForLog(candidate) + "\"");
                    return false;
                }
            }

            return true;
        }

        std::optional<std::filesystem::path> localSourceArchivePath(
            const FluxPackManifest& manifest,
            const FluxPackSourceReference& source,
            const BuildPathSettingsService& pathSettings,
            Logger& logger)
        {
            const std::optional<std::filesystem::path> archiveFileName =
                safeArchiveFileName(source.archiveFileName);
            if (!archiveFileName.has_value())
            {
                return std::nullopt;
            }

            const std::filesystem::path projectDirectoryHint =
                normalizePathForFluxPack(manifest.projectDirectoryHint);
            if (projectDirectoryHint.empty())
            {
                return std::nullopt;
            }

            std::error_code statusError;
            if (!std::filesystem::exists(projectDirectoryHint, statusError) ||
                !std::filesystem::is_directory(projectDirectoryHint, statusError))
            {
                return std::nullopt;
            }

            std::vector<std::filesystem::path> downloadRoots;
            try
            {
                addUniquePath(
                    downloadRoots,
                    pathSettings.loadForProjectDirectory(projectDirectoryHint).downloadsDirectory);
            }
            catch (const std::exception& exception)
            {
                logger.writeOperation(
                    LogLevel::Warning,
                    "FluxPack",
                    std::string("FluxPack could not read source build path settings for local archives: ") +
                        exception.what());
            }
            addUniquePath(downloadRoots, projectDirectoryHint / L"downloads");

            for (const std::filesystem::path& root : downloadRoots)
            {
                const std::filesystem::path candidate =
                    std::filesystem::absolute(root / archiveFileName.value()).lexically_normal();
                if (!isSameOrInsidePath(candidate, root) ||
                    !std::filesystem::exists(candidate, statusError) ||
                    !std::filesystem::is_regular_file(candidate, statusError))
                {
                    continue;
                }

                if (sourceArchiveMatchesManifest(candidate, source, logger))
                {
                    return candidate;
                }
            }

            return std::nullopt;
        }

        void writeFluxPackDownloadMetadata(
            const std::filesystem::path& archivePath,
            const FluxPackSourceReference& source)
        {
            JsonWriter writer;
            writer.beginObject();
            writer.field(L"source", sourceUrlForPack(source.source));
            writer.field(L"status", L"");
            writer.field(L"gameDomain", source.source.gameDomain);
            writer.field(L"modId", source.source.remoteModId);
            writer.field(L"fileId", source.source.remoteFileId);
            writer.field(L"nexusModName", source.displayName.empty() ? source.folderName : source.displayName);
            writer.field(L"version", source.version);
            writer.field(L"latestVersion", source.source.latestVersion);
            writer.field(L"installedModName", L"");
            writer.field(L"installedAtUtc", L"");
            writer.field(L"destinationFileName", archivePath.filename().wstring());
            writer.field(L"partialPath", L"");
            writer.field(L"bytesReceived", static_cast<std::uintmax_t>(0));
            writer.field(L"totalBytes", static_cast<std::uintmax_t>(0));
            writer.field(L"downloadStartedUnix", static_cast<std::uintmax_t>(0));
            writer.field(L"isDownloading", false);
            writer.endObject();

            AtomicFileStore().writeTextFile(
                metadataPath(archivePath),
                toUtf8(writer.str()),
                AtomicFileWriteOptions{
                    L"FluxPack imported source metadata",
                    ProjectStateValidation::JsonObject
                });
        }

        std::uintmax_t applyEmbeddedConfigs(
            const std::filesystem::path& projectDirectory,
            const std::vector<FluxPackConfigReference>& configs,
            Logger& logger)
        {
            std::uintmax_t applied = 0;
            const PathSafetyService safety;
            for (const FluxPackConfigReference& config : configs)
            {
                if (!config.embedsText)
                {
                    continue;
                }

                const std::optional<std::filesystem::path> relative = safeRelativePath(config.relativePath);
                if (!relative.has_value())
                {
                    logger.writeOperation(
                        LogLevel::Warning,
                        "FluxPack",
                        "Skipped unsafe embedded config path: " + toUtf8(config.relativePath));
                    continue;
                }

                const std::filesystem::path target = projectDirectory / relative.value();
                safety.validateWritePath(projectDirectory, target)
                    .throwIfUnsafe("Embedded FluxPack config path is unsafe");
                std::filesystem::create_directories(target.parent_path());
                AtomicFileStore().writeTextFile(
                    target,
                    toUtf8(config.text),
                    AtomicFileWriteOptions{
                        L"FluxPack embedded config",
                        ProjectStateValidation::Utf8Text
                    });
                ++applied;
            }

            return applied;
        }

        std::uintmax_t applyProfileOrder(
            const std::filesystem::path& projectDirectory,
            std::wstring_view profileName,
            const std::vector<FluxPackProfileOrderReference>& order)
        {
            if (order.empty())
            {
                return 0;
            }

            std::vector<ProfileOrderImportItemRecord> items;
            items.reserve(order.size());
            for (const FluxPackProfileOrderReference& item : order)
            {
                items.push_back(ProfileOrderImportItemRecord{
                    item.kind,
                    item.folderName,
                    item.separatorTitle
                });
            }

            InstanceMetadataStore::replaceProfileOrderItems(projectDirectory, profileName, items);
            return items.size();
        }

        std::vector<InstalledModRecord> listInstalledModsForPack(
            const std::filesystem::path& projectDirectory,
            const std::filesystem::path& modsDirectory,
            Logger& logger)
        {
            try
            {
                return InstanceMetadataStore::listInstalledMods(projectDirectory, modsDirectory);
            }
            catch (const std::exception& exception)
            {
                logger.writeOperation(
                    LogLevel::Warning,
                    "FluxPack",
                    std::string("Installed mod metadata unavailable during FluxPack export: ") + exception.what());
                return {};
            }
        }

        std::vector<ProfileOrderItemRecord> listProfileOrderForPack(
            const std::filesystem::path& projectDirectory,
            const std::filesystem::path& modsDirectory,
            std::wstring_view profileName,
            Logger& logger)
        {
            try
            {
                return InstanceMetadataStore::listProfileOrderItems(projectDirectory, profileName, modsDirectory);
            }
            catch (const std::exception& exception)
            {
                logger.writeOperation(
                    LogLevel::Warning,
                    "FluxPack",
                    std::string("Profile order metadata unavailable during FluxPack export: ") + exception.what());
                return {};
            }
        }
    }

    FluxPackService::FluxPackService(
        Logger& logger,
        ProjectService& projects,
        DownloadService& downloads,
        const BuildPathSettingsService& pathSettings) noexcept
        : logger_(logger),
          projects_(projects),
          downloads_(downloads),
          pathSettings_(pathSettings)
    {
    }

    void FluxPackService::initialize()
    {
        if (initialized_)
        {
            return;
        }

        initialized_ = true;
        logger_.write(LogLevel::Info, "FluxPack service initialized.");
    }

    void FluxPackService::shutdown()
    {
        if (!initialized_)
        {
            return;
        }

        logger_.write(LogLevel::Info, "FluxPack service shut down.");
        initialized_ = false;
    }

    FluxPackSummary FluxPackService::exportProject(const FluxPackExportRequest& request) const
    {
        if (request.configPath.empty())
        {
            throw std::invalid_argument("Build config path is required.");
        }

        if (request.outputPath.empty())
        {
            throw std::invalid_argument("FluxPack output path is required.");
        }

        logger_.writeOperation(
            LogLevel::Info,
            "FluxPack",
            "FluxPack export requested. configPath=\"" + pathForLog(request.configPath) +
                "\", outputPath=\"" + pathForLog(request.outputPath) +
                "\", includeGeneratedAssets=" + (request.includeGeneratedAssets ? "true" : "false"));

        const ProjectOpenResult project = projects_.readProjectConfigSummary(request.configPath);
        BuildPathSettings paths{
            project.project.gamePath,
            project.project.projectDirectory / L"mods",
            project.project.projectDirectory / L"profiles",
            project.project.projectDirectory / L"downloads",
            project.project.projectDirectory / L"overwrite"
        };
        try
        {
            paths = pathSettings_.loadForConfig(project.project.configPath);
        }
        catch (const std::exception& exception)
        {
            logger_.writeOperation(
                LogLevel::Warning,
                "FluxPack",
                std::string("Build path settings unavailable during FluxPack export: ") + exception.what());
        }

        std::vector<DownloadSourceFile> downloads = buildDownloadIndex(paths.downloadsDirectory);
        std::vector<PackModReference> sourceArchives;
        std::vector<PackModReference> generatedAssets;
        std::vector<PackModReference> customPatches;

        for (const InstalledModRecord& mod : listInstalledModsForPack(
                 project.project.projectDirectory,
                 paths.modsDirectory,
                 logger_))
        {
            PackModReference reference{
                mod,
                sourceHasRemoteIdentity(mod.source)
                    ? matchSourceArchive(mod, downloads)
                    : std::optional<DownloadSourceFile>{}
            };

            if (isGeneratedAssetMod(mod))
            {
                generatedAssets.push_back(std::move(reference));
            }
            else if (sourceHasRemoteIdentity(mod.source))
            {
                sourceArchives.push_back(std::move(reference));
            }
            else
            {
                customPatches.push_back(std::move(reference));
            }
        }

        std::vector<FileManifestEntry> customConfigs;
        std::vector<FileManifestEntry> profileConfigs =
            scanFiles(paths.profilesDirectory, project.project.projectDirectory, true, true);
        customConfigs.insert(customConfigs.end(), profileConfigs.begin(), profileConfigs.end());
        std::vector<FileManifestEntry> overwriteConfigs =
            scanFiles(paths.overwriteDirectory, project.project.projectDirectory, true, true);
        customConfigs.insert(customConfigs.end(), overwriteConfigs.begin(), overwriteConfigs.end());

        const std::filesystem::path rootModOrganizerIni =
            project.project.projectDirectory / L"ModOrganizer.ini";
        if (std::filesystem::is_regular_file(rootModOrganizerIni))
        {
            customConfigs.push_back(buildFileManifestEntry(
                rootModOrganizerIni,
                project.project.projectDirectory,
                true));
        }

        std::sort(customConfigs.begin(), customConfigs.end(), [](const FileManifestEntry& left, const FileManifestEntry& right)
        {
            return left.relativePath < right.relativePath;
        });

        const std::wstring defaultProfile = project.resolvedTemplate.defaultProfileName.empty()
            ? L"Default"
            : project.resolvedTemplate.defaultProfileName;
        const std::vector<ProfileOrderItemRecord> profileOrder =
            listProfileOrderForPack(
                project.project.projectDirectory,
                paths.modsDirectory,
                defaultProfile,
                logger_);

        const std::wstring manifest = serializeFluxPack(
            project,
            paths,
            sourceArchives,
            generatedAssets,
            customPatches,
            customConfigs,
            profileOrder,
            request.includeGeneratedAssets);

        const std::filesystem::path absoluteOutput =
            std::filesystem::absolute(request.outputPath).lexically_normal();
        if (!absoluteOutput.parent_path().empty())
        {
            std::filesystem::create_directories(absoluteOutput.parent_path());
        }

        AtomicFileStore().writeTextFile(
            absoluteOutput,
            toUtf8(manifest),
            AtomicFileWriteOptions{
                L"FluxPack manifest",
                ProjectStateValidation::JsonObject
            });

        std::error_code sizeError;
        const std::uintmax_t manifestBytes = std::filesystem::file_size(absoluteOutput, sizeError);
        FluxPackSummary summary;
        summary.outputPath = absoluteOutput;
        summary.buildName = project.project.name;
        summary.formatVersion = packageFormatVersion;
        summary.manifestBytes = sizeError ? 0 : manifestBytes;
        summary.sourceArchiveCount = sourceArchives.size();
        summary.generatedAssetCount = generatedAssets.size();
        summary.customPatchCount = customPatches.size();
        summary.customConfigCount = customConfigs.size();
        summary.installStepCount = 4;
        summary.generatedAssetsIncluded = request.includeGeneratedAssets;
        summary.installPlanAvailable = true;

        logger_.writeOperation(
            LogLevel::Info,
            "FluxPack",
            "FluxPack export completed. sourceArchives=" + std::to_string(sourceArchives.size()) +
                ", generatedAssets=" + std::to_string(generatedAssets.size()) +
                ", customPatches=" + std::to_string(customPatches.size()) +
                ", customConfigs=" + std::to_string(customConfigs.size()));

        return summary;
    }

    FluxPackSummary FluxPackService::inspectFluxPack(const std::filesystem::path& fluxPackPath) const
    {
        if (fluxPackPath.empty())
        {
            throw std::invalid_argument("FluxPack path is required.");
        }

        const std::filesystem::path absolutePath =
            std::filesystem::absolute(fluxPackPath).lexically_normal();
        std::error_code sizeError;
        const std::uintmax_t manifestBytes = std::filesystem::file_size(absolutePath, sizeError);
        const JsonValue root = JsonReader::parse(fromUtf8(readTextFile(absolutePath)));
        FluxPackSummary summary = summaryFromJson(root, absolutePath, sizeError ? 0 : manifestBytes);

        logger_.writeOperation(
            LogLevel::Info,
            "FluxPack",
            "FluxPack inspected. path=\"" + pathForLog(absolutePath) +
                "\", sourceArchives=" + std::to_string(summary.sourceArchiveCount) +
                ", installSteps=" + std::to_string(summary.installStepCount));
        return summary;
    }

    FluxPackInstallResult FluxPackService::installFluxPack(const FluxPackInstallRequest& request) const
    {
        if (request.fluxPackPath.empty())
        {
            throw std::invalid_argument("FluxPack path is required.");
        }
        if (request.installRootDirectory.empty())
        {
            throw std::invalid_argument("Install root directory is required.");
        }

        const std::filesystem::path absolutePath =
            std::filesystem::absolute(request.fluxPackPath).lexically_normal();
        const std::filesystem::path installRoot =
            std::filesystem::absolute(request.installRootDirectory).lexically_normal();
        if (!std::filesystem::exists(installRoot) || !std::filesystem::is_directory(installRoot))
        {
            throw std::invalid_argument("Install root directory does not exist.");
        }

        std::error_code sizeError;
        const std::uintmax_t manifestBytes = std::filesystem::file_size(absolutePath, sizeError);
        const JsonValue root = JsonReader::parse(fromUtf8(readTextFile(absolutePath)));
        FluxPackManifest manifest = parseFluxPackManifest(absolutePath, root, sizeError ? 0 : manifestBytes);
        if (manifest.templateId.empty())
        {
            throw std::invalid_argument("FluxPack build template is missing.");
        }
        if (!manifest.summary.installPlanAvailable)
        {
            throw std::invalid_argument("FluxPack install plan is missing.");
        }

        const std::wstring projectName = uniqueProjectName(projects_, installRoot, manifest.buildName);
        const std::filesystem::path projectDirectory =
            projects_.buildProjectDirectory(installRoot, projectName);
        const ResolvedFluxPackGameDirectory installGameDirectory =
            resolveInstallGameDirectory(manifest, pathSettings_, logger_, projectDirectory);
        if (installGameDirectory.path.empty())
        {
            throw std::invalid_argument("FluxPack game directory could not be resolved.");
        }

        if (!installGameDirectory.validateExistingGame)
        {
            PathSafetyService().validateWritePath(projectDirectory, installGameDirectory.path)
                .throwIfUnsafe("FluxPack game directory is unsafe");
            std::filesystem::create_directories(installGameDirectory.path);
            logger_.writeOperation(
                LogLevel::Info,
                "FluxPack",
                "FluxPack install using local game directory. gameDirectory=\"" +
                    pathForLog(installGameDirectory.path) + "\"");
        }

        std::vector<ProviderInstallState> providers = buildProviderStates(manifest.sourceArchives);
        publishInstallProgress(
            request.progress,
            providers,
            L"inspect",
            L"FluxPack прочитан",
            absolutePath.filename().wstring(),
            L"Проверяем рецепт и install plan",
            8);

        logger_.writeOperation(
            LogLevel::Info,
            "FluxPack",
            "FluxPack install requested. path=\"" + pathForLog(absolutePath) +
                "\", installRoot=\"" + pathForLog(installRoot) +
                "\", buildName=\"" + toUtf8(projectName) +
                "\", gameDirectory=\"" + pathForLog(installGameDirectory.path) +
                "\", sourceArchives=" + std::to_string(manifest.sourceArchives.size()));

        publishInstallProgress(
            request.progress,
            providers,
            L"project",
            L"Создаём сборку",
            projectName,
            L"Готовим структуру проекта Fluxora",
            16);

        const ProjectDescriptor project = projects_.createProject(ProjectCreateRequest{
            projectName,
            manifest.templateId,
            installGameDirectory.path,
            installRoot,
            installGameDirectory.validateExistingGame
        });

        const BuildPathSettings savedInstallPaths = pathSettings_.saveForConfig(
            project.configPath,
            BuildPathSettings{
                project.gamePath,
                project.projectDirectory / L"mods",
                project.projectDirectory / L"profiles",
                project.projectDirectory / L"downloads",
                project.projectDirectory / L"overwrite"
            });
        static_cast<void>(savedInstallPaths);

        FluxPackInstallResult result;
        result.summary = manifest.summary;
        result.configPath = project.configPath;
        result.projectDirectory = project.projectDirectory;
        result.buildName = project.name;
        result.totalSourceCount = manifest.sourceArchives.size();

        publishInstallProgress(
            request.progress,
            providers,
            L"sources",
            L"Скачиваем источники",
            {},
            L"Подключаем источники из FluxPack",
            manifest.sourceArchives.empty() ? 68 : 24);

        for (const FluxPackSourceReference& source : manifest.sourceArchives)
        {
            ProviderInstallState& provider = providerStateFor(providers, providerIdForSource(source));
            const std::wstring installName = sourceInstallName(source);
            provider.currentItem = installName;
            provider.statusText = L"Скачиваем";
            publishInstallProgress(
                request.progress,
                providers,
                L"sources",
                L"Скачиваем источники",
                installName,
                L"Источник: " + provider.displayName,
                sourceInstallOverallPercent(providers));

            try
            {
                std::optional<DownloadEntry> localEntry;
                if (const std::optional<std::filesystem::path> localArchive =
                        localSourceArchivePath(manifest, source, pathSettings_, logger_);
                    localArchive.has_value())
                {
                    provider.statusText = L"Копируем локальный архив";
                    publishInstallProgress(
                        request.progress,
                        providers,
                        L"sources",
                        L"Копируем источник",
                        installName,
                        L"Используем архив из перенесённой сборки",
                        sourceInstallOverallPercent(providers));

                    try
                    {
                        DownloadEntry imported = downloads_.importLocalFile(project.projectDirectory, localArchive.value());
                        writeFluxPackDownloadMetadata(imported.localPath, source);
                        logger_.writeOperation(
                            LogLevel::Info,
                            "FluxPack",
                            "FluxPack source archive restored from source build downloads. provider=\"" +
                                toUtf8(provider.id) +
                                "\", mod=\"" + toUtf8(installName) +
                                "\", archive=\"" + pathForLog(localArchive.value()) + "\"");
                        localEntry = std::move(imported);
                    }
                    catch (const std::exception& exception)
                    {
                        logger_.writeOperation(
                            LogLevel::Warning,
                            "FluxPack",
                            "FluxPack local source archive could not be imported; falling back to remote download. provider=\"" +
                                toUtf8(provider.id) +
                                "\", mod=\"" + toUtf8(installName) +
                                "\", archive=\"" + pathForLog(localArchive.value()) +
                                "\", reason=\"" + exception.what() + "\"");
                    }
                }

                DownloadEntry entry;
                if (localEntry.has_value())
                {
                    entry = std::move(localEntry.value());
                }
                else
                {
                    const std::wstring nxmLink = nxmLinkForSource(source);
                    if (provider.id != L"nexus" || nxmLink.empty())
                    {
                        ++provider.failed;
                        ++result.failedSourceCount;
                        provider.statusText = L"Автозагрузка недоступна";
                        logger_.writeOperation(
                            LogLevel::Warning,
                            "FluxPack",
                            "FluxPack source cannot be downloaded automatically. provider=\"" + toUtf8(provider.id) +
                                "\", mod=\"" + toUtf8(installName) + "\"");
                        publishInstallProgress(
                            request.progress,
                            providers,
                            L"sources",
                            L"Источник не установлен",
                            installName,
                            L"Для этого источника нет автоматической загрузки",
                            sourceInstallOverallPercent(providers));
                        continue;
                    }

                    std::vector<DownloadEntry> downloaded = downloads_.captureNxmLinks(project.projectDirectory, {nxmLink});
                    if (downloaded.empty() || !downloaded.front().canInstall)
                    {
                        ++provider.failed;
                        ++result.failedSourceCount;
                        provider.statusText = fluxPackDownloadFailureStatus(downloaded);
                        logger_.writeOperation(
                            LogLevel::Warning,
                            "FluxPack",
                            "FluxPack source download did not produce an installable archive. provider=\"" +
                                toUtf8(provider.id) +
                                "\", mod=\"" + toUtf8(installName) +
                                "\", status=\"" + toUtf8(provider.statusText) + "\"");
                        if (!downloaded.empty() && !downloaded.front().localPath.empty())
                        {
                            try
                            {
                                downloads_.deleteDownload(project.projectDirectory, downloaded.front().localPath);
                            }
                            catch (const std::exception& cleanupException)
                            {
                                logger_.writeOperation(
                                    LogLevel::Warning,
                                    "FluxPack",
                                    "FluxPack failed source placeholder cleanup failed. path=\"" +
                                        pathForLog(downloaded.front().localPath) +
                                        "\", reason=\"" + cleanupException.what() + "\"");
                            }
                        }
                        publishInstallProgress(
                            request.progress,
                            providers,
                            L"sources",
                            L"Ошибка загрузки",
                            installName,
                            provider.statusText,
                            sourceInstallOverallPercent(providers));
                        continue;
                    }

                    entry = downloaded.front();
                }
                provider.statusText = L"Устанавливаем";
                publishInstallProgress(
                    request.progress,
                    providers,
                    L"sources",
                    L"Устанавливаем мод",
                    installName,
                    entry.fileName,
                    sourceInstallOverallPercent(providers));

                InstalledMod installed;
                const FomodInstallerDescriptor fomod = downloads_.analyzeFomodDownload(
                    project.projectDirectory,
                    entry.localPath);
                if (fomod.isFomod)
                {
                    installed = downloads_.installFomodDownload(
                        project.projectDirectory,
                        entry.localPath,
                        installName,
                        ExistingModInstallMode::FailIfExists,
                        {});
                }
                else
                {
                    installed = downloads_.installDownload(
                        project.projectDirectory,
                        entry.localPath,
                        installName,
                        ExistingModInstallMode::FailIfExists);
                }

                if (!source.enabled)
                {
                    InstanceMetadataStore::setInstalledModEnabled(project.projectDirectory, installed.id, false);
                }

                ++provider.completed;
                ++result.installedSourceCount;
                provider.statusText = L"Установлено";
                publishInstallProgress(
                    request.progress,
                    providers,
                    L"sources",
                    L"Мод установлен",
                    installName,
                    provider.displayName,
                    sourceInstallOverallPercent(providers));
            }
            catch (const std::exception& exception)
            {
                ++provider.failed;
                ++result.failedSourceCount;
                provider.statusText = L"Ошибка";
                logger_.writeOperation(
                    LogLevel::Error,
                    "FluxPack",
                    "FluxPack source install failed. provider=\"" + toUtf8(provider.id) +
                        "\", mod=\"" + toUtf8(installName) +
                        "\", reason=\"" + exception.what() + "\"");
                publishInstallProgress(
                    request.progress,
                    providers,
                    L"sources",
                    L"Источник не установлен",
                    installName,
                    std::wstring(exception.what(), exception.what() + std::strlen(exception.what())),
                    sourceInstallOverallPercent(providers));
            }
        }

        publishInstallProgress(
            request.progress,
            providers,
            L"configs",
            L"Применяем настройки",
            manifest.defaultProfile,
            L"Пишем embedded config и порядок профиля",
            84);

        result.appliedConfigCount = applyEmbeddedConfigs(project.projectDirectory, manifest.customConfigs, logger_);
        result.appliedProfileOrderItemCount = applyProfileOrder(
            project.projectDirectory,
            manifest.defaultProfile,
            manifest.profileOrder);
        result.hasWarnings = result.pendingSourceCount > 0 || result.failedSourceCount > 0;

        publishInstallProgress(
            request.progress,
            providers,
            L"complete",
            result.hasWarnings ? L"Установка завершена с предупреждениями" : L"Сборка установлена",
            project.name,
            result.hasWarnings
                ? L"Часть источников не была установлена"
                : L"FluxPack install plan выполнен",
            100);

        logger_.writeOperation(
            LogLevel::Info,
            "FluxPack",
            "FluxPack install completed. configPath=\"" + pathForLog(project.configPath) +
                "\", installedSources=" + std::to_string(result.installedSourceCount) +
                ", pendingSources=" + std::to_string(result.pendingSourceCount) +
                ", failedSources=" + std::to_string(result.failedSourceCount) +
                ", appliedConfigs=" + std::to_string(result.appliedConfigCount) +
                ", profileOrderItems=" + std::to_string(result.appliedProfileOrderItemCount));
        return result;
    }

    bool FluxPackService::isInitialized() const noexcept
    {
        return initialized_;
    }
}
