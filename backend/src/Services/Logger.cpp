#include "FluxoraCore/Services/Logger.hpp"

#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#ifdef _WIN32
#include <spdlog/sinks/msvc_sink.h>
#endif
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <exception>
#include <sstream>
#include <string>
#include <thread>
#include <ctime>
#include <vector>

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
        thread_local std::string currentOperationId;
        std::atomic_uint64_t loggerInstanceCounter{0};

        std::filesystem::path processCrashLogPath;
        std::mutex processCrashLogMutex;
        std::terminate_handler previousTerminateHandler = nullptr;
        std::once_flag terminateHandlerOnce;

        std::string_view toLabel(LogLevel level) noexcept
        {
            switch (level)
            {
            case LogLevel::Debug:
                return "DEBUG";
            case LogLevel::Info:
                return "INFO";
            case LogLevel::Warning:
                return "WARNING";
            case LogLevel::Error:
                return "ERROR";
            }

            return "UNKNOWN";
        }

        std::string_view toChannelLabel(LogChannel channel) noexcept
        {
            switch (channel)
            {
            case LogChannel::Core:
                return "Core";
            case LogChannel::Bridge:
                return "Bridge";
            case LogChannel::Operations:
                return "Operations";
            case LogChannel::Crash:
                return "Crash";
            }

            return "Core";
        }

        spdlog::level::level_enum toSpdLevel(LogLevel level) noexcept
        {
            switch (level)
            {
            case LogLevel::Debug:
                return spdlog::level::debug;
            case LogLevel::Info:
                return spdlog::level::info;
            case LogLevel::Warning:
                return spdlog::level::warn;
            case LogLevel::Error:
                return spdlog::level::err;
            }

            return spdlog::level::info;
        }

        std::tm localTimeNow()
        {
            const std::time_t now = std::time(nullptr);
            std::tm local{};
#ifdef _WIN32
            localtime_s(&local, &now);
#else
            localtime_r(&now, &local);
#endif
            return local;
        }

        std::string timestamp()
        {
            const auto now = std::chrono::system_clock::now();
            const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) % 1000;
            const std::tm local = localTimeNow();

            std::ostringstream stream;
            stream << std::put_time(&local, "%Y-%m-%d %H:%M:%S")
                   << "." << std::setfill('0') << std::setw(3) << milliseconds.count();
            return stream.str();
        }

        std::wstring logDateStamp()
        {
            const std::tm local = localTimeNow();
            std::wostringstream stream;
            stream << std::put_time(&local, L"%Y%m%d");
            return stream.str();
        }

#ifdef _WIN32
        std::wstring readEnvironmentVariable(const wchar_t* name)
        {
            const DWORD requiredLength = GetEnvironmentVariableW(name, nullptr, 0);
            if (requiredLength == 0)
            {
                return {};
            }

            std::wstring value(requiredLength, L'\0');
            const DWORD actualLength = GetEnvironmentVariableW(name, value.data(), requiredLength);
            if (actualLength == 0 || actualLength >= requiredLength)
            {
                return {};
            }

            value.resize(actualLength);
            return value;
        }

        std::filesystem::path executableDirectory()
        {
            std::wstring buffer(MAX_PATH, L'\0');
            DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            while (length == buffer.size())
            {
                buffer.resize(buffer.size() * 2);
                length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            }

            if (length == 0)
            {
                return {};
            }

            buffer.resize(length);
            return std::filesystem::path(buffer).parent_path();
        }

        std::filesystem::path appDataLogDirectory()
        {
            if (const std::wstring appData = readEnvironmentVariable(L"APPDATA"); !appData.empty())
            {
                return std::filesystem::path(appData) / L"Fluxora" / L"logs";
            }

            return {};
        }

        std::filesystem::path tempLogDirectory()
        {
            return std::filesystem::temp_directory_path() / L"Fluxora" / L"logs";
        }

        bool isDebugLoggingEnabled()
        {
            const std::wstring value = readEnvironmentVariable(L"FLUXORA_DEBUG_LOGS");
            return value == L"1" ||
                value == L"true" ||
                value == L"TRUE" ||
                value == L"yes" ||
                value == L"YES";
        }

        std::string toUtf8(std::wstring_view wide)
        {
            if (wide.empty())
            {
                return {};
            }

            const int requiredLength = WideCharToMultiByte(
                CP_UTF8,
                0,
                wide.data(),
                static_cast<int>(wide.size()),
                nullptr,
                0,
                nullptr,
                nullptr);
            if (requiredLength <= 0)
            {
                return {};
            }

            std::string value(static_cast<std::size_t>(requiredLength), '\0');
            WideCharToMultiByte(
                CP_UTF8,
                0,
                wide.data(),
                static_cast<int>(wide.size()),
                value.data(),
                requiredLength,
                nullptr,
                nullptr);
            return value;
        }

        std::string pathForLog(const std::filesystem::path& path)
        {
            const std::string converted = toUtf8(path.wstring());
            return converted.empty() ? path.string() : converted;
        }
#else
        std::filesystem::path executableDirectory()
        {
            std::error_code error;
            const std::filesystem::path current = std::filesystem::current_path(error);
            return error ? std::filesystem::path{} : current;
        }

        std::filesystem::path appDataLogDirectory()
        {
            const char* home = std::getenv("HOME");
            return home == nullptr
                ? std::filesystem::path{}
                : std::filesystem::path(home) / ".local" / "state" / "fluxora" / "logs";
        }

        std::filesystem::path tempLogDirectory()
        {
            return std::filesystem::temp_directory_path() / "Fluxora" / "logs";
        }

        bool isDebugLoggingEnabled()
        {
            const char* value = std::getenv("FLUXORA_DEBUG_LOGS");
            return value != nullptr &&
                (std::string_view(value) == "1" ||
                 std::string_view(value) == "true" ||
                 std::string_view(value) == "yes");
        }

        std::string toUtf8(std::wstring_view wide)
        {
            return std::string(wide.begin(), wide.end());
        }

        std::string pathForLog(const std::filesystem::path& path)
        {
            return path.string();
        }
#endif

        std::filesystem::path makeAbsolute(const std::filesystem::path& path)
        {
            std::error_code error;
            const std::filesystem::path absolute = std::filesystem::absolute(path, error);
            return error ? path : absolute;
        }

        bool canAppendFile(const std::filesystem::path& path)
        {
            std::ofstream file(path, std::ios::out | std::ios::app | std::ios::binary);
            return static_cast<bool>(file);
        }

        std::filesystem::path chooseLogDirectory()
        {
            std::vector<std::filesystem::path> directories;
            if (const std::filesystem::path directory = executableDirectory(); !directory.empty())
            {
                directories.push_back(directory / L"logs");
            }

            if (const std::filesystem::path directory = appDataLogDirectory(); !directory.empty())
            {
                directories.push_back(directory);
            }

            directories.push_back(tempLogDirectory());

            for (const std::filesystem::path& directory : directories)
            {
                std::error_code error;
                std::filesystem::create_directories(directory, error);
                if (error)
                {
                    continue;
                }

                const std::filesystem::path probe = directory / L".fluxora-log-probe";
                if (canAppendFile(probe))
                {
                    std::filesystem::remove(probe, error);
                    return makeAbsolute(directory);
                }
            }

            return {};
        }

        std::filesystem::path logPathFor(const std::filesystem::path& directory, std::wstring_view name)
        {
            return directory.empty()
                ? std::filesystem::path{}
                : directory / (std::wstring(name) + L"-" + logDateStamp() + L".log");
        }

        std::shared_ptr<spdlog::logger> createLogger(
            std::string_view name,
            const std::filesystem::path& path,
            bool debugEnabled)
        {
            if (path.empty())
            {
                return {};
            }

            std::vector<spdlog::sink_ptr> sinks;
            auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(pathForLog(path), true);
            fileSink->set_level(spdlog::level::trace);
            sinks.push_back(fileSink);

#ifdef _WIN32
            auto debugSink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
            debugSink->set_level(debugEnabled ? spdlog::level::debug : spdlog::level::info);
            sinks.push_back(debugSink);
#endif

            const std::uint64_t instance = ++loggerInstanceCounter;
            auto logger = std::make_shared<spdlog::logger>(
                std::string(name) + "-" + std::to_string(instance),
                sinks.begin(),
                sinks.end());
            logger->set_pattern("%v");
            logger->set_level(debugEnabled ? spdlog::level::debug : spdlog::level::info);
            logger->flush_on(spdlog::level::warn);
            spdlog::register_logger(logger);
            return logger;
        }

        void dropLogger(const std::shared_ptr<spdlog::logger>& logger) noexcept
        {
            if (!logger)
            {
                return;
            }

            try
            {
                logger->flush();
                spdlog::drop(logger->name());
            }
            catch (...)
            {
            }
        }

        std::string formatLine(
            LogChannel channel,
            LogLevel level,
            std::string_view category,
            std::string_view message)
        {
            const std::string safeCategory = category.empty()
                ? std::string(toChannelLabel(channel))
                : std::string(category);

            std::ostringstream stream;
            stream << "[" << timestamp() << "] "
                   << "[" << toLabel(level) << "] "
                   << "[" << toChannelLabel(channel) << "] "
                   << "[" << safeCategory << "] "
                   << "[tid=" << std::this_thread::get_id() << "]";

            if (!currentOperationId.empty())
            {
                stream << " [op=" << currentOperationId << "]"
                       << " [operationId=" << currentOperationId << "]";
            }

            stream << " " << message;
            return stream.str();
        }

        void fallbackWrite(const std::string& line)
        {
#ifdef _WIN32
            OutputDebugStringA((line + "\n").c_str());
#endif
            std::clog << line << '\n';
        }

        void writeRawCrashLine(std::string_view message) noexcept
        {
            try
            {
                std::ostringstream line;
                line << "[" << timestamp() << "] "
                     << "[ERROR] "
                     << "[Crash] "
                     << "[NativeCrash] "
                     << "[tid=" << std::this_thread::get_id() << "]";
                if (!currentOperationId.empty())
                {
                    line << " [op=" << currentOperationId << "]"
                         << " [operationId=" << currentOperationId << "]";
                }
                line << " " << message;

                std::lock_guard lock(processCrashLogMutex);
                if (!processCrashLogPath.empty())
                {
                    std::filesystem::create_directories(processCrashLogPath.parent_path());
                    std::ofstream file(processCrashLogPath, std::ios::out | std::ios::app | std::ios::binary);
                    if (file)
                    {
                        file << line.str() << '\n';
                        file.flush();
                    }
                }

                fallbackWrite(line.str());
            }
            catch (...)
            {
            }
        }

        void terminateHandler() noexcept
        {
            writeRawCrashLine("std::terminate called.");
            if (previousTerminateHandler != nullptr)
            {
                previousTerminateHandler();
            }

            std::abort();
        }

        void installTerminateHandler(const std::filesystem::path& crashLogPath)
        {
            {
                std::lock_guard lock(processCrashLogMutex);
                processCrashLogPath = crashLogPath;
            }

            std::call_once(terminateHandlerOnce, []()
            {
                previousTerminateHandler = std::set_terminate(terminateHandler);
            });
        }

#ifdef _WIN32
        LPTOP_LEVEL_EXCEPTION_FILTER previousUnhandledExceptionFilter = nullptr;
        std::once_flag unhandledExceptionFilterOnce;

        LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* exceptionPointers) noexcept
        {
            DWORD code = 0;
            void* address = nullptr;
            if (exceptionPointers != nullptr && exceptionPointers->ExceptionRecord != nullptr)
            {
                code = exceptionPointers->ExceptionRecord->ExceptionCode;
                address = exceptionPointers->ExceptionRecord->ExceptionAddress;
            }

            std::ostringstream message;
            message << "Unhandled native exception. code=0x"
                    << std::hex << std::uppercase << code
                    << ", address=" << address;
            writeRawCrashLine(message.str());

            if (previousUnhandledExceptionFilter != nullptr)
            {
                return previousUnhandledExceptionFilter(exceptionPointers);
            }

            return EXCEPTION_CONTINUE_SEARCH;
        }

        void installUnhandledExceptionFilter(const std::filesystem::path& crashLogPath)
        {
            {
                std::lock_guard lock(processCrashLogMutex);
                processCrashLogPath = crashLogPath;
            }

            std::call_once(unhandledExceptionFilterOnce, []()
            {
                previousUnhandledExceptionFilter = SetUnhandledExceptionFilter(unhandledExceptionFilter);
            });
        }
#endif
    }

    void Logger::initialize()
    {
        std::lock_guard lock(mutex_);
        if (initialized_)
        {
            return;
        }

        debugEnabled_ = isDebugLoggingEnabled();
        const std::filesystem::path directory = chooseLogDirectory();
        coreLogPath_ = logPathFor(directory, L"fluxora-core");
        bridgeLogPath_ = logPathFor(directory, L"fluxora-bridge");
        operationsLogPath_ = logPathFor(directory, L"fluxora-operations");
        crashLogPath_ = logPathFor(directory, L"fluxora-crash");

        try
        {
            coreLogger_ = createLogger("fluxora-core", coreLogPath_, debugEnabled_);
            bridgeLogger_ = createLogger("fluxora-bridge", bridgeLogPath_, debugEnabled_);
            operationsLogger_ = createLogger("fluxora-operations", operationsLogPath_, debugEnabled_);
            crashLogger_ = createLogger("fluxora-crash", crashLogPath_, true);
            installTerminateHandler(crashLogPath_);
#ifdef _WIN32
            installUnhandledExceptionFilter(crashLogPath_);
#endif
        }
        catch (const std::exception& exception)
        {
            fallbackWrite(std::string("Failed to initialize native spdlog loggers: ") + exception.what());
            coreLogger_.reset();
            bridgeLogger_.reset();
            operationsLogger_.reset();
            crashLogger_.reset();
        }

        initialized_ = true;

        std::ostringstream stream;
        stream << "Native logger initialized";
        if (!coreLogPath_.empty())
        {
            stream << ". corePath=" << pathForLog(coreLogPath_);
        }
        if (!bridgeLogPath_.empty())
        {
            stream << ", bridgePath=" << pathForLog(bridgeLogPath_);
        }
        if (!operationsLogPath_.empty())
        {
            stream << ", operationsPath=" << pathForLog(operationsLogPath_);
        }
        if (!crashLogPath_.empty())
        {
            stream << ", crashPath=" << pathForLog(crashLogPath_);
        }
        stream << ". debug=" << (debugEnabled_ ? "enabled" : "disabled");

        const std::string line = formatLine(LogChannel::Core, LogLevel::Info, "Logging", stream.str());
        if (coreLogger_)
        {
            coreLogger_->info("{}", line);
            coreLogger_->flush();
        }
        else
        {
            fallbackWrite(line);
        }
    }

    void Logger::shutdown()
    {
        std::lock_guard lock(mutex_);
        if (!initialized_)
        {
            return;
        }

        const std::string line = formatLine(LogChannel::Core, LogLevel::Info, "Logging", "Native logger shut down.");
        if (coreLogger_)
        {
            coreLogger_->info("{}", line);
            coreLogger_->flush();
        }
        else
        {
            fallbackWrite(line);
        }

        initialized_ = false;
        dropLogger(coreLogger_);
        dropLogger(bridgeLogger_);
        dropLogger(operationsLogger_);
        dropLogger(crashLogger_);
        coreLogger_.reset();
        bridgeLogger_.reset();
        operationsLogger_.reset();
        crashLogger_.reset();
        spdlog::shutdown();
    }

    void Logger::write(LogLevel level, std::string_view message) const
    {
        write(LogChannel::Core, level, "Core", message);
    }

    void Logger::write(LogLevel level, std::string_view category, std::string_view message) const
    {
        write(LogChannel::Core, level, category, message);
    }

    void Logger::write(
        LogChannel channel,
        LogLevel level,
        std::string_view category,
        std::string_view message) const
    {
        if (level == LogLevel::Debug && !debugEnabled_)
        {
            return;
        }

        const std::string line = formatLine(channel, level, category, message);

        std::shared_ptr<spdlog::logger> logger;
        {
            std::lock_guard lock(mutex_);
            switch (channel)
            {
            case LogChannel::Core:
                logger = coreLogger_;
                break;
            case LogChannel::Bridge:
                logger = bridgeLogger_;
                break;
            case LogChannel::Operations:
                logger = operationsLogger_;
                break;
            case LogChannel::Crash:
                logger = crashLogger_;
                break;
            }
        }

        if (logger)
        {
            logger->log(toSpdLevel(level), "{}", line);
            if (level == LogLevel::Warning || level == LogLevel::Error || channel == LogChannel::Crash)
            {
                logger->flush();
            }
            return;
        }

        fallbackWrite(line);
    }

    void Logger::writeOperation(LogLevel level, std::string_view category, std::string_view message) const
    {
        write(LogChannel::Operations, level, category, message);
    }

    void Logger::writeCrash(LogLevel level, std::string_view category, std::string_view message) const
    {
        write(LogChannel::Crash, level, category, message);
    }

    void Logger::setOperationId(std::wstring_view operationId)
    {
        currentOperationId = toUtf8(operationId);
    }

    void Logger::clearOperationId()
    {
        currentOperationId.clear();
    }

    std::string Logger::operationId()
    {
        return currentOperationId;
    }

    bool Logger::isInitialized() const noexcept
    {
        return initialized_;
    }

    const std::filesystem::path& Logger::logPath() const noexcept
    {
        return coreLogPath_;
    }

    const std::filesystem::path& Logger::bridgeLogPath() const noexcept
    {
        return bridgeLogPath_;
    }

    const std::filesystem::path& Logger::operationsLogPath() const noexcept
    {
        return operationsLogPath_;
    }

    const std::filesystem::path& Logger::crashLogPath() const noexcept
    {
        return crashLogPath_;
    }

    std::filesystem::path Logger::logDirectory() const
    {
        return coreLogPath_.empty() ? std::filesystem::path{} : coreLogPath_.parent_path();
    }
}
