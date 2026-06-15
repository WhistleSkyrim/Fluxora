cmake_minimum_required(VERSION 3.20)

project(FluxoraCore VERSION 0.1.0 LANGUAGES CXX)

# The user-mode virtual file system (the MO2/usvfs-style "mods appear in the game
# folder without being copied" feature) needs an inline-hooking engine. We use
# Microsoft Detours, the same class of library usvfs itself relies on. Turn this
# off (-DFLUXORA_ENABLE_VFS=OFF) to build the core without it; launching then
# behaves like a plain run.
option(FLUXORA_ENABLE_VFS "Build the user-mode virtual file system (requires Microsoft Detours)" ON)

# --------------------------------------------------------------------------
# FluxoraCore: the C++ core / business logic exposed to the C# UI over a C ABI.
# --------------------------------------------------------------------------
add_library(FluxoraCore SHARED
    src/FluxoraCoreApi.cpp
    src/Core.cpp
    src/Services/AppSettingsService.cpp
    src/Services/BuildPathSettingsService.cpp
    src/Services/BulkFileCopyService.cpp
    src/Services/HookService.cpp
    src/Services/DownloadService.cpp
    src/Services/ExecutableIconService.cpp
    src/Services/ExecutableService.cpp
    src/Services/Logger.cpp
    src/Services/ModService.cpp
    src/Services/ModOrganizerExecutableImportService.cpp
    src/Services/ModOrganizerImportService.cpp
    src/Services/ModOrganizerPluginGroupService.cpp
    src/Services/ModOrganizerProfileOrderService.cpp
    src/Services/NexusModsAuthService.cpp
    src/Services/PluginService.cpp
    src/Services/ProfileOrderService.cpp
    src/Services/ProjectService.cpp
    src/Services/TemplateService.cpp
    src/Services/VirtualFileSystemService.cpp
    src/Storage/InstanceMetadataStore.cpp
)

target_include_directories(FluxoraCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        # Shared manager<->hook contract (VfsProtocol.hpp).
        ${CMAKE_CURRENT_SOURCE_DIR}/vfs/include
)

target_compile_features(FluxoraCore PUBLIC cxx_std_20)
target_compile_definitions(FluxoraCore PRIVATE FLUXORA_CORE_EXPORTS)

if(MSVC)
    # /utf-8 makes MSVC read the UTF-8 sources correctly, so Cyrillic wide-string
    # literals (template display names, capability labels) become proper UTF-16
    # instead of being decoded through the system code page.
    target_compile_options(FluxoraCore PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(FluxoraCore PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    target_link_libraries(FluxoraCore PRIVATE advapi32 bcrypt crypt32 gdiplus rstrtmgr shell32 winhttp ws2_32)
endif()

# --------------------------------------------------------------------------
# Virtual file system: Microsoft Detours + the injected FluxoraVfs.dll hook.
# --------------------------------------------------------------------------
if(FLUXORA_ENABLE_VFS AND WIN32)
    # Obtain Detours. Prefer a local copy when offline (-DDETOURS_SOURCE_DIR=...),
    # otherwise fetch the pinned release.
    if(DEFINED DETOURS_SOURCE_DIR)
        set(detours_root "${DETOURS_SOURCE_DIR}")
    else()
        include(FetchContent)
        FetchContent_Declare(
            detours
            GIT_REPOSITORY https://github.com/microsoft/Detours.git
            GIT_TAG        4.0.1
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(detours)
        set(detours_root "${detours_SOURCE_DIR}")
    endif()

    set(detours_src "${detours_root}/src")
    if(NOT EXISTS "${detours_src}/detours.cpp")
        message(FATAL_ERROR
            "Detours sources were not found at '${detours_src}'. "
            "Build with -DFLUXORA_ENABLE_VFS=OFF to skip the virtual file system, "
            "or pass -DDETOURS_SOURCE_DIR=<path to Detours checkout>.")
    endif()

    set(detours_sources
        "${detours_src}/detours.cpp"
        "${detours_src}/modules.cpp"
        "${detours_src}/disasm.cpp"
        "${detours_src}/image.cpp"
        "${detours_src}/creatwth.cpp"
    )

    # Detours is third-party: silence its warnings and build one variant per CRT.
    # FluxoraCore uses the dynamic CRT (status quo); the injected DLL uses the
    # static CRT so it loads into any game without needing the VC++ redistributable.
    add_library(detours_md STATIC ${detours_sources})
    target_include_directories(detours_md PUBLIC "${detours_src}")
    set_property(TARGET detours_md PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    if(MSVC)
        target_compile_options(detours_md PRIVATE /w /Gy)
    endif()

    add_library(detours_mt STATIC ${detours_sources})
    target_include_directories(detours_mt PUBLIC "${detours_src}")
    set_property(TARGET detours_mt PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    if(MSVC)
        target_compile_options(detours_mt PRIVATE /w /Gy)
    endif()

    # The manager side needs Detours to start the game with the hook injected.
    target_compile_definitions(FluxoraCore PRIVATE FLUXORA_ENABLE_VFS)
    target_include_directories(FluxoraCore PRIVATE "${detours_src}")
    target_link_libraries(FluxoraCore PRIVATE detours_md)

    # The injected hook DLL.
    add_library(FluxoraVfs SHARED
        vfs/src/DllMain.cpp
        vfs/src/VfsConfig.cpp
        vfs/src/VfsHooks.cpp
        vfs/src/VfsLog.cpp
        vfs/src/VfsTree.cpp
        # Detours injects this payload by importing ordinal #1 from it, so the DLL
        # MUST export a symbol there or every injected process fails to start. The
        # .def re-exports Detours' DetourFinishHelperProcess at ordinal 1 (MSVC
        # uses a .def among the sources as the module-definition file).
        vfs/FluxoraVfs.def
    )
    target_include_directories(FluxoraVfs PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/vfs/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include   # reuse the header-only JSON reader
        "${detours_src}"
    )
    target_compile_features(FluxoraVfs PRIVATE cxx_std_20)
    set_property(TARGET FluxoraVfs PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    target_link_libraries(FluxoraVfs PRIVATE detours_mt)
    if(MSVC)
        target_compile_options(FluxoraVfs PRIVATE /W4 /permissive- /utf-8)
    endif()
endif()
