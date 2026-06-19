#include "FluxoraCore/Storage/ProjectStateTransaction.hpp"

#include "FluxoraCore/Services/Logger.hpp"
#include "FluxoraCore/Support/JsonReader.hpp"
#include "FluxoraCore/Support/JsonWriter.hpp"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fluxora
{
    namespace
    {
        constexpr std::wstring_view markerPrefix = L".fluxora-state-transaction-";
        constexpr std::wstring_view markerExtension = L".json";

        std::atomic_uint64_t markerCounter{0};

#ifdef _WIN32
        std::string toUtf8(std::wstring_view value)
        {
            if (value.empty())
            {
                return {};
            }

            const int requiredLength = WideCharToMultiByte(
                CP_UTF8,
                0,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0,
                nullptr,
                nullptr);
            if (requiredLength <= 0)
            {
                return {};
            }

            std::string out(static_cast<std::size_t>(requiredLength), '\0');
            WideCharToMultiByte(
                CP_UTF8,
                0,
                value.data(),
                static_cast<int>(value.size()),
                out.data(),
                requiredLength,
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

            const int requiredLength = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0);
            if (requiredLength <= 0)
            {
                throw std::invalid_argument("Transaction marker is not valid UTF-8.");
            }

            std::wstring out(static_cast<std::size_t>(requiredLength), L'\0');
            MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                out.data(),
                requiredLength);
            return out;
        }
#else
        std::string toUtf8(std::wstring_view value)
        {
            return std::string(value.begin(), value.end());
        }

        std::wstring fromUtf8(const std::string& value)
        {
            return std::wstring(value.begin(), value.end());
        }
#endif

        std::string readTextFile(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::in | std::ios::binary);
            if (!file)
            {
                throw std::runtime_error("Failed to read transaction marker.");
            }

            return std::string(
                std::istreambuf_iterator<char>(file),
                std::istreambuf_iterator<char>());
        }

        std::filesystem::path makeMarkerPath(const std::filesystem::path& directory)
        {
            const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
            const std::uint64_t counter = ++markerCounter;
            return directory / std::filesystem::path(
                std::wstring(markerPrefix) +
                std::to_wstring(now) +
                L"." +
                std::to_wstring(counter) +
                std::wstring(markerExtension));
        }

        std::wstring validationName(ProjectStateValidation validation)
        {
            switch (validation)
            {
            case ProjectStateValidation::None:
                return L"none";
            case ProjectStateValidation::Utf8Text:
                return L"utf8-text";
            case ProjectStateValidation::JsonObject:
                return L"json-object";
            }

            return L"utf8-text";
        }

        ProjectStateValidation parseValidation(const std::wstring& value)
        {
            if (value == L"none")
            {
                return ProjectStateValidation::None;
            }
            if (value == L"json-object")
            {
                return ProjectStateValidation::JsonObject;
            }
            return ProjectStateValidation::Utf8Text;
        }

        std::wstring requiredString(const JsonValue& object, std::wstring_view key)
        {
            const JsonValue* value = object.find(key);
            if (value == nullptr || !value->isString())
            {
                throw std::invalid_argument("Transaction marker is missing a string field.");
            }

            return value->asString();
        }

        std::vector<ProjectStateTransactionFile> readMarkerFiles(
            const std::filesystem::path& markerPath)
        {
            const JsonValue root = JsonReader::parse(fromUtf8(readTextFile(markerPath)));
            if (!root.isObject())
            {
                throw std::invalid_argument("Transaction marker root must be an object.");
            }

            const JsonValue* files = root.find(L"files");
            if (files == nullptr || !files->isArray())
            {
                throw std::invalid_argument("Transaction marker is missing files.");
            }

            std::vector<ProjectStateTransactionFile> result;
            for (const JsonValue& item : files->asArray())
            {
                if (!item.isObject())
                {
                    continue;
                }

                result.push_back(ProjectStateTransactionFile{
                    std::filesystem::path(requiredString(item, L"path")),
                    requiredString(item, L"stateName"),
                    parseValidation(requiredString(item, L"validation"))
                });
            }

            return result;
        }

        std::string pathForLog(const std::filesystem::path& path)
        {
            const std::string converted = toUtf8(path.wstring());
            return converted.empty() ? path.string() : converted;
        }

        void log(Logger* logger, LogLevel level, std::string_view message)
        {
            if (logger != nullptr)
            {
                logger->write(level, "ProjectStateRecovery", message);
            }
        }

        [[nodiscard]] std::string recoveryActionName(AtomicFileRecoveryAction action)
        {
            switch (action)
            {
            case AtomicFileRecoveryAction::None:
                return "none";
            case AtomicFileRecoveryAction::RemovedStaleTemp:
                return "removedStaleTemp";
            case AtomicFileRecoveryAction::RestoredBackup:
                return "restoredBackup";
            case AtomicFileRecoveryAction::PromotedTemp:
                return "promotedTemp";
            }

            return "unknown";
        }
    }

    ProjectStateTransaction::ProjectStateTransaction(
        const AtomicFileStore& store,
        const std::filesystem::path& markerDirectory,
        std::wstring operationName,
        Logger* logger)
        : store_(store),
          markerPath_(makeMarkerPath(markerDirectory)),
          operationName_(std::move(operationName)),
          logger_(logger)
    {
        if (markerDirectory.empty())
        {
            throw std::invalid_argument("Transaction marker directory is required.");
        }
    }

    ProjectStateTransaction::~ProjectStateTransaction()
    {
        if (committed_ || markerPath_.empty())
        {
            return;
        }

        try
        {
            writeMarker();
        }
        catch (...)
        {
        }
    }

    void ProjectStateTransaction::trackFile(
        const std::filesystem::path& path,
        std::wstring stateName,
        ProjectStateValidation validation)
    {
        if (path.empty())
        {
            throw std::invalid_argument("Transaction file path is required.");
        }

        files_.push_back(ProjectStateTransactionFile{
            std::filesystem::absolute(path).lexically_normal(),
            std::move(stateName),
            validation
        });
        writeMarker();
    }

    void ProjectStateTransaction::commit()
    {
        std::error_code error;
        std::filesystem::remove(markerPath_, error);
        committed_ = true;
    }

    const std::filesystem::path& ProjectStateTransaction::markerPath() const noexcept
    {
        return markerPath_;
    }

    std::vector<ProjectStateTransactionRecovery> ProjectStateTransaction::recoverDirectory(
        const std::filesystem::path& markerDirectory,
        const AtomicFileStore& store,
        Logger* logger)
    {
        std::vector<ProjectStateTransactionRecovery> recoveries;
        std::error_code error;
        if (markerDirectory.empty() ||
            !std::filesystem::exists(markerDirectory, error) ||
            !std::filesystem::is_directory(markerDirectory, error))
        {
            return recoveries;
        }

        for (const auto& entry : std::filesystem::directory_iterator(
                 markerDirectory,
                 std::filesystem::directory_options::skip_permission_denied,
                 error))
        {
            if (error)
            {
                break;
            }

            std::error_code statusError;
            if (!entry.is_regular_file(statusError) || !isTransactionMarker(entry.path()))
            {
                continue;
            }

            ProjectStateTransactionRecovery recovery;
            recovery.markerPath = entry.path();
            try
            {
                for (const ProjectStateTransactionFile& file : readMarkerFiles(entry.path()))
                {
                    recovery.fileResults.push_back(store.recoverFile(
                        file.path,
                        AtomicFileWriteOptions{
                            file.stateName,
                            file.validation
                        },
                        logger));
                }

                std::filesystem::remove(entry.path(), statusError);
                log(
                    logger,
                    LogLevel::Warning,
                    "recoveryAction=recoveredTransaction marker=\"" + pathForLog(entry.path()) +
                        "\", restoredFiles=" + std::to_string(recovery.fileResults.size()) + ".");
                for (const AtomicFileRecoveryResult& fileResult : recovery.fileResults)
                {
                    log(
                        logger,
                        fileResult.action == AtomicFileRecoveryAction::None ? LogLevel::Info : LogLevel::Warning,
                        "recoveryAction=" + recoveryActionName(fileResult.action) +
                            " transactionMarker=\"" + pathForLog(entry.path()) +
                            "\", target=\"" + pathForLog(fileResult.targetPath) +
                            "\", backup=\"" + pathForLog(fileResult.backupPath) +
                            "\", removedTempFiles=" +
                            std::to_string(fileResult.removedTempFiles.size()) + ".");
                }
            }
            catch (const std::exception& exception)
            {
                log(
                    logger,
                    LogLevel::Error,
                    "Failed to recover project state transaction marker \"" +
                        pathForLog(entry.path()) + "\": " + exception.what());
            }

            recoveries.push_back(std::move(recovery));
        }

        return recoveries;
    }

    bool ProjectStateTransaction::isTransactionMarker(
        const std::filesystem::path& path)
    {
        const std::wstring name = path.filename().wstring();
        return name.rfind(markerPrefix, 0) == 0 &&
            name.size() >= markerExtension.size() &&
            name.compare(name.size() - markerExtension.size(), markerExtension.size(), markerExtension) == 0;
    }

    void ProjectStateTransaction::writeMarker()
    {
        JsonWriter writer;
        writer.beginObject();
        writer.field(L"schemaVersion", 1);
        writer.field(L"operation", operationName_);
        writer.key(L"files").beginArray();
        for (const ProjectStateTransactionFile& file : files_)
        {
            writer.beginObject();
            writer.field(L"path", file.path.wstring());
            writer.field(L"stateName", file.stateName);
            writer.field(L"validation", validationName(file.validation));
            writer.endObject();
        }
        writer.endArray();
        writer.endObject();

        store_.writeTextFile(
            markerPath_,
            toUtf8(writer.str()),
            AtomicFileWriteOptions{
                L"project state transaction marker",
                ProjectStateValidation::JsonObject,
                {},
                false
            });
    }
}
