include(FetchContent)

find_package(GTest CONFIG QUIET)
if(NOT TARGET GTest::gtest_main)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(googletest)
endif()

add_executable(FluxoraCoreTests
    AppSettingsServiceTests.cpp
    AtomicFileStoreTests.cpp
    BuildPathSettingsServiceTests.cpp
    ContentLayoutServiceTests.cpp
    DownloadServiceTests.cpp
    ExecutableServiceTests.cpp
    GameDetectionHealthServiceTests.cpp
    GameDefinitionLoaderTests.cpp
    GameSupportRegistryTests.cpp
    GameTypesTests.cpp
    HookServiceTests.cpp
    InstanceMetadataStoreTests.cpp
    JsonSupportTests.cpp
    FomodInstallerServiceTests.cpp
    FluxPackServiceTests.cpp
    LoggerTests.cpp
    ModFileOperationsIntegrationTests.cpp
    ModOrganizerImportServiceTests.cpp
    ModOrganizerPluginGroupServiceTests.cpp
    ModOrganizerProfileOrderServiceTests.cpp
    NexusModsAuthServiceTests.cpp
    PathSafetyServiceTests.cpp
    PluginServiceTests.cpp
    ProjectServiceTests.cpp
    ProjectStateTransactionTests.cpp
    TemplateServiceTests.cpp
    TestFilesystem.hpp
    ../src/Services/AppSettingsService.cpp
    ../src/Services/BulkFileCopyService.cpp
    ../src/Services/BuildPathSettingsService.cpp
    ../src/Services/ContentLayoutService.cpp
    ../src/GameSupport/DefinitionBackedGameSupport.cpp
    ../src/GameSupport/GameDefinitionLoader.cpp
    ../src/GameSupport/GameDetectionService.cpp
    ../src/GameSupport/GameHealthCheckService.cpp
    ../src/GameSupport/GameSupportRegistry.cpp
    ../src/GameSupport/ProjectFingerprint.cpp
    ../src/GameSupport/SkyrimSpecialEditionSupport.cpp
    ../src/Services/DownloadService.cpp
    ../src/Services/ExecutableIconService.cpp
    ../src/Services/ExecutableService.cpp
    ../src/Services/FomodInstallerService.cpp
    ../src/Services/FluxPackService.cpp
    ../src/Services/HookService.cpp
    ../src/Services/Logger.cpp
    ../src/Services/ModOrganizerExecutableImportService.cpp
    ../src/Services/ModOrganizerImportService.cpp
    ../src/Services/ModService.cpp
    ../src/Services/NexusModsAuthService.cpp
    ../src/Services/ModOrganizerPluginGroupService.cpp
    ../src/Services/ModOrganizerProfileOrderService.cpp
    ../src/Services/PathSafetyService.cpp
    ../src/Services/PluginService.cpp
    ../src/Services/ProfileOrderService.cpp
    ../src/Services/ProjectService.cpp
    ../src/Services/TemplateService.cpp
    ../src/Storage/AtomicFileStore.cpp
    ../src/Storage/InstanceMetadataStore.cpp
    ../src/Storage/ProjectStateTransaction.cpp
)

if(WIN32)
    target_sources(FluxoraCoreTests
        PRIVATE
            VfsTreeTests.cpp
            ../vfs/src/VfsLog.cpp
            ../vfs/src/VfsTree.cpp
    )
endif()

target_include_directories(FluxoraCoreTests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${PROJECT_BINARY_DIR}/generated
)

if(WIN32)
    target_include_directories(FluxoraCoreTests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../vfs/include
    )
endif()

target_compile_features(FluxoraCoreTests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(FluxoraCoreTests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(FluxoraCoreTests PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(FluxoraCoreTests PRIVATE GTest::gtest_main spdlog::spdlog_header_only)

if(WIN32)
    target_link_libraries(FluxoraCoreTests PRIVATE advapi32 bcrypt crypt32 gdiplus rstrtmgr shell32 winhttp ws2_32)
endif()

include(GoogleTest)
gtest_discover_tests(FluxoraCoreTests)
