#include <windows.h>
#include <detours.h>

#include "FluxoraVfs/VfsConfig.hpp"
#include "FluxoraVfs/VfsHooks.hpp"
#include "FluxoraVfs/VfsLog.hpp"

using namespace fluxora::vfs;

namespace
{
    std::wstring currentProcessImage()
    {
        wchar_t buffer[MAX_PATH * 2];
        const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
        return std::wstring(buffer, length);
    }
}

// FluxoraVfs is injected into the game (and every child process) by the manager
// through DetourCreateProcessWithDllEx. On attach it reads the descriptor named
// by FLUXORA_VFS_CONFIG, builds the merged virtual data directory and installs
// the file-system hooks. Nothing is copied: the mods stay in place and are made
// to appear inside the game folder for the lifetime of the process.
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID /*reserved*/)
{
    // Detours spins up a tiny helper process while injecting; it must return
    // immediately without doing any work of its own.
    if (DetourIsHelperProcess())
    {
        return TRUE;
    }

    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
    {
        DetourRestoreAfterWith();
        DisableThreadLibraryCalls(module);

        try
        {
            VfsConfig config;
            if (loadVfsConfigFromEnvironment(config))
            {
                VfsLog::open(config.logPath);
                VfsLog::writef(L"FluxoraVfs attached to \"%s\".", currentProcessImage().c_str());
                if (!installHooks(config))
                {
                    VfsLog::write(L"FluxoraVfs did not install hooks (nothing to virtualize or hook error).");
                }
            }
        }
        catch (...)
        {
            // DllMain must never propagate an exception.
        }
        break;
    }

    case DLL_PROCESS_DETACH:
        try
        {
            uninstallHooks();
            VfsLog::write(L"FluxoraVfs detached.");
            VfsLog::close();
        }
        catch (...)
        {
        }
        break;

    default:
        break;
    }

    return TRUE;
}
