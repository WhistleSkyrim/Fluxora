#include "FluxoraCore/Services/ProfileOrderService.hpp"

#include "FluxoraCore/Services/BuildPathSettingsService.hpp"
#include "FluxoraCore/Services/Logger.hpp"
#include "FluxoraCore/Storage/InstanceMetadataStore.hpp"

#include <algorithm>
#include <cwctype>
#include <map>
#include <stdexcept>
#include <utility>

namespace fluxora
{
    namespace
    {
        constexpr std::wstring_view modKind = L"mod";
        constexpr std::wstring_view separatorKind = L"separator";

        std::wstring toLower(std::wstring value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character)
            {
                return static_cast<wchar_t>(std::towlower(character));
            });
            return value;
        }

        std::wstring trim(std::wstring value)
        {
            const auto first = value.find_first_not_of(L" \t\r\n");
            if (first == std::wstring::npos)
            {
                return {};
            }

            const auto last = value.find_last_not_of(L" \t\r\n");
            return value.substr(first, last - first + 1);
        }

        bool equalsIgnoreCase(std::wstring_view left, std::wstring_view right)
        {
            return toLower(std::wstring(left)) == toLower(std::wstring(right));
        }

        bool canCheckNexusUpdates(const InstalledModRecord& mod)
        {
            return equalsIgnoreCase(mod.source.provider, L"nexus") &&
                !mod.source.gameDomain.empty() &&
                !mod.source.remoteModId.empty();
        }

        bool isUnknownVersion(std::wstring_view value)
        {
            const std::wstring normalized = toLower(trim(std::wstring(value)));
            return normalized.empty() || normalized == L"unknown";
        }

        bool hasUpdate(const InstalledModRecord& mod)
        {
            return !isUnknownVersion(mod.version) &&
                !isUnknownVersion(mod.source.latestVersion) &&
                !equalsIgnoreCase(mod.version, mod.source.latestVersion);
        }

        std::wstring updateStatusText(const InstalledModRecord& mod)
        {
            if (!canCheckNexusUpdates(mod))
            {
                return mod.source.provider.empty() || equalsIgnoreCase(mod.source.provider, L"local")
                    ? L"Локальный мод"
                    : L"Ручной источник";
            }

            if (mod.source.lastCheckedAt.empty())
            {
                return L"Не проверялся";
            }

            if (isUnknownVersion(mod.source.latestVersion))
            {
                return L"Проверено";
            }

            if (isUnknownVersion(mod.version))
            {
                return L"Последняя: " + mod.source.latestVersion;
            }

            return hasUpdate(mod)
                ? L"Доступно: " + mod.source.latestVersion
                : L"Актуально";
        }

        std::wstring conflictStatusText(const ModFileSummary& summary)
        {
            if (summary.fileCount < 0)
            {
                return L"Файлы не просканированы";
            }
            if (summary.fileCount == 0)
            {
                return L"Файлов нет";
            }
            if (summary.conflictingFileCount == 0)
            {
                return L"Конфликтов нет";
            }

            return std::to_wstring(summary.conflictingFileCount) +
                L" конфликтных; перекрывает " +
                std::to_wstring(summary.overwritingFileCount) +
                L", перекрыт " +
                std::to_wstring(summary.overwrittenFileCount);
        }

        std::wstring pathKey(const std::filesystem::path& path)
        {
            return toLower(path.wstring());
        }

        std::map<std::wstring, InstalledModEntry> installedModsByPath(
            const std::vector<InstalledModEntry>& mods)
        {
            std::map<std::wstring, InstalledModEntry> entries;
            for (const InstalledModEntry& mod : mods)
            {
                entries.emplace(pathKey(mod.id), mod);
            }

            return entries;
        }

        ProfileModOrderItem separatorItemFromRecord(const ProfileOrderItemRecord& record)
        {
            return ProfileModOrderItem{
                record.id,
                std::wstring(separatorKind),
                record.position,
                std::filesystem::path(record.id),
                record.separatorTitle,
                {},
                {},
                {},
                {},
                {},
                0,
                0,
                0,
                0,
                true,
                false,
                false,
                {},
                record.separatorTitle
            };
        }

        ProfileModOrderItem modItemFromRecord(
            const ProfileOrderItemRecord& record,
            const InstalledModEntry& mod)
        {
            return ProfileModOrderItem{
                record.id,
                std::wstring(modKind),
                record.position,
                mod.id,
                mod.name,
                mod.version,
                mod.latestVersion,
                mod.lastCheckedAt,
                mod.updateStatus,
                mod.conflictStatus,
                mod.fileCount,
                mod.conflictingFileCount,
                mod.overwrittenFileCount,
                mod.overwritingFileCount,
                mod.isEnabled,
                mod.canCheckUpdates,
                mod.hasUpdate,
                record.mod.uuid,
                {}
            };
        }

        ProfileModOrderItem modItemFromRecord(
            const ProfileOrderItemRecord& record,
            const ModFileSummary& summary)
        {
            const InstalledModRecord& mod = record.mod;
            return ProfileModOrderItem{
                record.id,
                std::wstring(modKind),
                record.position,
                mod.path,
                mod.displayName.empty() ? mod.folderName : mod.displayName,
                isUnknownVersion(mod.version) ? L"Unknown" : mod.version,
                mod.source.latestVersion,
                mod.source.lastCheckedAt,
                updateStatusText(mod),
                conflictStatusText(summary),
                summary.fileCount,
                summary.conflictingFileCount,
                summary.overwrittenFileCount,
                summary.overwritingFileCount,
                mod.state != L"disabled",
                canCheckNexusUpdates(mod),
                hasUpdate(mod),
                record.mod.uuid,
                {}
            };
        }

        ProfileModOrderItem modItemFromRecord(const ProfileOrderItemRecord& record)
        {
            const InstalledModRecord& mod = record.mod;
            return ProfileModOrderItem{
                record.id,
                std::wstring(modKind),
                record.position,
                mod.path,
                mod.displayName.empty() ? mod.folderName : mod.displayName,
                isUnknownVersion(mod.version) ? L"Unknown" : mod.version,
                mod.source.latestVersion,
                mod.source.lastCheckedAt,
                updateStatusText(mod),
                L"Файлы не просканированы",
                -1,
                0,
                0,
                0,
                mod.state != L"disabled",
                canCheckNexusUpdates(mod),
                hasUpdate(mod),
                mod.uuid,
                {}
            };
        }

        std::vector<ProfileModOrderItem> buildModOrder(
            const std::vector<ProfileOrderItemRecord>& records,
            const std::vector<InstalledModEntry>& mods)
        {
            const std::map<std::wstring, InstalledModEntry> modEntries = installedModsByPath(mods);
            std::vector<ProfileModOrderItem> items;
            items.reserve(records.size());

            for (const ProfileOrderItemRecord& record : records)
            {
                if (record.kind == separatorKind)
                {
                    items.push_back(separatorItemFromRecord(record));
                    continue;
                }

                if (record.kind != modKind || !record.hasMod)
                {
                    continue;
                }

                const auto mod = modEntries.find(pathKey(record.mod.path));
                if (mod != modEntries.end())
                {
                    items.push_back(modItemFromRecord(record, mod->second));
                }
            }

            return items;
        }

        std::map<std::wstring, ModFileSummary> summariesByPath(
            const std::vector<ModFileSummaryRecord>& summaries)
        {
            std::map<std::wstring, ModFileSummary> entries;
            for (const ModFileSummaryRecord& summary : summaries)
            {
                entries.emplace(pathKey(summary.modPath), summary.summary);
            }

            return entries;
        }

        std::vector<ProfileModOrderItem> buildModOrder(
            const std::vector<ProfileOrderItemRecord>& records,
            const std::vector<ModFileSummaryRecord>& summaries)
        {
            const std::map<std::wstring, ModFileSummary> summariesByModPath = summariesByPath(summaries);
            std::vector<ProfileModOrderItem> items;
            items.reserve(records.size());

            for (const ProfileOrderItemRecord& record : records)
            {
                if (record.kind == separatorKind)
                {
                    items.push_back(separatorItemFromRecord(record));
                    continue;
                }

                if (record.kind != modKind || !record.hasMod)
                {
                    continue;
                }

                const auto summary = summariesByModPath.find(pathKey(record.mod.path));
                if (summary != summariesByModPath.end())
                {
                    items.push_back(modItemFromRecord(record, summary->second));
                }
                else
                {
                    items.push_back(modItemFromRecord(record));
                }
            }

            return items;
        }

        std::vector<ProfileModOrderItem> buildModOrder(
            const std::vector<ProfileOrderItemRecord>& records)
        {
            std::vector<ProfileModOrderItem> items;
            items.reserve(records.size());

            for (const ProfileOrderItemRecord& record : records)
            {
                if (record.kind == separatorKind)
                {
                    items.push_back(separatorItemFromRecord(record));
                    continue;
                }

                if (record.kind == modKind && record.hasMod)
                {
                    items.push_back(modItemFromRecord(record));
                }
            }

            return items;
        }
    }

    ProfileOrderService::ProfileOrderService(
        Logger& logger,
        ModService& mods,
        const BuildPathSettingsService& pathSettings) noexcept
        : logger_(logger),
          mods_(mods),
          pathSettings_(pathSettings)
    {
    }

    void ProfileOrderService::initialize()
    {
        if (initialized_)
        {
            return;
        }

        initialized_ = true;
        logger_.write(LogLevel::Info, "Profile order service initialized.");
    }

    void ProfileOrderService::shutdown()
    {
        if (!initialized_)
        {
            return;
        }

        logger_.write(LogLevel::Info, "Profile order service shut down.");
        initialized_ = false;
    }

    std::vector<ProfileModOrderItem> ProfileOrderService::listModOrder(
        const std::filesystem::path& projectDirectory,
        std::wstring_view profileName) const
    {
        if (projectDirectory.empty())
        {
            throw std::invalid_argument("Project directory is required.");
        }

        const std::vector<ProfileOrderItemRecord> records =
            InstanceMetadataStore::listProfileOrderItems(
                projectDirectory,
                profileName,
                pathSettings_.modsDirectory(projectDirectory));
        return buildModOrder(records);
    }

    std::vector<ProfileModOrderItem> ProfileOrderService::createModSeparator(
        const std::filesystem::path& projectDirectory,
        std::wstring_view profileName,
        std::wstring_view title,
        int targetIndex) const
    {
        if (projectDirectory.empty())
        {
            throw std::invalid_argument("Project directory is required.");
        }

        const std::vector<ProfileOrderItemRecord> records =
            InstanceMetadataStore::createProfileOrderSeparator(
                projectDirectory,
                profileName,
                title,
                targetIndex,
                pathSettings_.modsDirectory(projectDirectory));
        return buildModOrder(records);
    }

    std::vector<ProfileModOrderItem> ProfileOrderService::deleteModSeparator(
        const std::filesystem::path& projectDirectory,
        std::wstring_view profileName,
        std::wstring_view separatorId) const
    {
        if (projectDirectory.empty())
        {
            throw std::invalid_argument("Project directory is required.");
        }

        const std::vector<ProfileOrderItemRecord> records =
            InstanceMetadataStore::deleteProfileOrderSeparator(
                projectDirectory,
                profileName,
                separatorId,
                pathSettings_.modsDirectory(projectDirectory));
        return buildModOrder(records);
    }

    std::vector<ProfileModOrderItem> ProfileOrderService::moveModOrderItem(
        const std::filesystem::path& projectDirectory,
        std::wstring_view profileName,
        std::wstring_view orderItemId,
        int targetIndex) const
    {
        if (projectDirectory.empty())
        {
            throw std::invalid_argument("Project directory is required.");
        }

        const std::vector<ProfileOrderItemRecord> records =
            InstanceMetadataStore::moveProfileOrderItem(
                projectDirectory,
                profileName,
                orderItemId,
                targetIndex,
                pathSettings_.modsDirectory(projectDirectory));
        return buildModOrder(records);
    }

    bool ProfileOrderService::isInitialized() const noexcept
    {
        return initialized_;
    }
}
