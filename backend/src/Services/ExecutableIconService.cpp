#include "FluxoraCore/Services/ExecutableIconService.hpp"

#include "FluxoraCore/Services/Logger.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shlobj.h>
#endif

namespace fluxora
{
    namespace
    {
        std::wstring toLower(std::wstring value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
            return value;
        }

        bool hasExecutableExtension(const std::filesystem::path& path)
        {
            return toLower(path.extension().wstring()) == L".exe";
        }

        std::uint64_t fnv1a(const std::wstring& value)
        {
            std::uint64_t hash = 14695981039346656037ull;
            for (wchar_t character : value)
            {
                const std::uint32_t code = static_cast<std::uint32_t>(character);
                hash ^= code & 0xFFu;
                hash *= 1099511628211ull;
                hash ^= (code >> 8) & 0xFFu;
                hash *= 1099511628211ull;
                hash ^= (code >> 16) & 0xFFu;
                hash *= 1099511628211ull;
                hash ^= (code >> 24) & 0xFFu;
                hash *= 1099511628211ull;
            }

            return hash;
        }

        std::wstring iconFingerprint(const std::filesystem::path& executablePath)
        {
            std::error_code error;
            const auto absolutePath = std::filesystem::absolute(executablePath, error);
            const std::filesystem::path stablePath = error ? executablePath : absolutePath;

            std::wostringstream stream;
            stream << toLower(stablePath.wstring());

            const auto size = std::filesystem::file_size(stablePath, error);
            if (!error)
            {
                stream << L'|' << size;
            }

            const auto writeTime = std::filesystem::last_write_time(stablePath, error);
            if (!error)
            {
                stream << L'|' << writeTime.time_since_epoch().count();
            }

            std::wostringstream hashText;
            hashText << std::hex << fnv1a(stream.str());
            return hashText.str();
        }

        bool hasEmbeddedIcon(const std::filesystem::path& executablePath)
        {
#ifdef _WIN32
            HICON largeIcon = nullptr;
            HICON smallIcon = nullptr;
            const UINT count = ExtractIconExW(executablePath.c_str(), 0, &largeIcon, &smallIcon, 1);
            if (largeIcon != nullptr)
            {
                DestroyIcon(largeIcon);
            }
            if (smallIcon != nullptr)
            {
                DestroyIcon(smallIcon);
            }

            return count > 0;
#else
            (void)executablePath;
            return false;
#endif
        }

#ifdef _WIN32
        bool getPngEncoderClsid(CLSID& clsid)
        {
            UINT encoderCount = 0;
            UINT encoderBytes = 0;
            if (Gdiplus::GetImageEncodersSize(&encoderCount, &encoderBytes) != Gdiplus::Ok ||
                encoderCount == 0 ||
                encoderBytes == 0)
            {
                return false;
            }

            std::vector<std::byte> buffer(encoderBytes);
            auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
            if (Gdiplus::GetImageEncoders(encoderCount, encoderBytes, encoders) != Gdiplus::Ok)
            {
                return false;
            }

            for (UINT index = 0; index < encoderCount; ++index)
            {
                if (std::wcscmp(encoders[index].MimeType, L"image/png") == 0)
                {
                    clsid = encoders[index].Clsid;
                    return true;
                }
            }

            return false;
        }

        bool saveIconAsPng(HICON icon, const std::filesystem::path& outputPath)
        {
            if (icon == nullptr)
            {
                return false;
            }

            CLSID pngClsid{};
            if (!getPngEncoderClsid(pngClsid))
            {
                return false;
            }

            Gdiplus::Bitmap bitmap(icon);
            return bitmap.GetLastStatus() == Gdiplus::Ok &&
                bitmap.Save(outputPath.c_str(), &pngClsid, nullptr) == Gdiplus::Ok;
        }

        bool extractIconAsPng(const std::filesystem::path& executablePath, const std::filesystem::path& outputPath)
        {
            HICON largeIcon = nullptr;
            HICON smallIcon = nullptr;

            HRESULT result = SHDefExtractIconW(
                executablePath.c_str(),
                0,
                0,
                &largeIcon,
                &smallIcon,
                MAKELONG(256, 32));

            if (FAILED(result) || largeIcon == nullptr)
            {
                if (largeIcon != nullptr)
                {
                    DestroyIcon(largeIcon);
                    largeIcon = nullptr;
                }
                if (smallIcon != nullptr)
                {
                    DestroyIcon(smallIcon);
                    smallIcon = nullptr;
                }

                (void)ExtractIconExW(executablePath.c_str(), 0, &largeIcon, &smallIcon, 1);
            }

            HICON icon = largeIcon != nullptr ? largeIcon : smallIcon;
            const bool saved = saveIconAsPng(icon, outputPath);

            if (largeIcon != nullptr)
            {
                DestroyIcon(largeIcon);
            }
            if (smallIcon != nullptr)
            {
                DestroyIcon(smallIcon);
            }

            return saved;
        }

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
#endif
    }

    ExecutableIconService::ExecutableIconService(Logger& logger) noexcept
        : logger_(logger)
    {
    }

    void ExecutableIconService::initialize()
    {
        if (initialized_)
        {
            return;
        }

        cacheDirectory_ = resolveCacheDirectory();
        std::error_code error;
        std::filesystem::create_directories(cacheDirectory_, error);

#ifdef _WIN32
        Gdiplus::GdiplusStartupInput startupInput;
        ULONG_PTR token = 0;
        if (Gdiplus::GdiplusStartup(&token, &startupInput, nullptr) == Gdiplus::Ok)
        {
            gdiplusToken_ = static_cast<std::uintptr_t>(token);
        }
        else
        {
            logger_.write(LogLevel::Warning, "ExecutableIcons", "GDI+ startup failed. Executable icons will use placeholders.");
        }
#endif

        initialized_ = true;
        logger_.write(LogLevel::Info, "Executable icon service initialized.");
    }

    void ExecutableIconService::shutdown()
    {
        if (!initialized_)
        {
            return;
        }

#ifdef _WIN32
        if (gdiplusToken_ != 0)
        {
            Gdiplus::GdiplusShutdown(static_cast<ULONG_PTR>(gdiplusToken_));
            gdiplusToken_ = 0;
        }
#endif

        initialized_ = false;
        logger_.write(LogLevel::Info, "Executable icon service shut down.");
    }

    std::filesystem::path ExecutableIconService::resolveIconPath(
        const std::filesystem::path& executablePath) const
    {
        if (executablePath.empty() || !hasExecutableExtension(executablePath))
        {
            return {};
        }

        std::error_code error;
        const std::filesystem::path absolutePath = std::filesystem::absolute(executablePath, error);
        const std::filesystem::path stablePath = error ? executablePath : absolutePath;
        if (!std::filesystem::exists(stablePath, error) ||
            !std::filesystem::is_regular_file(stablePath, error) ||
            !hasEmbeddedIcon(stablePath))
        {
            return {};
        }

        const std::filesystem::path cacheFile = cacheDirectory_ / (iconFingerprint(stablePath) + L".png");
        if (std::filesystem::exists(cacheFile, error) && std::filesystem::is_regular_file(cacheFile, error))
        {
            return std::filesystem::absolute(cacheFile, error);
        }

        std::lock_guard lock(cacheMutex_);
        if (std::filesystem::exists(cacheFile, error) && std::filesystem::is_regular_file(cacheFile, error))
        {
            return std::filesystem::absolute(cacheFile, error);
        }

        std::filesystem::create_directories(cacheDirectory_, error);
        if (error)
        {
            return {};
        }

#ifdef _WIN32
        if (gdiplusToken_ == 0 || !extractIconAsPng(stablePath, cacheFile))
        {
            return {};
        }

        return std::filesystem::absolute(cacheFile, error);
#else
        (void)cacheFile;
        return {};
#endif
    }

    bool ExecutableIconService::isInitialized() const noexcept
    {
        return initialized_;
    }

    std::filesystem::path ExecutableIconService::resolveCacheDirectory() const
    {
#ifdef _WIN32
        if (const std::wstring appData = readEnvironmentVariable(L"APPDATA"); !appData.empty())
        {
            return std::filesystem::path(appData) / L"Fluxora" / L"cache" / L"executable-icons";
        }
#endif

        std::error_code error;
        const std::filesystem::path temp = std::filesystem::temp_directory_path(error);
        return (error ? std::filesystem::current_path() : temp) / L"Fluxora" / L"cache" / L"executable-icons";
    }
}
