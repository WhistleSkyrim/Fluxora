#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace fluxora
{
    struct ModSourceRecord
    {
        std::wstring provider;
        std::wstring gameDomain;
        std::wstring remoteModId;
        std::wstring remoteFileId;
        std::wstring url;
        std::wstring lastCheckedAt;
        std::wstring latestVersion;
    };

    struct InstalledModRecord
    {
        std::int64_t id{0};
        std::wstring uuid;
        std::wstring gameId;
        std::wstring folderName;
        std::wstring displayName;
        std::wstring version;
        std::wstring installedAt;
        std::wstring updatedAt;
        std::wstring state;
        std::wstring contentFingerprint;
        std::filesystem::path path;
        ModSourceRecord source;
    };

    struct InstalledModImportRecord
    {
        std::filesystem::path modDirectory;
        std::wstring displayName;
        std::wstring version;
        bool isEnabled{true};
        ModSourceRecord source;
        bool computeContentFingerprint{true};
    };

    using InstalledModImportProgress =
        std::function<void(std::size_t processed, std::size_t total, std::wstring_view folderName)>;

    struct RemoteCheckRecord
    {
        std::wstring folderName;
        ModSourceRecord source;
        std::wstring latestVersion;
        std::wstring payloadJson;
        std::wstring checkedAt;
    };

    struct ModFileSummary
    {
        int fileCount{0};
        int conflictingFileCount{0};
        int overwrittenFileCount{0};
        int overwritingFileCount{0};
    };

    struct ModFileSummaryRecord
    {
        std::wstring folderName;
        std::filesystem::path modPath;
        ModFileSummary summary;
    };

    struct ModFileTreeEntry
    {
        std::wstring name;
        std::wstring relativePath;
        bool isDirectory{false};
        bool hasChildren{false};
        std::uintmax_t size{0};
        std::wstring conflictState;
        std::vector<std::wstring> conflictOwners;
    };

    struct ProfileOrderItemRecord
    {
        std::wstring id;
        std::wstring profileName;
        std::wstring kind;
        int position{0};
        std::wstring separatorTitle;
        bool hasMod{false};
        InstalledModRecord mod;
    };

    struct ProfileOrderImportItemRecord
    {
        std::wstring kind;
        std::wstring folderName;
        std::wstring separatorTitle;
    };

    struct ProfilePluginOrderItemRecord
    {
        std::wstring id;
        std::wstring profileName;
        std::wstring kind;
        int position{0};
        std::wstring pluginName;
        std::wstring separatorTitle;
    };

    struct ProfilePluginOrderImportItemRecord
    {
        std::wstring kind;
        std::wstring pluginName;
        std::wstring separatorTitle;
    };

    class InstanceMetadataStore final
    {
    public:
        InstanceMetadataStore() = delete;

        static void ensureInstance(
            const std::filesystem::path& projectDirectory,
            std::wstring_view gameId = {});

        [[nodiscard]] static std::wstring gameId(
            const std::filesystem::path& projectDirectory);

        [[nodiscard]] static std::vector<InstalledModRecord> listInstalledMods(
            const std::filesystem::path& projectDirectory,
            const std::filesystem::path& modsDirectory = {});

        [[nodiscard]] static std::vector<ProfileOrderItemRecord> listProfileOrderItems(
            const std::filesystem::path& projectDirectory,
            std::wstring_view profileName,
            const std::filesystem::path& modsDirectory = {});

        [[nodiscard]] static std::vector<ProfileOrderItemRecord> createProfileOrderSeparator(
            const std::filesystem::path& projectDirectory,
            std::wstring_view profileName,
            std::wstring_view title,
            int targetIndex,
            const std::filesystem::path& modsDirectory = {});

        [[nodiscard]] static std::vector<ProfileOrderItemRecord> deleteProfileOrderSeparator(
            const std::filesystem::path& projectDirectory,
            std::wstring_view profileName,
            std::wstring_view separatorId,
            const std::filesystem::path& modsDirectory = {});

        [[nodiscard]] static std::vector<ProfileOrderItemRecord> moveProfileOrderItem(
            const std::filesystem::path& projectDirectory,
            std::wstring_view profileName,
            std::wstring_view orderItemId,
            int targetIndex,
            const std::filesystem::path& modsDirectory = {});

        static void replaceProfileOrderItems(
            const std::filesystem::path& projectDirectory,
            std::wstring_view profileName,
            const std::vector<ProfileOrderImportItemRecord>& items);

        [[nodiscard]] static std::vector<ProfilePluginOrderItemRecord> listProfilePluginOrderItems(
            const std::filesystem::path& projectDirectory,
            std::wstring_view profileName,
            const std::vector<std::wstring>& pluginNames);

        static void replaceProfilePluginOrderItems(
            const std::filesystem::path& projectDirectory,
            std::wstring_view profileName,
            const std::vector<ProfilePluginOrderImportItemRecord>& items);

        [[nodiscard]] static std::vector<ProfilePluginOrderItemRecord> createProfilePluginOrderSeparator(
            const std::filesystem::path& projectDirectory,
            std::wstring_view profileName,
            const std::vector<std::wstring>& pluginNames,
            std::wstring_view title,
            int targetIndex);

        [[nodiscard]] static std::vector<ProfilePluginOrderItemRecord> deleteProfilePluginOrderSeparator(
            const std::filesystem::path& projectDirectory,
            std::wstring_view profileName,
            const std::vector<std::wstring>& pluginNames,
            std::wstring_view separatorId);

        [[nodiscard]] static std::vector<ProfilePluginOrderItemRecord> moveProfilePluginOrderItem(
            const std::filesystem::path& projectDirectory,
            std::wstring_view profileName,
            const std::vector<std::wstring>& pluginNames,
            std::wstring_view orderItemId,
            int targetIndex);

        static InstalledModRecord registerInstalledMod(
            const std::filesystem::path& projectDirectory,
            const std::filesystem::path& modDirectory,
            std::wstring_view displayName,
            std::wstring_view version,
            const ModSourceRecord& source);

        static void registerInstalledMods(
            const std::filesystem::path& projectDirectory,
            const std::vector<InstalledModImportRecord>& mods,
            const InstalledModImportProgress& progress = {});

        static void deleteInstalledMod(
            const std::filesystem::path& projectDirectory,
            const std::filesystem::path& modPath);

        static void setInstalledModEnabled(
            const std::filesystem::path& projectDirectory,
            const std::filesystem::path& modPath,
            bool isEnabled);

        static void setAllInstalledModsEnabled(
            const std::filesystem::path& projectDirectory,
            bool isEnabled,
            const std::filesystem::path& modsDirectory = {});

        static void recordRemoteCheck(
            const std::filesystem::path& projectDirectory,
            const RemoteCheckRecord& check,
            const std::filesystem::path& modsDirectory = {});

        [[nodiscard]] static ModFileSummary summarizeModFiles(
            const std::filesystem::path& projectDirectory,
            const std::filesystem::path& modPath,
            const std::filesystem::path& modsDirectory = {});

        [[nodiscard]] static std::vector<ModFileSummaryRecord> summarizeInstalledModFiles(
            const std::filesystem::path& projectDirectory,
            const std::filesystem::path& modsDirectory = {});

        [[nodiscard]] static std::vector<ModFileSummaryRecord> summarizeProfileModFiles(
            const std::filesystem::path& projectDirectory,
            std::wstring_view profileName,
            const std::filesystem::path& modsDirectory = {});

        [[nodiscard]] static std::vector<ModFileTreeEntry> listModFileTree(
            const std::filesystem::path& projectDirectory,
            const std::filesystem::path& modPath,
            std::wstring_view relativeDirectory,
            const std::filesystem::path& modsDirectory = {});
    };
}
