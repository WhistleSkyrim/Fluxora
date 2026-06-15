#include "FluxoraCore/Services/Logger.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <ctime>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fluxora
{
    namespace
    {
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

        bool isDebugLoggingEnabled()
        {
            const std::wstring value = readEnvironmentVariable(L"FLUXORA_DEBUG_LOGS");
            return value == L"1" ||
                value == L"true" ||
                value == L"TRUE" ||
                value == L"yes" ||
                value == L"YES";
        }

        std::string pathForLog(const std::filesystem::path& path)
        {
            const std::wstring wide = path.wstring();
            if (wide.empty())
            {
                return {};
            }

            const int requiredLength = WideCharToMultiByte(
                CP_UTF8,
                0,
                wide.c_str(),
                static_cast<int>(wide.size()),
                nullptr,
                0,
                nullptr,
                nullptr);
            if (requiredLength <= 0)
            {
                return path.string();
            }

            std::string value(static_cast<std::size_t>(requiredLength), '\0');
            WideCharToMultiByte(
                CP_UTF8,
                0,
                wide.c_str(),
                static_cast<int>(wide.size()),
                value.data(),
                requiredLength,
                nullptr,
                nullptr);
            return value;
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

        bool isDebugLoggingEnabled()
        {
            const char* value = std::getenv("FLUXORA_DEBUG_LOGS");
            return value != nullptr &&
                (std::string_view(value) == "1" ||
                 std::string_view(value) == "true" ||
                 std::string_view(value) == "yes");
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

        std::filesystem::path chooseLogPath()
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

            const std::filesystem::path fileName =
                std::wstring(L"fluxora-core-") + logDateStamp() + L".log";
            for (const std::filesystem::path& directory : directories)
            {
                std::error_code error;
                std::filesystem::create_directories(directory, error);
                if (error)
                {
                    continue;
                }

                const std::filesystem::path candidate = directory / fileName;
                std::ofstream file(candidate, std::ios::out | std::ios::app | std::ios::binary);
                if (file)
                {
                    return makeAbsolute(candidate);
                }
            }

            return {};
        }
    }

    void Logger::initialize()
    {
        if (initialized_)
        {
            return;
        }

        debugEnabled_ = isDebugLoggingEnabled();
        logPath_ = chooseLogPath();
        initialized_ = true;

        std::ostringstream stream;
        stream << "Native logger initialized";
        if (!logPath_.empty())
        {
            stream << ". path=" << pathForLog(logPath_);
        }
        stream << ". debug=" << (debugEnabled_ ? "enabled" : "disabled");
        write(LogLevel::Info, "Logging", stream.str());
    }

    void Logger::shutdown()
    {
        if (!initialized_)
        {
            return;
        }

        write(LogLevel::Info, "Logging", "Native logger shut down.");
        initialized_ = false;
    }

    void Logger::write(LogLevel level, std::string_view message) const
    {
        write(level, "Core", message);
    }

    void Logger::write(LogLevel level, std::string_view category, std::string_view message) const
    {
        if (level == LogLevel::Debug && !debugEnabled_)
        {
            return;
        }

        const std::string safeCategory = category.empty()
            ? std::string("Core")
            : std::string(category);

        std::ostringstream stream;
        stream << "[" << timestamp() << "] "
               << "[" << toLabel(level) << "] "
               << "[" << safeCategory << "] "
               << "[tid=" << std::this_thread::get_id() << "] "
               << message;
        const std::string line = stream.str();

        std::lock_guard lock(mutex_);

        if (!logPath_.empty())
        {
            std::ofstream file(logPath_, std::ios::out | std::ios::app | std::ios::binary);
            if (file)
            {
                file << line << '\n';
            }
        }

#ifdef _WIN32
        OutputDebugStringA((line + "\n").c_str());
#endif

        std::clog << line << '\n';
    }

    bool Logger::isInitialized() const noexcept
    {
        return initialized_;
    }

    const std::filesystem::path& Logger::logPath() const noexcept
    {
        return logPath_;
    }

    std::filesystem::path Logger::logDirectory() const
    {
        return logPath_.empty() ? std::filesystem::path{} : logPath_.parent_path();
    }
}
