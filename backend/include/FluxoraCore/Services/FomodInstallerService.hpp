#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fluxora
{
    struct FomodFileEntry
    {
        std::wstring source;
        std::wstring destination;
        bool isFolder{false};
        bool alwaysInstall{false};
        bool installIfUsable{false};
        int priority{0};
    };

    struct FomodConditionFlag
    {
        std::wstring name;
        std::wstring value;
    };

    struct FomodDependencyNode
    {
        std::wstring kind;
        std::wstring op{L"And"};
        std::wstring file;
        std::wstring state;
        std::wstring flag;
        std::wstring value;
        std::wstring version;
        std::vector<FomodDependencyNode> children;
    };

    struct FomodTypePattern
    {
        FomodDependencyNode dependencies;
        std::wstring type;
    };

    struct FomodConditionalFilePattern
    {
        FomodDependencyNode dependencies;
        std::vector<FomodFileEntry> files;
    };

    struct FomodFileDependencyState
    {
        std::wstring file;
        bool exists{false};
    };

    struct FomodOption
    {
        std::wstring id;
        std::wstring name;
        std::wstring description;
        std::wstring imagePath;
        std::wstring type{L"Optional"};
        std::wstring defaultType{L"Optional"};
        std::vector<FomodFileEntry> files;
        std::vector<FomodConditionFlag> flags;
        std::vector<FomodTypePattern> typePatterns;
    };

    struct FomodGroup
    {
        std::wstring id;
        std::wstring name;
        std::wstring type{L"SelectAny"};
        std::vector<FomodOption> options;
    };

    struct FomodStep
    {
        std::wstring id;
        std::wstring name;
        std::optional<FomodDependencyNode> visible;
        std::vector<FomodGroup> groups;
    };

    struct FomodInstallerDescriptor
    {
        bool isFomod{false};
        std::wstring moduleName;
        std::wstring moduleVersion;
        std::wstring moduleId;
        std::wstring moduleImagePath;
        std::wstring memoryKey;
        bool hasPreviousSelection{false};
        std::vector<std::wstring> previousSelectedOptionIds;
        std::vector<FomodFileDependencyState> fileDependencyStates;
        std::vector<FomodFileEntry> requiredFiles;
        std::vector<FomodStep> steps;
        std::vector<FomodConditionalFilePattern> conditionalFilePatterns;
    };

    struct FomodPackageIdentity
    {
        std::wstring provider;
        std::wstring gameDomain;
        std::wstring remoteModId;
        std::wstring remoteFileId;
        std::wstring source;
        std::wstring fallbackName;
    };

    struct FomodInstallContext
    {
        std::filesystem::path projectDirectory;
        std::filesystem::path gameDirectory;
        std::filesystem::path modsDirectory;
        std::filesystem::path packageDirectory;
        std::filesystem::path destinationDirectory;
        FomodPackageIdentity identity;
        std::vector<std::wstring> selectedOptionIds;
        std::vector<std::wstring> gameDataFolders;
    };

    class FomodInstallerService final
    {
    public:
        FomodInstallerService() = delete;

        [[nodiscard]] static bool hasXmlInstaller(const std::filesystem::path& packageDirectory);

        [[nodiscard]] static FomodInstallerDescriptor analyze(
            const std::filesystem::path& projectDirectory,
            const std::filesystem::path& gameDirectory,
            const std::filesystem::path& modsDirectory,
            const std::filesystem::path& packageDirectory,
            const FomodPackageIdentity& identity,
            const std::vector<std::wstring>& gameDataFolders = {});

        [[nodiscard]] static std::vector<std::wstring> install(const FomodInstallContext& context);

        static void rememberSelection(
            const std::filesystem::path& projectDirectory,
            const FomodInstallerDescriptor& descriptor,
            const std::vector<std::wstring>& selectedOptionIds);
    };
}
