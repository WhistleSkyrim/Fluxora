#include "FluxoraCore/Services/BulkFileCopyService.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <exception>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#include <restartmanager.h>
#endif

namespace fluxora
{
    namespace
    {
        struct CopyFileTask
        {
            std::filesystem::path source;
            std::filesystem::path destination;
            std::wstring currentStep;
            std::uintmax_t bytes{0};
        };

        struct CopyProgressState
        {
            std::atomic<std::uintmax_t> copiedBytes{0};
            std::mutex mutex;
            std::chrono::steady_clock::time_point lastReport{};
            int lastPercent{-1};
        };

        struct CopyTaskQueue
        {
            std::deque<CopyFileTask> tasks;
            std::mutex mutex;
            std::condition_variable changed;
            std::size_t maxQueuedTasks{128};
            bool closed{false};
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
                return {};
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

        std::string pathForLog(const std::filesystem::path& path)
        {
            const std::wstring wide = path.wstring();
            if (wide.empty())
            {
                return {};
            }

            const std::string converted = toUtf8(wide);
            return converted.empty() ? path.string() : converted;
        }

        std::string errnoMessage(int value)
        {
            if (value == 0)
            {
                return {};
            }

            char buffer[256]{};
#ifdef _WIN32
            strerror_s(buffer, sizeof(buffer), value);
            return buffer;
#else
            return std::strerror(value);
#endif
        }

        std::string filesystemErrorForLog(const std::error_code& error)
        {
            if (!error)
            {
                return "none";
            }

            std::ostringstream stream;
            stream << "value=" << error.value()
                   << ", category=" << error.category().name()
                   << ", message=" << error.message();
            return stream.str();
        }

        std::string lastIoErrorForLog()
        {
            const int capturedErrno = errno;
            std::ostringstream stream;
            stream << "errno=" << capturedErrno;
            if (const std::string message = errnoMessage(capturedErrno); !message.empty())
            {
                stream << " (" << message << ")";
            }
#ifdef _WIN32
            const DWORD lastError = GetLastError();
            stream << ", GetLastError=" << lastError;
            if (lastError != ERROR_SUCCESS)
            {
                LPSTR raw = nullptr;
                const DWORD length = FormatMessageA(
                    FORMAT_MESSAGE_ALLOCATE_BUFFER |
                        FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS,
                    nullptr,
                    lastError,
                    0,
                    reinterpret_cast<LPSTR>(&raw),
                    0,
                    nullptr);
                if (length != 0 && raw != nullptr)
                {
                    std::string message(raw, raw + length);
                    LocalFree(raw);
                    while (!message.empty() &&
                        (message.back() == '\r' || message.back() == '\n' || message.back() == ' '))
                    {
                        message.pop_back();
                    }
                    if (!message.empty())
                    {
                        stream << " (" << message << ")";
                    }
                }
            }
#endif
            return stream.str();
        }

#ifdef _WIN32
        std::string win32Message(DWORD value)
        {
            if (value == ERROR_SUCCESS)
            {
                return {};
            }

            LPSTR raw = nullptr;
            const DWORD length = FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER |
                    FORMAT_MESSAGE_FROM_SYSTEM |
                    FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr,
                value,
                0,
                reinterpret_cast<LPSTR>(&raw),
                0,
                nullptr);
            if (length == 0 || raw == nullptr)
            {
                return {};
            }

            std::string message(raw, raw + length);
            LocalFree(raw);
            while (!message.empty() && (message.back() == '\r' || message.back() == '\n' || message.back() == ' '))
            {
                message.pop_back();
            }
            return message;
        }

        std::string win32ErrorForLog(DWORD value)
        {
            std::ostringstream stream;
            stream << "GetLastError=" << value;
            if (const std::string message = win32Message(value); !message.empty())
            {
                stream << " (" << message << ")";
            }

            return stream.str();
        }

        std::wstring pathForWin32Io(const std::filesystem::path& path)
        {
            std::filesystem::path fullPath = path;
            if (!fullPath.is_absolute())
            {
                std::error_code error;
                const std::filesystem::path absolutePath = std::filesystem::absolute(fullPath, error);
                if (!error)
                {
                    fullPath = absolutePath;
                }
            }

            fullPath.make_preferred();
            std::wstring value = fullPath.wstring();
            if (value.rfind(L"\\\\?\\", 0) == 0 ||
                value.rfind(L"\\\\.\\", 0) == 0 ||
                value.rfind(L"\\??\\", 0) == 0)
            {
                return value;
            }

            if (value.rfind(L"\\\\", 0) == 0)
            {
                return L"\\\\?\\UNC\\" + value.substr(2);
            }

            return L"\\\\?\\" + value;
        }

        struct Win32FileHandle
        {
            Win32FileHandle() = default;

            explicit Win32FileHandle(HANDLE fileHandle) noexcept
                : handle(fileHandle)
            {
            }

            Win32FileHandle(const Win32FileHandle&) = delete;
            Win32FileHandle& operator=(const Win32FileHandle&) = delete;

            Win32FileHandle(Win32FileHandle&& other) noexcept
                : handle(std::exchange(other.handle, INVALID_HANDLE_VALUE))
            {
            }

            Win32FileHandle& operator=(Win32FileHandle&& other) noexcept
            {
                if (this != &other)
                {
                    close();
                    handle = std::exchange(other.handle, INVALID_HANDLE_VALUE);
                }

                return *this;
            }

            ~Win32FileHandle()
            {
                close();
            }

            void reset(HANDLE fileHandle = INVALID_HANDLE_VALUE)
            {
                if (handle != fileHandle)
                {
                    close();
                    handle = fileHandle;
                }
            }

            bool close()
            {
                if (handle == INVALID_HANDLE_VALUE)
                {
                    return true;
                }

                const HANDLE current = handle;
                handle = INVALID_HANDLE_VALUE;
                return CloseHandle(current) != FALSE;
            }

            bool valid() const
            {
                return handle != INVALID_HANDLE_VALUE;
            }

            HANDLE get() const
            {
                return handle;
            }

            HANDLE handle{INVALID_HANDLE_VALUE};
        };
#endif

        std::string pathStateForLog(const std::filesystem::path& path)
        {
            std::ostringstream stream;
            stream << pathForLog(path) << " [";

            std::error_code error;
            const bool exists = std::filesystem::exists(path, error);
            stream << "exists=" << (exists ? "true" : "false");
            if (error)
            {
                stream << ", existsError={" << filesystemErrorForLog(error) << "}]";
                return stream.str();
            }

            error.clear();
            const bool isDirectory = std::filesystem::is_directory(path, error);
            stream << ", directory=" << (isDirectory ? "true" : "false");
            if (error)
            {
                stream << ", directoryError={" << filesystemErrorForLog(error) << "}";
            }

            error.clear();
            const bool isRegularFile = std::filesystem::is_regular_file(path, error);
            stream << ", regularFile=" << (isRegularFile ? "true" : "false");
            if (error)
            {
                stream << ", regularFileError={" << filesystemErrorForLog(error) << "}";
            }

            if (isRegularFile)
            {
                error.clear();
                const std::uintmax_t size = std::filesystem::file_size(path, error);
                if (!error)
                {
                    stream << ", size=" << size;
                }
                else
                {
                    stream << ", sizeError={" << filesystemErrorForLog(error) << "}";
                }
            }

            stream << "]";
            return stream.str();
        }

        void writeDiagnostic(
            const BulkFileCopyOptions& options,
            LogLevel level,
            std::string_view message)
        {
            if (options.diagnostics)
            {
                options.diagnostics(level, message);
            }
        }

        std::string copyFailureMessage(
            std::string_view baseMessage,
            const CopyFileTask& task)
        {
            std::ostringstream stream;
            stream << baseMessage
                   << " source=\"" << pathForLog(task.source) << "\""
                   << " destination=\"" << pathForLog(task.destination) << "\"";
            return stream.str();
        }

        void logCopyFailure(
            const Logger& logger,
            const BulkFileCopyOptions& options,
            const CopyFileTask& task,
            std::string_view reason,
            std::string_view details)
        {
            std::ostringstream stream;
            stream << "copy-file-failure: " << reason << "\n"
                   << "  source=" << pathStateForLog(task.source) << "\n"
                   << "  destination=" << pathStateForLog(task.destination) << "\n"
                   << "  destinationParent=" << pathStateForLog(task.destination.parent_path()) << "\n"
                   << "  plannedBytes=" << task.bytes << "\n"
                   << "  details=" << details;
            writeDiagnostic(options, LogLevel::Error, stream.str());

            std::ostringstream summary;
            summary << "Copy failed during bulk file copy. reason=" << reason
                    << ", source=\"" << pathForLog(task.source) << "\""
                    << ", destination=\"" << pathForLog(task.destination) << "\""
                    << ", plannedBytes=" << task.bytes;
            logger.write(LogLevel::Error, "BulkFileCopy", summary.str());
        }

        bool isTransientFilesystemError(const std::error_code& error)
        {
#ifdef _WIN32
            return error.value() == ERROR_SHARING_VIOLATION ||
                error.value() == ERROR_ACCESS_DENIED ||
                error.value() == ERROR_LOCK_VIOLATION;
#else
            (void)error;
            return false;
#endif
        }

        bool closeProcessesLockingPath(const std::filesystem::path& path)
        {
#ifdef _WIN32
            std::error_code absoluteError;
            const std::filesystem::path absolutePath = std::filesystem::absolute(path, absoluteError);
            const std::wstring resource = (absoluteError ? path : absolutePath).wstring();
            if (resource.empty())
            {
                return false;
            }

            DWORD sessionHandle = 0;
            WCHAR sessionKey[CCH_RM_SESSION_KEY + 1]{};
            if (RmStartSession(&sessionHandle, 0, sessionKey) != ERROR_SUCCESS)
            {
                return false;
            }

            struct SessionGuard
            {
                DWORD handle{0};
                bool active{false};

                ~SessionGuard()
                {
                    if (active)
                    {
                        RmEndSession(handle);
                    }
                }
            } guard{sessionHandle, true};

            LPCWSTR resources[] = {resource.c_str()};
            if (RmRegisterResources(sessionHandle, 1, resources, 0, nullptr, 0, nullptr) != ERROR_SUCCESS)
            {
                return false;
            }

            UINT processInfoNeeded = 0;
            UINT processInfoCount = 0;
            DWORD rebootReasons = RmRebootReasonNone;
            DWORD result = RmGetList(
                sessionHandle,
                &processInfoNeeded,
                &processInfoCount,
                nullptr,
                &rebootReasons);
            if (result == ERROR_SUCCESS || processInfoNeeded == 0)
            {
                return false;
            }

            if (result != ERROR_MORE_DATA)
            {
                return false;
            }

            std::vector<RM_PROCESS_INFO> processes(processInfoNeeded);
            processInfoCount = processInfoNeeded;
            result = RmGetList(
                sessionHandle,
                &processInfoNeeded,
                &processInfoCount,
                processes.data(),
                &rebootReasons);
            if (result != ERROR_SUCCESS || processInfoCount == 0)
            {
                return false;
            }

            const DWORD currentProcessId = GetCurrentProcessId();
            const bool hasExternalLocker = std::any_of(
                processes.begin(),
                processes.begin() + static_cast<std::ptrdiff_t>(processInfoCount),
                [currentProcessId](const RM_PROCESS_INFO& process)
                {
                    return process.Process.dwProcessId != currentProcessId;
                });
            if (!hasExternalLocker)
            {
                return false;
            }

            return RmShutdown(sessionHandle, 0, nullptr) == ERROR_SUCCESS;
#else
            (void)path;
            return false;
#endif
        }

        template <typename Operation>
        std::error_code retryTransientFilesystemOperation(Operation&& operation)
        {
            constexpr int maxAttempts = 6;
            std::error_code error;
            for (int attempt = 1; attempt <= maxAttempts; ++attempt)
            {
                error.clear();
                operation(error);
                if (!error || attempt == maxAttempts || !isTransientFilesystemError(error))
                {
                    return error;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(100 * attempt));
            }

            return error;
        }

        template <typename Operation>
        std::error_code retryTransientFilesystemOperationWithUnlock(
            const std::filesystem::path& lockedPath,
            Operation&& operation)
        {
            std::error_code error = retryTransientFilesystemOperation(std::forward<Operation>(operation));
            if (error && isTransientFilesystemError(error) && closeProcessesLockingPath(lockedPath))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                error = retryTransientFilesystemOperation(std::forward<Operation>(operation));
            }

            return error;
        }

        void publishCopyProgress(
            CopyProgressState& state,
            std::uintmax_t totalBytes,
            const std::function<void(const BulkFileCopyProgress&)>& progress,
            std::wstring_view currentStep,
            const std::filesystem::path& currentItem,
            bool force)
        {
            if (!progress)
            {
                return;
            }

            const std::uintmax_t copiedBytes = state.copiedBytes.load(std::memory_order_relaxed);
            const int copyPercent = totalBytes == 0
                ? 100
                : static_cast<int>(((std::min)(copiedBytes, totalBytes) * 100) / totalBytes);
            const auto now = std::chrono::steady_clock::now();

            std::lock_guard lock(state.mutex);
            if (!force &&
                state.lastPercent == copyPercent &&
                now - state.lastReport < std::chrono::milliseconds(150))
            {
                return;
            }

            state.lastPercent = copyPercent;
            state.lastReport = now;
            progress(BulkFileCopyProgress{
                std::wstring(currentStep),
                currentItem,
                copiedBytes,
                totalBytes
            });
        }

        void addCopiedBytes(
            CopyProgressState& state,
            std::uintmax_t bytes,
            std::uintmax_t totalBytes,
            const std::function<void(const BulkFileCopyProgress&)>& progress,
            std::wstring_view currentStep,
            const std::filesystem::path& currentItem,
            bool force = false)
        {
            if (bytes > 0)
            {
                state.copiedBytes.fetch_add(bytes, std::memory_order_relaxed);
            }
            publishCopyProgress(state, totalBytes, progress, currentStep, currentItem, force);
        }

#ifdef _WIN32
        void copyFileWithProgressWin32(
            const CopyFileTask& task,
            CopyProgressState& state,
            std::uintmax_t totalBytes,
            const std::function<void(const BulkFileCopyProgress&)>& progress,
            const Logger& logger,
            const BulkFileCopyOptions& options)
        {
            SetLastError(ERROR_SUCCESS);
            Win32FileHandle input(CreateFileW(
                pathForWin32Io(task.source).c_str(),
                GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                nullptr));
            if (!input.valid())
            {
                const DWORD error = GetLastError();
                logCopyFailure(logger, options, task, "open-source-file-failed", win32ErrorForLog(error));
                throw std::runtime_error(copyFailureMessage("Failed to read source file during import.", task));
            }

            Win32FileHandle output;
            DWORD openError = ERROR_SUCCESS;
            bool closeAttempted = false;
            for (int attempt = 1; ; ++attempt)
            {
                SetLastError(ERROR_SUCCESS);
                output.reset(CreateFileW(
                    pathForWin32Io(task.destination).c_str(),
                    GENERIC_WRITE,
                    FILE_SHARE_READ,
                    nullptr,
                    CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                    nullptr));
                if (output.valid())
                {
                    break;
                }

                openError = GetLastError();
                if (!closeAttempted &&
                    (openError == ERROR_ACCESS_DENIED ||
                        openError == ERROR_LOCK_VIOLATION ||
                        openError == ERROR_SHARING_VIOLATION))
                {
                    closeAttempted = closeProcessesLockingPath(task.destination);
                    if (closeAttempted)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(250));
                        continue;
                    }
                }

                if (attempt >= 6)
                {
                    std::ostringstream details;
                    details << "attempts=" << attempt << ", " << win32ErrorForLog(openError);
                    logCopyFailure(logger, options, task, "open-destination-file-failed", details.str());
                    throw std::runtime_error(copyFailureMessage("Failed to write target file during import.", task));
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(100 * attempt));
            }

            std::vector<char> buffer(4 * 1024 * 1024);
            for (;;)
            {
                DWORD bytesRead = 0;
                if (ReadFile(
                        input.get(),
                        buffer.data(),
                        static_cast<DWORD>(buffer.size()),
                        &bytesRead,
                        nullptr) == FALSE)
                {
                    const DWORD error = GetLastError();
                    logCopyFailure(logger, options, task, "read-source-file-failed", win32ErrorForLog(error));
                    throw std::runtime_error(copyFailureMessage("Failed to read source file during import.", task));
                }

                if (bytesRead == 0)
                {
                    break;
                }

                DWORD totalWritten = 0;
                while (totalWritten < bytesRead)
                {
                    DWORD bytesWritten = 0;
                    if (WriteFile(
                            output.get(),
                            buffer.data() + totalWritten,
                            bytesRead - totalWritten,
                            &bytesWritten,
                            nullptr) == FALSE ||
                        bytesWritten == 0)
                    {
                        const DWORD error = GetLastError();
                        std::ostringstream details;
                        details << "writeBytes=" << (bytesRead - totalWritten)
                                << ", writtenBytes=" << bytesWritten
                                << ", " << win32ErrorForLog(error);
                        logCopyFailure(logger, options, task, "write-destination-file-failed", details.str());
                        throw std::runtime_error(copyFailureMessage("Failed to write target file during import.", task));
                    }

                    totalWritten += bytesWritten;
                    addCopiedBytes(
                        state,
                        static_cast<std::uintmax_t>(bytesWritten),
                        totalBytes,
                        progress,
                        task.currentStep,
                        task.source);
                }
            }

            if (!output.close())
            {
                const DWORD error = GetLastError();
                logCopyFailure(logger, options, task, "close-destination-file-failed", win32ErrorForLog(error));
                throw std::runtime_error(copyFailureMessage("Failed to write target file during import.", task));
            }
        }
#endif

        void createDirectoryDuringCopy(
            const std::filesystem::path& directory,
            const Logger& logger,
            const BulkFileCopyOptions& options)
        {
            const std::error_code error = retryTransientFilesystemOperation(
                [&directory](std::error_code& code)
                {
                    std::filesystem::create_directories(directory, code);
                });
            if (!error)
            {
                return;
            }

            std::ostringstream stream;
            stream << "create-copy-directory-failed\n"
                   << "  directory=" << pathStateForLog(directory) << "\n"
                   << "  error={" << filesystemErrorForLog(error) << "}";
            writeDiagnostic(options, LogLevel::Error, stream.str());
            logger.write(
                LogLevel::Error,
                "BulkFileCopy",
                std::string("Failed to create copy directory. directory=\"") +
                    pathForLog(directory) + "\", error={" + filesystemErrorForLog(error) + "}");
            throw std::runtime_error("Failed to create the target folder during import.");
        }

        void copyFileWithProgress(
            const CopyFileTask& task,
            CopyProgressState& state,
            std::uintmax_t totalBytes,
            const std::function<void(const BulkFileCopyProgress&)>& progress,
            const Logger& logger,
            const BulkFileCopyOptions& options)
        {
#ifdef _WIN32
            copyFileWithProgressWin32(task, state, totalBytes, progress, logger, options);
#else
            errno = 0;
            std::ifstream input(task.source, std::ios::in | std::ios::binary);
            if (!input)
            {
                logCopyFailure(logger, options, task, "open-source-file-failed", lastIoErrorForLog());
                throw std::runtime_error(copyFailureMessage("Failed to read source file during import.", task));
            }

            std::ofstream output;
            bool closeAttempted = false;
            for (int attempt = 1; ; ++attempt)
            {
                errno = 0;
                output.open(task.destination, std::ios::out | std::ios::trunc | std::ios::binary);
                if (output)
                {
                    break;
                }

                if (!closeAttempted)
                {
                    closeAttempted = closeProcessesLockingPath(task.destination);
                    if (closeAttempted)
                    {
                        output.clear();
                        std::this_thread::sleep_for(std::chrono::milliseconds(250));
                        continue;
                    }
                }

                if (attempt >= 6)
                {
                    std::ostringstream details;
                    details << "attempts=" << attempt << ", " << lastIoErrorForLog();
                    logCopyFailure(logger, options, task, "open-destination-file-failed", details.str());
                    throw std::runtime_error(copyFailureMessage("Failed to write target file during import.", task));
                }

                output.clear();
                std::this_thread::sleep_for(std::chrono::milliseconds(100 * attempt));
            }

            std::vector<char> buffer(4 * 1024 * 1024);
            while (input)
            {
                input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const std::streamsize read = input.gcount();
                if (read <= 0)
                {
                    break;
                }

                errno = 0;
                output.write(buffer.data(), read);
                if (!output)
                {
                    std::ostringstream details;
                    details << "writeBytes=" << read << ", " << lastIoErrorForLog();
                    logCopyFailure(logger, options, task, "write-destination-file-failed", details.str());
                    throw std::runtime_error(copyFailureMessage("Failed to write target file during import.", task));
                }

                addCopiedBytes(
                    state,
                    static_cast<std::uintmax_t>(read),
                    totalBytes,
                    progress,
                    task.currentStep,
                    task.source);
            }

            errno = 0;
            output.close();
            if (!output)
            {
                logCopyFailure(logger, options, task, "close-destination-file-failed", lastIoErrorForLog());
                throw std::runtime_error(copyFailureMessage("Failed to write target file during import.", task));
            }
#endif
        }

        bool enqueueCopyTask(
            CopyTaskQueue& queue,
            CopyFileTask task,
            const std::atomic<bool>& shouldStop)
        {
            std::unique_lock lock(queue.mutex);
            queue.changed.wait(lock, [&]()
            {
                return shouldStop.load(std::memory_order_relaxed) ||
                    queue.closed ||
                    queue.tasks.size() < queue.maxQueuedTasks;
            });

            if (shouldStop.load(std::memory_order_relaxed) || queue.closed)
            {
                return false;
            }

            queue.tasks.push_back(std::move(task));
            lock.unlock();
            queue.changed.notify_one();
            return true;
        }

        std::optional<CopyFileTask> dequeueCopyTask(
            CopyTaskQueue& queue,
            const std::atomic<bool>& shouldStop)
        {
            std::unique_lock lock(queue.mutex);
            queue.changed.wait(lock, [&]()
            {
                return shouldStop.load(std::memory_order_relaxed) ||
                    queue.closed ||
                    !queue.tasks.empty();
            });

            if (shouldStop.load(std::memory_order_relaxed) || queue.tasks.empty())
            {
                return std::nullopt;
            }

            CopyFileTask task = std::move(queue.tasks.front());
            queue.tasks.pop_front();
            lock.unlock();
            queue.changed.notify_one();
            return task;
        }

        void closeCopyQueue(CopyTaskQueue& queue)
        {
            {
                std::lock_guard lock(queue.mutex);
                queue.closed = true;
            }
            queue.changed.notify_all();
        }

        void enumerateRootTasks(
            const BulkFileCopyRoot& root,
            CopyTaskQueue& queue,
            CopyProgressState& state,
            const BulkFileCopyOptions& options,
            const Logger& logger,
            const std::atomic<bool>& shouldStop)
        {
            std::error_code existsError;
            const bool sourceExists = std::filesystem::exists(root.sourceDirectory, existsError);
            if (existsError)
            {
                throw std::runtime_error(
                    std::string("Failed to inspect source directory during import. sourceDirectory=\"") +
                    pathForLog(root.sourceDirectory) + "\", error={" + filesystemErrorForLog(existsError) + "}");
            }
            if (!sourceExists)
            {
                return;
            }

            createDirectoryDuringCopy(root.destinationDirectory, logger, options);
            publishCopyProgress(
                state,
                options.totalBytes,
                options.progress,
                root.currentStep,
                root.sourceDirectory,
                false);

            std::error_code error;
            std::filesystem::recursive_directory_iterator iterator(
                root.sourceDirectory,
                std::filesystem::directory_options::skip_permission_denied,
                error);
            const std::filesystem::recursive_directory_iterator end;

            while (!error && iterator != end)
            {
                if (shouldStop.load(std::memory_order_relaxed))
                {
                    return;
                }

                const std::filesystem::path source = iterator->path();
                if (root.shouldSkip && root.shouldSkip(source))
                {
                    std::error_code typeError;
                    if (iterator->is_directory(typeError))
                    {
                        iterator.disable_recursion_pending();
                    }
                    iterator.increment(error);
                    continue;
                }

                std::error_code relativeError;
                const std::filesystem::path relative =
                    std::filesystem::relative(source, root.sourceDirectory, relativeError);
                if (relativeError)
                {
                    throw std::runtime_error(
                        std::string("Failed to resolve relative import path.") +
                        " source=\"" + pathForLog(source) +
                        "\", sourceDirectory=\"" + pathForLog(root.sourceDirectory) +
                        "\", error={" + filesystemErrorForLog(relativeError) + "}");
                }

                const std::filesystem::path destination = root.destinationDirectory / relative;

                std::error_code typeError;
                if (iterator->is_directory(typeError))
                {
                    createDirectoryDuringCopy(destination, logger, options);
                }
                else if (!typeError && iterator->is_regular_file(typeError))
                {
                    std::error_code sizeError;
                    const std::uintmax_t bytes = iterator->file_size(sizeError);
                    if (!enqueueCopyTask(
                            queue,
                            CopyFileTask{
                                source,
                                destination,
                                root.currentStep,
                                sizeError ? 0 : bytes
                            },
                            shouldStop))
                    {
                        return;
                    }
                }
                else if (typeError)
                {
                    throw std::runtime_error(
                        std::string("Failed to read import entry type.") +
                        " source=\"" + pathForLog(source) +
                        "\", error={" + filesystemErrorForLog(typeError) + "}");
                }

                iterator.increment(error);
            }

            if (error)
            {
                throw std::runtime_error(
                    std::string("Failed while copying directory tree.") +
                    " sourceDirectory=\"" + pathForLog(root.sourceDirectory) +
                    "\", destinationDirectory=\"" + pathForLog(root.destinationDirectory) +
                    "\", error={" + filesystemErrorForLog(error) + "}");
            }
        }
    }

    BulkFileCopyService::BulkFileCopyService(const Logger& logger) noexcept
        : logger_(logger)
    {
    }

    std::uintmax_t BulkFileCopyService::copy(
        const std::vector<BulkFileCopyRoot>& roots,
        const BulkFileCopyOptions& options) const
    {
        if (roots.empty())
        {
            CopyProgressState emptyState;
            publishCopyProgress(emptyState, 0, options.progress, L"Копирую файлы", L"Готово", true);
            return 0;
        }

        CopyProgressState state;
        std::atomic<bool> shouldStop{false};
        std::exception_ptr firstError;
        std::mutex errorMutex;

        const unsigned int hardwareThreads = std::thread::hardware_concurrency();
        const std::size_t detectedWorkers = hardwareThreads == 0 ? 4 : hardwareThreads;
        const std::size_t workerLimit = options.maxWorkers == 0
            ? (std::min<std::size_t>)(detectedWorkers, 8)
            : options.maxWorkers;
        const std::size_t workerCount = (std::max<std::size_t>)(1, workerLimit);
        CopyTaskQueue queue;
        queue.maxQueuedTasks = (std::max<std::size_t>)(64, workerCount * 16);

        const auto rememberCurrentException = [&]()
        {
            {
                std::lock_guard lock(errorMutex);
                if (!firstError)
                {
                    firstError = std::current_exception();
                }
            }
            shouldStop.store(true, std::memory_order_relaxed);
            closeCopyQueue(queue);
        };

        logger_.write(
            LogLevel::Info,
            "BulkFileCopy",
            std::string("Bulk streaming copy started. roots=") + std::to_string(roots.size()) +
                ", workers=" + std::to_string(workerCount) +
                ", queueLimit=" + std::to_string(queue.maxQueuedTasks) +
                ", totalBytes=" + std::to_string(options.totalBytes));

        std::vector<std::thread> workers;
        workers.reserve(workerCount);
        for (std::size_t worker = 0; worker < workerCount; ++worker)
        {
            workers.emplace_back([&, worker]()
            {
                (void)worker;
                while (!shouldStop.load(std::memory_order_relaxed))
                {
                    std::optional<CopyFileTask> task = dequeueCopyTask(queue, shouldStop);
                    if (!task)
                    {
                        break;
                    }

                    try
                    {
                        copyFileWithProgress(
                            *task,
                            state,
                            options.totalBytes,
                            options.progress,
                            logger_,
                            options);
                    }
                    catch (const std::exception& exception)
                    {
                        std::ostringstream stream;
                        stream << "copy-worker-exception: " << exception.what() << "\n"
                               << "  source=" << pathForLog(task->source) << "\n"
                               << "  destination=" << pathForLog(task->destination);
                        writeDiagnostic(options, LogLevel::Error, stream.str());
                        logger_.write(
                            LogLevel::Error,
                            "BulkFileCopy",
                            std::string("Bulk copy worker exception. error=\"") + exception.what() +
                                "\", source=\"" + pathForLog(task->source) +
                                "\", destination=\"" + pathForLog(task->destination) + "\"");

                        rememberCurrentException();
                        break;
                    }
                    catch (...)
                    {
                        std::ostringstream stream;
                        stream << "copy-worker-unknown-exception\n"
                               << "  source=" << pathForLog(task->source) << "\n"
                               << "  destination=" << pathForLog(task->destination);
                        writeDiagnostic(options, LogLevel::Error, stream.str());
                        logger_.write(
                            LogLevel::Error,
                            "BulkFileCopy",
                            std::string("Unknown bulk copy worker exception. source=\"") +
                                pathForLog(task->source) + "\", destination=\"" +
                                pathForLog(task->destination) + "\"");

                        rememberCurrentException();
                        break;
                    }
                }
            });
        }

        std::thread producer([&]()
        {
            try
            {
                for (const BulkFileCopyRoot& root : roots)
                {
                    if (shouldStop.load(std::memory_order_relaxed))
                    {
                        break;
                    }

                    enumerateRootTasks(root, queue, state, options, logger_, shouldStop);
                }
            }
            catch (const std::exception& exception)
            {
                writeDiagnostic(options, LogLevel::Error, std::string("copy-producer-exception: ") + exception.what());
                logger_.write(
                    LogLevel::Error,
                    "BulkFileCopy",
                    std::string("Bulk copy producer exception. error=\"") + exception.what() + "\"");
                rememberCurrentException();
            }
            catch (...)
            {
                writeDiagnostic(options, LogLevel::Error, "copy-producer-unknown-exception");
                logger_.write(LogLevel::Error, "BulkFileCopy", "Unknown bulk copy producer exception.");
                rememberCurrentException();
            }

            closeCopyQueue(queue);
        });

        producer.join();
        for (std::thread& worker : workers)
        {
            worker.join();
        }

        if (firstError)
        {
            std::rethrow_exception(firstError);
        }

        publishCopyProgress(state, options.totalBytes, options.progress, L"Файлы скопированы", L"Готово", true);
        const std::uintmax_t copiedBytes = state.copiedBytes.load(std::memory_order_relaxed);
        logger_.write(
            LogLevel::Info,
            "BulkFileCopy",
            std::string("Bulk streaming copy completed. copiedBytes=") + std::to_string(copiedBytes) +
                ", totalBytes=" + std::to_string(options.totalBytes));
        return copiedBytes;
    }
}
