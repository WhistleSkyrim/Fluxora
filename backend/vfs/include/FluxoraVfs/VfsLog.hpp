#pragma once

#include <string>

namespace fluxora::vfs
{
    // Minimal append-only diagnostic log for the injected process. Writing is done
    // with the raw Win32 file API (never the CRT) so it stays safe to call from
    // inside DllMain and from within the file-system hooks themselves.
    class VfsLog final
    {
    public:
        static void open(const std::wstring& path);
        static void close();

        static void write(const std::wstring& message);
        static void writef(const wchar_t* format, ...);

        [[nodiscard]] static bool isOpen() noexcept;
    };
}
