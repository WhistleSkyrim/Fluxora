#include "FluxoraVfs/VfsLog.hpp"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace fluxora::vfs
{
    namespace
    {
        std::mutex g_mutex;
        HANDLE g_handle = INVALID_HANDLE_VALUE;

        std::string toUtf8(const std::wstring& value)
        {
            if (value.empty())
            {
                return {};
            }

            const int size = WideCharToMultiByte(
                CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            if (size <= 0)
            {
                return {};
            }

            std::string out(static_cast<size_t>(size), '\0');
            WideCharToMultiByte(
                CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), size, nullptr, nullptr);
            return out;
        }

        std::wstring timestampPrefix()
        {
            SYSTEMTIME now{};
            GetLocalTime(&now);

            wchar_t buffer[64];
            const DWORD pid = GetCurrentProcessId();
            _snwprintf_s(
                buffer,
                _countof(buffer),
                _TRUNCATE,
                L"[%02u:%02u:%02u.%03u pid:%lu] ",
                now.wHour,
                now.wMinute,
                now.wSecond,
                now.wMilliseconds,
                static_cast<unsigned long>(pid));
            return buffer;
        }
    }

    void VfsLog::open(const std::wstring& path)
    {
        std::scoped_lock lock(g_mutex);
        if (g_handle != INVALID_HANDLE_VALUE || path.empty())
        {
            return;
        }

        g_handle = CreateFileW(
            path.c_str(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
    }

    void VfsLog::close()
    {
        std::scoped_lock lock(g_mutex);
        if (g_handle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(g_handle);
            g_handle = INVALID_HANDLE_VALUE;
        }
    }

    bool VfsLog::isOpen() noexcept
    {
        return g_handle != INVALID_HANDLE_VALUE;
    }

    void VfsLog::write(const std::wstring& message)
    {
        std::scoped_lock lock(g_mutex);
        if (g_handle == INVALID_HANDLE_VALUE)
        {
            return;
        }

        std::string line = toUtf8(timestampPrefix() + message);
        line.push_back('\r');
        line.push_back('\n');

        DWORD written = 0;
        WriteFile(g_handle, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    }

    void VfsLog::writef(const wchar_t* format, ...)
    {
        if (g_handle == INVALID_HANDLE_VALUE)
        {
            return;
        }

        wchar_t buffer[2048];
        va_list args;
        va_start(args, format);
        _vsnwprintf_s(buffer, _countof(buffer), _TRUNCATE, format, args);
        va_end(args);

        write(buffer);
    }
}
