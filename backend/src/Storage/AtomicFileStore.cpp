#include "FluxoraCore/Storage/AtomicFileStore.hpp"

#include "FluxoraCore/Services/Logger.hpp"
#include "FluxoraCore/Support/JsonReader.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <string_view>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fluxora
{
    namespace
    {
        constexpr std::wstring_view backupExtension = L".fluxora-bak";
        constexpr std::wstring_view tempMarker = L".fluxora-tmp.";
        constexpr std::wstring_view tempExtension = L".tmp";

        std::atomic_uint64_t tempCounter{0};

        std::wstring hex8(std::uint32_t value)
        {
            constexpr wchar_t digits[] = L"0123456789abcdef";
            std::wstring result(8, L'0');
            for (int index = 7; index >= 0; --index)
            {
                result[static_cast<std::size_t>(index)] = digits[value & 0x0FU];
                value >>= 4U;
            }
            return result;
        }

        std::wstring stablePathToken(const std::filesystem::path& path)
        {
            std::uint32_t hash = 2166136261U;
            for (const wchar_t character : path.lexically_normal().generic_wstring())
            {
                hash ^= static_cast<std::uint32_t>(character);
                hash *= 16777619U;
            }
            return hex8(hash);
        }

        std::wstring tempPrefixFor(const std::filesystem::path& targetPath)
        {
            return L".ft" + stablePathToken(targetPath) + L".";
        }

        std::filesystem::path legacyBackupPathFor(const std::filesystem::path& path)
        {
            return std::filesystem::path(path.wstring() + std::wstring(backupExtension));
        }

        std::string readBinaryFile(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::in | std::ios::binary);
            if (!file)
            {
                throw std::runtime_error("Failed to open state file for validation.");
            }

            return std::string(
                std::istreambuf_iterator<char>(file),
                std::istreambuf_iterator<char>());
        }

        bool isContinuationByte(unsigned char byte) noexcept
        {
            return (byte & 0xC0U) == 0x80U;
        }

        bool isValidUtf8(std::string_view value) noexcept
        {
            std::size_t index = 0;
            while (index < value.size())
            {
                const auto byte = static_cast<unsigned char>(value[index]);
                if (byte <= 0x7FU)
                {
                    ++index;
                    continue;
                }

                if (byte >= 0xC2U && byte <= 0xDFU)
                {
                    if (index + 1 >= value.size() ||
                        !isContinuationByte(static_cast<unsigned char>(value[index + 1])))
                    {
                        return false;
                    }
                    index += 2;
                    continue;
                }

                if (byte == 0xE0U)
                {
                    if (index + 2 >= value.size())
                    {
                        return false;
                    }
                    const auto second = static_cast<unsigned char>(value[index + 1]);
                    const auto third = static_cast<unsigned char>(value[index + 2]);
                    if (second < 0xA0U || second > 0xBFU || !isContinuationByte(third))
                    {
                        return false;
                    }
                    index += 3;
                    continue;
                }

                if ((byte >= 0xE1U && byte <= 0xECU) || (byte >= 0xEEU && byte <= 0xEFU))
                {
                    if (index + 2 >= value.size() ||
                        !isContinuationByte(static_cast<unsigned char>(value[index + 1])) ||
                        !isContinuationByte(static_cast<unsigned char>(value[index + 2])))
                    {
                        return false;
                    }
                    index += 3;
                    continue;
                }

                if (byte == 0xEDU)
                {
                    if (index + 2 >= value.size())
                    {
                        return false;
                    }
                    const auto second = static_cast<unsigned char>(value[index + 1]);
                    const auto third = static_cast<unsigned char>(value[index + 2]);
                    if (second < 0x80U || second > 0x9FU || !isContinuationByte(third))
                    {
                        return false;
                    }
                    index += 3;
                    continue;
                }

                if (byte == 0xF0U)
                {
                    if (index + 3 >= value.size())
                    {
                        return false;
                    }
                    const auto second = static_cast<unsigned char>(value[index + 1]);
                    if (second < 0x90U || second > 0xBFU ||
                        !isContinuationByte(static_cast<unsigned char>(value[index + 2])) ||
                        !isContinuationByte(static_cast<unsigned char>(value[index + 3])))
                    {
                        return false;
                    }
                    index += 4;
                    continue;
                }

                if (byte >= 0xF1U && byte <= 0xF3U)
                {
                    if (index + 3 >= value.size() ||
                        !isContinuationByte(static_cast<unsigned char>(value[index + 1])) ||
                        !isContinuationByte(static_cast<unsigned char>(value[index + 2])) ||
                        !isContinuationByte(static_cast<unsigned char>(value[index + 3])))
                    {
                        return false;
                    }
                    index += 4;
                    continue;
                }

                if (byte == 0xF4U)
                {
                    if (index + 3 >= value.size())
                    {
                        return false;
                    }
                    const auto second = static_cast<unsigned char>(value[index + 1]);
                    if (second < 0x80U || second > 0x8FU ||
                        !isContinuationByte(static_cast<unsigned char>(value[index + 2])) ||
                        !isContinuationByte(static_cast<unsigned char>(value[index + 3])))
                    {
                        return false;
                    }
                    index += 4;
                    continue;
                }

                return false;
            }

            return true;
        }

#ifdef _WIN32
        std::wstring decodeUtf8(const std::string& value)
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
                throw std::invalid_argument("State file is not valid UTF-8.");
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
#else
        std::wstring decodeUtf8(const std::string& value)
        {
            if (!isValidUtf8(value))
            {
                throw std::invalid_argument("State file is not valid UTF-8.");
            }

            std::wstring out;
            for (std::size_t index = 0; index < value.size();)
            {
                const auto first = static_cast<unsigned char>(value[index]);
                if (first <= 0x7FU)
                {
                    out.push_back(static_cast<wchar_t>(first));
                    ++index;
                }
                else if (first <= 0xDFU)
                {
                    const auto codePoint = ((first & 0x1FU) << 6U) |
                        (static_cast<unsigned char>(value[index + 1]) & 0x3FU);
                    out.push_back(static_cast<wchar_t>(codePoint));
                    index += 2;
                }
                else if (first <= 0xEFU)
                {
                    const auto codePoint = ((first & 0x0FU) << 12U) |
                        ((static_cast<unsigned char>(value[index + 1]) & 0x3FU) << 6U) |
                        (static_cast<unsigned char>(value[index + 2]) & 0x3FU);
                    out.push_back(static_cast<wchar_t>(codePoint));
                    index += 3;
                }
                else
                {
                    const auto codePoint = ((first & 0x07U) << 18U) |
                        ((static_cast<unsigned char>(value[index + 1]) & 0x3FU) << 12U) |
                        ((static_cast<unsigned char>(value[index + 2]) & 0x3FU) << 6U) |
                        (static_cast<unsigned char>(value[index + 3]) & 0x3FU);
                    out.push_back(static_cast<wchar_t>(codePoint));
                    index += 4;
                }
            }

            return out;
        }

        std::string toUtf8(std::wstring_view value)
        {
            return std::string(value.begin(), value.end());
        }
#endif

        std::string pathForLog(const std::filesystem::path& path)
        {
            const std::string converted = toUtf8(path.wstring());
            return converted.empty() ? path.string() : converted;
        }

        std::string stateLabel(const AtomicFileWriteOptions& options)
        {
            const std::string label = toUtf8(options.stateName);
            return label.empty() ? std::string("project state") : label;
        }

        std::string actionableDiskFullMessage(
            const std::filesystem::path& path,
            const AtomicFileWriteOptions& options)
        {
            return "Disk is full while writing " + stateLabel(options) + " at \"" +
                pathForLog(path) + "\". Free disk space and retry; the previous state file was left in place.";
        }

        bool looksLikeDiskFull(const std::error_code& error) noexcept
        {
            return error == std::make_error_code(std::errc::no_space_on_device)
#ifdef _WIN32
                || error.value() == ERROR_DISK_FULL
                || error.value() == ERROR_HANDLE_DISK_FULL
#endif
                ;
        }

        void throwFromSystemError(
            const std::filesystem::path& path,
            const AtomicFileWriteOptions& options,
            const std::error_code& error)
        {
            if (looksLikeDiskFull(error))
            {
                throw std::runtime_error(actionableDiskFullMessage(path, options));
            }

            throw std::filesystem::filesystem_error(
                "Atomic project state write failed.",
                path,
                error);
        }

        std::filesystem::path makeTempPath(const std::filesystem::path& targetPath)
        {
            const std::filesystem::path parent = targetPath.parent_path();
            const std::uint64_t counter = ++tempCounter;
            return parent / std::filesystem::path(
                tempPrefixFor(targetPath) + std::to_wstring(counter) + std::wstring(tempExtension));
        }

        void flushFile(const std::filesystem::path& path)
        {
#ifdef _WIN32
            const HANDLE handle = CreateFileW(
                path.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);
            if (handle == INVALID_HANDLE_VALUE)
            {
                throwFromSystemError(path, AtomicFileWriteOptions{}, std::error_code(
                    static_cast<int>(GetLastError()),
                    std::system_category()));
            }

            const BOOL ok = FlushFileBuffers(handle);
            const DWORD lastError = ok ? ERROR_SUCCESS : GetLastError();
            CloseHandle(handle);
            if (!ok)
            {
                throwFromSystemError(path, AtomicFileWriteOptions{}, std::error_code(
                    static_cast<int>(lastError),
                    std::system_category()));
            }
#else
            const int descriptor = open(path.c_str(), O_RDONLY);
            if (descriptor < 0)
            {
                throwFromSystemError(path, AtomicFileWriteOptions{}, std::error_code(errno, std::generic_category()));
            }

            if (fsync(descriptor) != 0)
            {
                const int saved = errno;
                close(descriptor);
                throwFromSystemError(path, AtomicFileWriteOptions{}, std::error_code(saved, std::generic_category()));
            }

            close(descriptor);
#endif
        }

        void flushDirectory(const std::filesystem::path& directory)
        {
#ifndef _WIN32
            if (directory.empty())
            {
                return;
            }

            const int descriptor = open(directory.c_str(), O_RDONLY);
            if (descriptor < 0)
            {
                return;
            }

            fsync(descriptor);
            close(descriptor);
#else
            (void)directory;
#endif
        }

        void replaceFile(
            const std::filesystem::path& source,
            const std::filesystem::path& target)
        {
#ifdef _WIN32
            if (!MoveFileExW(
                    source.c_str(),
                    target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                throwFromSystemError(target, AtomicFileWriteOptions{}, std::error_code(
                    static_cast<int>(GetLastError()),
                    std::system_category()));
            }
#else
            std::error_code error;
            std::filesystem::rename(source, target, error);
            if (error)
            {
                throwFromSystemError(target, AtomicFileWriteOptions{}, error);
            }
#endif
            flushDirectory(target.parent_path());
        }

        void writeContent(
            const std::filesystem::path& path,
            const std::string& content,
            const AtomicFileWriteOptions& options)
        {
            std::ofstream file(path, std::ios::out | std::ios::trunc | std::ios::binary);
            if (!file)
            {
                throw std::runtime_error("Failed to create temporary state file at \"" + pathForLog(path) + "\".");
            }

            if (options.simulateDiskFullAfterBytes.has_value() &&
                content.size() > options.simulateDiskFullAfterBytes.value())
            {
                const std::size_t bytesToWrite = options.simulateDiskFullAfterBytes.value();
                file.write(content.data(), static_cast<std::streamsize>(bytesToWrite));
                file.flush();
                file.close();
                throw std::runtime_error(actionableDiskFullMessage(path, options));
            }

            file.write(content.data(), static_cast<std::streamsize>(content.size()));
            file.flush();
            if (!file)
            {
                throw std::runtime_error("Failed to flush temporary state file at \"" + pathForLog(path) + "\".");
            }
            file.close();
            if (!file)
            {
                throw std::runtime_error("Failed to close temporary state file at \"" + pathForLog(path) + "\".");
            }
        }

        void triggerSimulatedFailure(
            AtomicWriteFailurePoint actual,
            AtomicWriteFailurePoint expected,
            bool& preserveTemp)
        {
            if (actual != expected)
            {
                return;
            }

            preserveTemp = true;
            throw std::runtime_error("Simulated crash during atomic project state write.");
        }

        void copyFileDurably(
            const std::filesystem::path& source,
            const std::filesystem::path& target)
        {
            const std::filesystem::path tempPath = makeTempPath(target);
            try
            {
                std::filesystem::copy_file(
                    source,
                    tempPath,
                    std::filesystem::copy_options::overwrite_existing);
                flushFile(tempPath);
                replaceFile(tempPath, target);
            }
            catch (...)
            {
                std::error_code cleanupError;
                std::filesystem::remove(tempPath, cleanupError);
                throw;
            }
        }

        bool isValidFile(
            const std::filesystem::path& path,
            const AtomicFileWriteOptions& options)
        {
            try
            {
                AtomicFileStore::validateFile(path, options);
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        std::vector<std::filesystem::path> managedTempFiles(const std::filesystem::path& targetPath)
        {
            std::vector<std::filesystem::path> result;
            const std::filesystem::path parent = targetPath.parent_path();
            std::error_code error;
            if (parent.empty() ||
                !std::filesystem::exists(parent, error) ||
                !std::filesystem::is_directory(parent, error))
            {
                return result;
            }

            for (const auto& entry : std::filesystem::directory_iterator(
                     parent,
                     std::filesystem::directory_options::skip_permission_denied,
                     error))
            {
                if (error)
                {
                    break;
                }

                std::error_code statusError;
                if (!entry.is_regular_file(statusError) ||
                    !AtomicFileStore::isManagedTempFileFor(targetPath, entry.path()))
                {
                    continue;
                }

                result.push_back(entry.path());
            }

            std::sort(result.begin(), result.end());
            return result;
        }

        void removeTempFiles(
            const std::vector<std::filesystem::path>& tempFiles,
            AtomicFileRecoveryResult& result)
        {
            for (const std::filesystem::path& path : tempFiles)
            {
                std::error_code error;
                if (std::filesystem::remove(path, error))
                {
                    result.removedTempFiles.push_back(path);
                }
            }
        }

        void logRecovery(
            Logger* logger,
            LogLevel level,
            std::string_view message)
        {
            if (logger != nullptr)
            {
                logger->write(level, "ProjectStateRecovery", message);
            }
        }
    }

    void AtomicFileStore::writeTextFile(
        const std::filesystem::path& path,
        const std::string& content,
        const AtomicFileWriteOptions& options) const
    {
        if (path.empty())
        {
            throw std::invalid_argument("Atomic project state path is required.");
        }

        const std::filesystem::path parent = path.parent_path();
        if (!parent.empty())
        {
            std::filesystem::create_directories(parent);
        }

        const std::filesystem::path tempPath = makeTempPath(path);
        bool preserveTemp = false;
        try
        {
            writeContent(tempPath, content, options);
            flushFile(tempPath);
            validateFile(tempPath, options);
            triggerSimulatedFailure(
                options.simulateFailurePoint,
                AtomicWriteFailurePoint::AfterTempFileValidated,
                preserveTemp);

            const std::filesystem::path backupPath = backupPathFor(path);
            if (options.keepBackup && std::filesystem::exists(path))
            {
                copyFileDurably(path, backupPath);
            }
            triggerSimulatedFailure(
                options.simulateFailurePoint,
                AtomicWriteFailurePoint::AfterBackupCreated,
                preserveTemp);

            triggerSimulatedFailure(
                options.simulateFailurePoint,
                AtomicWriteFailurePoint::BeforeReplace,
                preserveTemp);
            replaceFile(tempPath, path);
        }
        catch (const std::filesystem::filesystem_error& exception)
        {
            if (!preserveTemp)
            {
                std::error_code cleanupError;
                std::filesystem::remove(tempPath, cleanupError);
            }

            if (looksLikeDiskFull(exception.code()))
            {
                throw std::runtime_error(actionableDiskFullMessage(path, options));
            }

            throw;
        }
        catch (...)
        {
            if (!preserveTemp)
            {
                std::error_code cleanupError;
                std::filesystem::remove(tempPath, cleanupError);
            }
            throw;
        }
    }

    AtomicFileRecoveryResult AtomicFileStore::recoverFile(
        const std::filesystem::path& path,
        const AtomicFileWriteOptions& options,
        Logger* logger) const
    {
        AtomicFileRecoveryResult result;
        result.targetPath = path;
        result.backupPath = backupPathFor(path);

        if (path.empty())
        {
            return result;
        }

        const std::vector<std::filesystem::path> temps = managedTempFiles(path);
        const bool targetExists = std::filesystem::exists(path);
        const bool targetValid = targetExists && isValidFile(path, options);
        if (targetValid)
        {
            removeTempFiles(temps, result);
            if (!result.removedTempFiles.empty())
            {
                result.action = AtomicFileRecoveryAction::RemovedStaleTemp;
                logRecovery(
                    logger,
                    LogLevel::Info,
                    "recoveryAction=removedStaleTemp target=\"" + pathForLog(path) +
                        "\", removedTempFiles=" + std::to_string(result.removedTempFiles.size()) + ".");
            }
            return result;
        }

        std::optional<std::filesystem::path> validBackup;
        if (std::filesystem::exists(result.backupPath) && isValidFile(result.backupPath, options))
        {
            validBackup = result.backupPath;
        }
        else
        {
            const std::filesystem::path legacyBackupPath = legacyBackupPathFor(path);
            if (legacyBackupPath != result.backupPath &&
                std::filesystem::exists(legacyBackupPath) &&
                isValidFile(legacyBackupPath, options))
            {
                validBackup = legacyBackupPath;
            }
        }

        if (validBackup.has_value())
        {
            result.backupPath = *validBackup;
            copyFileDurably(result.backupPath, path);
            removeTempFiles(temps, result);
            result.action = AtomicFileRecoveryAction::RestoredBackup;
            logRecovery(
                logger,
                LogLevel::Warning,
                "recoveryAction=restoredBackup target=\"" + pathForLog(path) +
                    "\", backup=\"" + pathForLog(result.backupPath) +
                    "\", removedTempFiles=" + std::to_string(result.removedTempFiles.size()) + ".");
            return result;
        }

        if (!targetExists)
        {
            for (auto iterator = temps.rbegin(); iterator != temps.rend(); ++iterator)
            {
                if (!isValidFile(*iterator, options))
                {
                    continue;
                }

                replaceFile(*iterator, path);
                result.action = AtomicFileRecoveryAction::PromotedTemp;
                logRecovery(
                    logger,
                    LogLevel::Warning,
                    "recoveryAction=promotedTemp target=\"" + pathForLog(path) +
                        "\", promotedTemp=\"" + pathForLog(*iterator) + "\".");
                const std::vector<std::filesystem::path> remainingTemps = managedTempFiles(path);
                removeTempFiles(remainingTemps, result);
                logRecovery(
                    logger,
                    LogLevel::Info,
                    "recoveryAction=removedRemainingTemps target=\"" + pathForLog(path) +
                        "\", removedTempFiles=" + std::to_string(result.removedTempFiles.size()) + ".");
                return result;
            }
        }

        if (!targetExists && !temps.empty())
        {
            removeTempFiles(temps, result);
            if (!result.removedTempFiles.empty())
            {
                result.action = AtomicFileRecoveryAction::RemovedStaleTemp;
                logRecovery(
                    logger,
                    LogLevel::Info,
                    "recoveryAction=removedStaleTemp target=\"" + pathForLog(path) +
                        "\", removedTempFiles=" + std::to_string(result.removedTempFiles.size()) + ".");
            }
        }

        if (targetExists)
        {
            logRecovery(
                logger,
                LogLevel::Error,
                "recoveryAction=failed target=\"" + pathForLog(path) +
                    "\", backup=\"" + pathForLog(result.backupPath) +
                    "\", tempCandidates=" + std::to_string(temps.size()) + ".");
        }

        return result;
    }

    void AtomicFileStore::validateFile(
        const std::filesystem::path& path,
        const AtomicFileWriteOptions& options)
    {
        if (options.validator)
        {
            options.validator(path);
            return;
        }

        if (options.validation == ProjectStateValidation::None)
        {
            return;
        }

        const std::string content = readBinaryFile(path);
        if (options.validation == ProjectStateValidation::Utf8Text)
        {
            if (!isValidUtf8(content))
            {
                throw std::invalid_argument("State file is not valid UTF-8.");
            }
            return;
        }

        const JsonValue root = JsonReader::parse(decodeUtf8(content));
        if (!root.isObject())
        {
            throw std::invalid_argument("State JSON root must be an object.");
        }
    }

    std::filesystem::path AtomicFileStore::backupPathFor(
        const std::filesystem::path& path)
    {
        return path.parent_path() / std::filesystem::path(L".fb" + stablePathToken(path));
    }

    bool AtomicFileStore::isManagedTempFileFor(
        const std::filesystem::path& targetPath,
        const std::filesystem::path& candidatePath)
    {
        const std::wstring name = candidatePath.filename().wstring();
        const auto hasExpectedShape = [&name](const std::wstring& prefix)
        {
            return name.rfind(prefix, 0) == 0 &&
                name.size() >= tempExtension.size() &&
                name.compare(name.size() - tempExtension.size(), tempExtension.size(), tempExtension) == 0;
        };

        const std::wstring legacyPrefix = L"." + targetPath.filename().wstring() + std::wstring(tempMarker);
        return hasExpectedShape(tempPrefixFor(targetPath)) || hasExpectedShape(legacyPrefix);
    }
}
