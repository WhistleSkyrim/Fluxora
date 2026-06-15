#include "FluxoraCore/Services/VirtualFileSystemService.hpp"

#include "FluxoraCore/Services/BuildPathSettingsService.hpp"
#include "FluxoraCore/Services/Logger.hpp"
#include "FluxoraCore/Services/ProfileOrderService.hpp"
#include "FluxoraCore/Support/JsonWriter.hpp"

#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef FLUXORA_ENABLE_VFS
#include "FluxoraVfs/VfsProtocol.hpp"
#include <detours.h>
#endif

namespace fluxora
{
    namespace
    {
#ifdef _WIN32
        std::string toUtf8(const std::wstring& value)
        {
            if (value.empty())
            {
                return {};
            }

            const int size = WideCharToMultiByte(
                CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            std::string out(static_cast<std::size_t>(size), '\0');
            WideCharToMultiByte(
                CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), size, nullptr, nullptr);
            return out;
        }

        std::string toAnsi(const std::wstring& value)
        {
            if (value.empty())
            {
                return {};
            }

            const int size = WideCharToMultiByte(
                CP_ACP, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            std::string out(static_cast<std::size_t>(size), '\0');
            WideCharToMultiByte(
                CP_ACP, 0, value.data(), static_cast<int>(value.size()), out.data(), size, nullptr, nullptr);
            return out;
        }
#endif

        void writeTextFile(const std::filesystem::path& path, const std::string& content)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }

            std::ofstream file(path, std::ios::out | std::ios::trunc | std::ios::binary);
            if (!file)
            {
                throw std::runtime_error("Failed to write the virtual file system descriptor.");
            }

            file.write(content.data(), static_cast<std::streamsize>(content.size()));
        }

        // The FluxoraVfs.dll hook ships next to FluxoraCore.dll, so it is located
        // relative to this very module rather than the (unknown) game folder.
        std::filesystem::path hookDllPath()
        {
#ifdef _WIN32
            HMODULE module = nullptr;
            if (!GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&writeTextFile),
                    &module))
            {
                return {};
            }

            wchar_t buffer[MAX_PATH * 2];
            const DWORD length = GetModuleFileNameW(module, buffer, static_cast<DWORD>(std::size(buffer)));
            if (length == 0 || length >= std::size(buffer))
            {
                return {};
            }

            return std::filesystem::path(std::wstring(buffer, length)).parent_path() / L"FluxoraVfs.dll";
#else
            return {};
#endif
        }

        std::vector<std::filesystem::path> collectEnabledMods(
            const ProfileOrderService& profileOrder,
            const std::filesystem::path& projectDirectory,
            std::wstring_view profileName)
        {
            std::vector<std::filesystem::path> mods;
            for (const ProfileModOrderItem& item : profileOrder.listModOrder(projectDirectory, profileName))
            {
                const bool isFullyOverwritten =
                    item.fileCount > 0 &&
                    item.overwrittenFileCount >= item.fileCount;
                if (item.kind == L"mod" && item.isEnabled && !item.id.empty() && !isFullyOverwritten)
                {
                    mods.push_back(item.id);
                }
            }

            return mods;
        }

#ifdef FLUXORA_ENABLE_VFS
        std::wstring buildDescriptor(
            const std::filesystem::path& target,
            const std::filesystem::path& overwrite,
            const std::filesystem::path& logPath,
            const std::filesystem::path& hookDll,
            const std::vector<std::filesystem::path>& mods)
        {
            JsonWriter writer;
            writer.beginObject();
            writer.field(vfs::protocol::fields::schemaVersion, vfs::protocol::schemaVersion);
            writer.field(vfs::protocol::fields::target, target.wstring());
            writer.field(vfs::protocol::fields::overwrite, overwrite.wstring());
            writer.field(vfs::protocol::fields::logPath, logPath.wstring());
            writer.field(vfs::protocol::fields::hookDll, hookDll.wstring());
            writer.key(vfs::protocol::fields::mods).beginArray();
            for (const std::filesystem::path& mod : mods)
            {
                writer.value(mod.wstring());
            }
            writer.endArray();
            writer.endObject();
            return writer.str();
        }
#endif
    }

    VirtualFileSystemService::VirtualFileSystemService(
        Logger& logger,
        ExecutableService& executables,
        ProfileOrderService& profileOrder,
        const BuildPathSettingsService& pathSettings) noexcept
        : logger_(logger),
          executables_(executables),
          profileOrder_(profileOrder),
          pathSettings_(pathSettings)
    {
    }

    void VirtualFileSystemService::initialize()
    {
        if (initialized_)
        {
            return;
        }

        initialized_ = true;
        logger_.write(LogLevel::Info, "Virtual file system service initialized.");
    }

    void VirtualFileSystemService::shutdown()
    {
        if (!initialized_)
        {
            return;
        }

        initialized_ = false;
        logger_.write(LogLevel::Info, "Virtual file system service shut down.");
    }

    GameExecutableLaunchResult VirtualFileSystemService::launchExecutable(
        const std::filesystem::path& configPath,
        std::wstring_view executableId) const
    {
        const ResolvedExecutableLaunch resolved = executables_.resolveExecutable(configPath, executableId);

#if !defined(FLUXORA_ENABLE_VFS) || !defined(_WIN32)
        // Built without the virtual file system: behave exactly like a plain run.
        return executables_.launchProjectExecutable(configPath, executableId);
#else
        const auto fallbackPlainLaunch = [&](const char* reason) -> GameExecutableLaunchResult
        {
            logger_.write(LogLevel::Warning, std::string("Launching without the virtual file system: ") + reason);
            return executables_.launchProjectExecutable(configPath, executableId);
        };

        if (resolved.gamePath.empty() || !std::filesystem::exists(resolved.gamePath))
        {
            return fallbackPlainLaunch("the build has no valid game path.");
        }

        const std::wstring dataDirectory = resolved.dataDirectory.empty() ? L"Data" : resolved.dataDirectory;
        const std::filesystem::path target = resolved.gamePath / dataDirectory;

        std::wstring profile = resolved.defaultProfile.empty() ? L"Default" : resolved.defaultProfile;
        const std::vector<std::filesystem::path> mods =
            collectEnabledMods(profileOrder_, resolved.projectDirectory, profile);
        if (mods.empty())
        {
            return fallbackPlainLaunch("no enabled mods to virtualize.");
        }

        const std::filesystem::path hookDll = hookDllPath();
        if (hookDll.empty() || !std::filesystem::exists(hookDll))
        {
            return fallbackPlainLaunch("FluxoraVfs.dll was not found next to FluxoraCore.dll.");
        }

        const std::filesystem::path overwrite = pathSettings_.overwriteDirectory(resolved.projectDirectory);
        const std::filesystem::path vfsDirectory = resolved.projectDirectory / L".flow" / L"vfs";
        const std::filesystem::path descriptorPath = vfsDirectory / L"vfs-config.json";
        const std::filesystem::path logPath = vfsDirectory / L"vfs.log";

        std::error_code error;
        std::filesystem::create_directories(overwrite, error);
        std::filesystem::create_directories(vfsDirectory, error);

        writeTextFile(
            descriptorPath,
            toUtf8(buildDescriptor(target, overwrite, logPath, hookDll, mods)));

        // Children inherit this, so the whole process tree shares one virtual view.
        SetEnvironmentVariableW(vfs::protocol::configEnvironmentVariable, descriptorPath.c_str());

        std::vector<wchar_t> commandLineBuffer(resolved.commandLine.begin(), resolved.commandLine.end());
        commandLineBuffer.push_back(L'\0');
        const std::string hookDllAnsi = toAnsi(hookDll.wstring());

        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInformation{};

        const BOOL started = DetourCreateProcessWithDllExW(
            resolved.resolvedExecutablePath.c_str(),
            commandLineBuffer.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_DEFAULT_ERROR_MODE,
            nullptr,
            resolved.resolvedWorkingDirectory.c_str(),
            &startupInfo,
            &processInformation,
            hookDllAnsi.c_str(),
            nullptr);

        if (!started)
        {
            return fallbackPlainLaunch("the game could not be started with the hook injected.");
        }

        CloseHandle(processInformation.hThread);
        CloseHandle(processInformation.hProcess);

        logger_.write(
            LogLevel::Info,
            "Game launched through the virtual file system (" + std::to_string(mods.size()) + " active mods).");

        return GameExecutableLaunchResult{
            resolved.executable,
            resolved.resolvedExecutablePath,
            resolved.resolvedWorkingDirectory
        };
#endif
    }

    bool VirtualFileSystemService::isInitialized() const noexcept
    {
        return initialized_;
    }
}
