#pragma once

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>
#endif

namespace fluxora::tests
{
#ifdef _WIN32
    inline bool createDirectoryJunction(
        const std::filesystem::path& target,
        const std::filesystem::path& junction,
        std::error_code& error)
    {
        error.clear();
        std::filesystem::create_directory(junction, error);
        if (error)
        {
            return false;
        }

        const std::wstring targetPath = std::filesystem::absolute(target).wstring();
        const std::wstring substituteName = LR"(\??\)" + targetPath;
        const std::wstring printName = targetPath;
        const auto substituteBytes = static_cast<USHORT>(substituteName.size() * sizeof(wchar_t));
        const auto printBytes = static_cast<USHORT>(printName.size() * sizeof(wchar_t));
        const USHORT printOffset = static_cast<USHORT>(substituteBytes + sizeof(wchar_t));
        const DWORD pathBufferBytes =
            substituteBytes + sizeof(wchar_t) + printBytes + sizeof(wchar_t);

        struct MountPointReparseData
        {
            ULONG reparseTag;
            USHORT reparseDataLength;
            USHORT reserved;
            USHORT substituteNameOffset;
            USHORT substituteNameLength;
            USHORT printNameOffset;
            USHORT printNameLength;
            wchar_t pathBuffer[1];
        };

        const DWORD bufferBytes =
            static_cast<DWORD>(offsetof(MountPointReparseData, pathBuffer)) + pathBufferBytes;
        std::vector<char> buffer(bufferBytes, 0);
        auto* data = reinterpret_cast<MountPointReparseData*>(buffer.data());
        data->reparseTag = IO_REPARSE_TAG_MOUNT_POINT;
        data->reparseDataLength = static_cast<USHORT>(8 + pathBufferBytes);
        data->substituteNameOffset = 0;
        data->substituteNameLength = substituteBytes;
        data->printNameOffset = printOffset;
        data->printNameLength = printBytes;

        std::memcpy(data->pathBuffer, substituteName.data(), substituteBytes);
        auto* printBuffer = reinterpret_cast<wchar_t*>(
            reinterpret_cast<char*>(data->pathBuffer) + printOffset);
        std::memcpy(printBuffer, printName.data(), printBytes);

        const HANDLE handle = CreateFileW(
            junction.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
            nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
            std::filesystem::remove(junction);
            return false;
        }

        DWORD returned = 0;
        const BOOL ok = DeviceIoControl(
            handle,
            FSCTL_SET_REPARSE_POINT,
            buffer.data(),
            bufferBytes,
            nullptr,
            0,
            &returned,
            nullptr);
        const DWORD lastError = ok ? ERROR_SUCCESS : GetLastError();
        CloseHandle(handle);
        if (!ok)
        {
            error = std::error_code(static_cast<int>(lastError), std::system_category());
            std::filesystem::remove(junction);
            return false;
        }

        return true;
    }
#endif

    class TempDirectory final
    {
    public:
        TempDirectory()
            : path_(makePath())
        {
            std::filesystem::create_directories(path_);
        }

        TempDirectory(const TempDirectory&) = delete;
        TempDirectory& operator=(const TempDirectory&) = delete;

        ~TempDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

        [[nodiscard]] const std::filesystem::path& path() const noexcept
        {
            return path_;
        }

    private:
        static std::filesystem::path makePath()
        {
            const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
            const auto threadHash = std::hash<std::thread::id>{}(std::this_thread::get_id());
            return std::filesystem::temp_directory_path() /
                ("fluxora-core-tests-" + std::to_string(now) + "-" + std::to_string(threadHash));
        }

        std::filesystem::path path_;
    };

    inline void writeTextFile(const std::filesystem::path& path, const std::string& content)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!file)
        {
            throw std::runtime_error("Failed to write test file.");
        }

        file.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    inline std::string readTextFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::in | std::ios::binary);
        if (!file)
        {
            throw std::runtime_error("Failed to read test file.");
        }

        return std::string(
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>());
    }

    inline std::filesystem::path normalized(std::filesystem::path path)
    {
        return std::filesystem::absolute(std::move(path)).lexically_normal();
    }

    class ScopedEnvironmentVariable final
    {
    public:
        ScopedEnvironmentVariable(std::wstring name, std::wstring value)
            : name_(std::move(name)),
              previous_(read(name_))
        {
            set(name_, value);
        }

        ScopedEnvironmentVariable(const ScopedEnvironmentVariable&) = delete;
        ScopedEnvironmentVariable& operator=(const ScopedEnvironmentVariable&) = delete;

        ~ScopedEnvironmentVariable()
        {
            if (previous_.has_value())
            {
                set(name_, previous_.value());
            }
            else
            {
                unset(name_);
            }
        }

    private:
        static std::optional<std::wstring> read(const std::wstring& name)
        {
#ifdef _WIN32
            const DWORD requiredLength = GetEnvironmentVariableW(name.c_str(), nullptr, 0);
            if (requiredLength == 0)
            {
                return std::nullopt;
            }

            std::wstring value(requiredLength, L'\0');
            const DWORD actualLength = GetEnvironmentVariableW(name.c_str(), value.data(), requiredLength);
            if (actualLength == 0 || actualLength >= requiredLength)
            {
                return std::nullopt;
            }

            value.resize(actualLength);
            return value;
#else
            std::string narrowName(name.begin(), name.end());
            const char* value = std::getenv(narrowName.c_str());
            if (value == nullptr)
            {
                return std::nullopt;
            }

            std::string narrowValue(value);
            return std::wstring(narrowValue.begin(), narrowValue.end());
#endif
        }

        static void set(const std::wstring& name, const std::wstring& value)
        {
#ifdef _WIN32
            SetEnvironmentVariableW(name.c_str(), value.c_str());
#else
            const std::string narrowName(name.begin(), name.end());
            const std::string narrowValue(value.begin(), value.end());
            setenv(narrowName.c_str(), narrowValue.c_str(), 1);
#endif
        }

        static void unset(const std::wstring& name)
        {
#ifdef _WIN32
            SetEnvironmentVariableW(name.c_str(), nullptr);
#else
            const std::string narrowName(name.begin(), name.end());
            unsetenv(narrowName.c_str());
#endif
        }

        std::wstring name_;
        std::optional<std::wstring> previous_;
    };
}
