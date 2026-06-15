#include "FluxoraCore/Services/VirtualFileSystemService.hpp"

#include "FluxoraCore/Services/BuildPathSettingsService.hpp"
#include "FluxoraCore/Services/Logger.hpp"
#include "FluxoraCore/Services/ProfileOrderService.hpp"
#include "FluxoraCore/Support/JsonWriter.hpp"

#include <fstream>
#include <algorithm>
#include <cwctype>
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

        std::string win32Message(DWORD value)
        {
            if (value == ERROR_SUCCESS)
            {
                return {};
            }

            LPWSTR raw = nullptr;
            const DWORD length = FormatMessageW(
                FORMAT_MESSAGE_ALLOCATE_BUFFER |
                    FORMAT_MESSAGE_FROM_SYSTEM |
                    FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr,
                value,
                0,
                reinterpret_cast<LPWSTR>(&raw),
                0,
                nullptr);
            if (length == 0 || raw == nullptr)
            {
                return {};
            }

            std::wstring message(raw, raw + length);
            LocalFree(raw);
            while (!message.empty() &&
                (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' '))
            {
                message.pop_back();
            }

            return toUtf8(message);
        }

        std::string describeWin32Error(DWORD value)
        {
            std::string description = std::to_string(value);
            if (const std::string message = win32Message(value); !message.empty())
            {
                description += " (" + message + ")";
            }

            return description;
        }
#endif

        struct VfsMountDescriptor
        {
            std::filesystem::path target;
            std::filesystem::path overwrite;
            std::vector<std::filesystem::path> mods;
            std::vector<std::wstring> excludedRootNames;
        };

        std::wstring toLower(std::wstring value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
            return value;
        }

        bool supportsRootBuilder(std::wstring_view templateId)
        {
            return toLower(std::wstring(templateId)).find(L"skyrim") != std::wstring::npos;
        }

        bool isDirectory(const std::filesystem::path& path)
        {
            std::error_code error;
            return std::filesystem::exists(path, error) && std::filesystem::is_directory(path, error);
        }

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

        std::vector<std::filesystem::path> collectRootBuilderMods(
            const std::vector<std::filesystem::path>& mods)
        {
            std::vector<std::filesystem::path> rootMods;
            rootMods.reserve(mods.size());
            for (const std::filesystem::path& mod : mods)
            {
                const std::filesystem::path root = mod / L"root";
                if (isDirectory(root))
                {
                    rootMods.push_back(root);
                }
            }

            return rootMods;
        }

        std::vector<std::filesystem::path> collectDataMountMods(
            const std::vector<std::filesystem::path>& mods,
            const std::wstring& dataDirectory,
            bool rootBuilderEnabled)
        {
            std::vector<std::filesystem::path> dataMods;
            dataMods.reserve(rootBuilderEnabled ? mods.size() * 2 : mods.size());
            for (const std::filesystem::path& mod : mods)
            {
                dataMods.push_back(mod);
                if (!rootBuilderEnabled)
                {
                    continue;
                }

                const std::filesystem::path rootData = mod / L"root" / dataDirectory;
                if (isDirectory(rootData))
                {
                    dataMods.push_back(rootData);
                }
            }

            return dataMods;
        }

#ifdef FLUXORA_ENABLE_VFS
        void writePathArray(JsonWriter& writer, const std::vector<std::filesystem::path>& paths)
        {
            writer.beginArray();
            for (const std::filesystem::path& path : paths)
            {
                writer.value(path.wstring());
            }
            writer.endArray();
        }

        void writeMount(JsonWriter& writer, const VfsMountDescriptor& mount)
        {
            writer.beginObject();
            writer.field(vfs::protocol::fields::target, mount.target.wstring());
            writer.field(vfs::protocol::fields::overwrite, mount.overwrite.wstring());
            writer.key(vfs::protocol::fields::mods);
            writePathArray(writer, mount.mods);
            writer.stringArray(vfs::protocol::fields::excludedRootNames, mount.excludedRootNames);
            writer.endObject();
        }

        std::wstring buildDescriptor(
            const std::filesystem::path& logPath,
            const std::filesystem::path& hookDll,
            const std::vector<VfsMountDescriptor>& mounts)
        {
            JsonWriter writer;
            writer.beginObject();
            writer.field(vfs::protocol::fields::schemaVersion, vfs::protocol::schemaVersion);
            writer.field(vfs::protocol::fields::logPath, logPath.wstring());
            writer.field(vfs::protocol::fields::hookDll, hookDll.wstring());

            if (!mounts.empty())
            {
                writer.field(vfs::protocol::fields::target, mounts.front().target.wstring());
                writer.field(vfs::protocol::fields::overwrite, mounts.front().overwrite.wstring());
                writer.key(vfs::protocol::fields::mods);
                writePathArray(writer, mounts.front().mods);
            }

            writer.key(vfs::protocol::fields::mounts).beginArray();
            for (const VfsMountDescriptor& mount : mounts)
            {
                writeMount(writer, mount);
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
        const auto fallbackPlainLaunch = [&](const std::string& reason) -> GameExecutableLaunchResult
        {
            logger_.write(LogLevel::Warning, "Launching without the virtual file system: " + reason);
            return executables_.launchProjectExecutable(configPath, executableId);
        };

        if (resolved.gamePath.empty() || !std::filesystem::exists(resolved.gamePath))
        {
            return fallbackPlainLaunch("the build has no valid game path.");
        }

        const std::wstring dataDirectory = resolved.dataDirectory.empty() ? L"Data" : resolved.dataDirectory;
        const std::filesystem::path dataTarget = resolved.gamePath / dataDirectory;

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
        const bool rootBuilderEnabled = supportsRootBuilder(resolved.templateId);
        const std::vector<std::filesystem::path> dataMods =
            collectDataMountMods(mods, dataDirectory, rootBuilderEnabled);
        std::vector<VfsMountDescriptor> mounts;
        mounts.push_back(VfsMountDescriptor{
            dataTarget,
            overwrite,
            dataMods,
            rootBuilderEnabled ? std::vector<std::wstring>{L"root"} : std::vector<std::wstring>{}
        });

        const std::vector<std::filesystem::path> rootMods = rootBuilderEnabled
            ? collectRootBuilderMods(mods)
            : std::vector<std::filesystem::path>{};
        const std::filesystem::path rootOverwrite = overwrite / L"root";
        if (rootBuilderEnabled && (!rootMods.empty() || isDirectory(rootOverwrite)))
        {
            mounts.push_back(VfsMountDescriptor{
                resolved.gamePath,
                rootOverwrite,
                rootMods,
                {dataDirectory}
            });
        }

        const std::filesystem::path launchCacheRoot = resolved.rootBuilderLaunchCacheDirectory;
        if (rootBuilderEnabled && !launchCacheRoot.empty())
        {
            const std::filesystem::path launchCacheDataTarget = launchCacheRoot / dataDirectory;
            std::vector<std::filesystem::path> launchCacheDataMods;
            if (isDirectory(dataTarget))
            {
                launchCacheDataMods.push_back(dataTarget);
            }
            launchCacheDataMods.insert(
                launchCacheDataMods.end(),
                dataMods.begin(),
                dataMods.end());

            if (!launchCacheDataMods.empty() || isDirectory(overwrite))
            {
                mounts.push_back(VfsMountDescriptor{
                    launchCacheDataTarget,
                    overwrite,
                    launchCacheDataMods,
                    std::vector<std::wstring>{L"root"}
                });
            }

            std::vector<std::filesystem::path> launchCacheRootMods;
            if (isDirectory(resolved.gamePath))
            {
                launchCacheRootMods.push_back(resolved.gamePath);
            }
            launchCacheRootMods.insert(
                launchCacheRootMods.end(),
                rootMods.begin(),
                rootMods.end());

            if (!launchCacheRootMods.empty() || isDirectory(rootOverwrite))
            {
                mounts.push_back(VfsMountDescriptor{
                    launchCacheRoot,
                    rootOverwrite,
                    launchCacheRootMods,
                    std::vector<std::wstring>{dataDirectory}
                });
            }
        }

        const std::filesystem::path vfsDirectory = resolved.projectDirectory / L".flow" / L"vfs";
        const std::filesystem::path descriptorPath = vfsDirectory / L"vfs-config.json";
        const std::filesystem::path logPath = vfsDirectory / L"vfs.log";

        std::error_code error;
        std::filesystem::create_directories(overwrite, error);
        if (mounts.size() > 1)
        {
            std::filesystem::create_directories(rootOverwrite, error);
        }
        std::filesystem::create_directories(vfsDirectory, error);

        writeTextFile(
            descriptorPath,
            toUtf8(buildDescriptor(logPath, hookDll, mounts)));

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
            const DWORD launchError = GetLastError();
            return fallbackPlainLaunch(
                ("the game could not be started with the hook injected. Win32 error: " +
                 describeWin32Error(launchError) + "."));
        }

        CloseHandle(processInformation.hThread);
        CloseHandle(processInformation.hProcess);

        logger_.write(
            LogLevel::Info,
            "Game launched through the virtual file system (" + std::to_string(mods.size()) +
                " active mods, " + std::to_string(mounts.size()) + " mounts).");

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
