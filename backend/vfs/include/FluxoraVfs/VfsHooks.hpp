#pragma once

#include "FluxoraVfs/VfsConfig.hpp"

namespace fluxora::vfs
{
    // Builds the virtual tree from `config` and installs the native file-system
    // hooks (and the child-process propagation hooks) with Microsoft Detours.
    // Returns false if the tree is empty or the hooks could not be attached.
    bool installHooks(const VfsConfig& config);

    // Removes every hook. Safe to call even if installHooks failed.
    void uninstallHooks();
}
