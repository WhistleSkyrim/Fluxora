#pragma once

#include "FluxoraCore/Services/IService.hpp"
#include "FluxoraCore/Services/ModService.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace fluxora
{
    class BuildPathSettingsService;
    class Logger;

    struct ProfileModOrderItem
    {
        std::wstring orderId;
        std::wstring kind;
        int order{0};
        std::filesystem::path id;
        std::wstring name;
        std::wstring version;
        std::wstring latestVersion;
        std::wstring lastCheckedAt;
        std::wstring updateStatus;
        std::wstring conflictStatus;
        int fileCount{0};
        int conflictingFileCount{0};
        int overwrittenFileCount{0};
        int overwritingFileCount{0};
        bool isEnabled{true};
        bool canCheckUpdates{false};
        bool hasUpdate{false};
        std::wstring modUuid;
        std::wstring separatorTitle;
    };

    class ProfileOrderService final : public IService
    {
    public:
        ProfileOrderService(
            Logger& logger,
            ModService& mods,
            const BuildPathSettingsService& pathSettings) noexcept;

        void initialize() override;
        void shutdown() override;

        [[nodiscard]] std::vector<ProfileModOrderItem> listModOrder(
            const std::filesystem::path& projectDirectory,
            std::wstring_view profileName) const;

        [[nodiscard]] std::vector<ProfileModOrderItem> createModSeparator(
            const std::filesystem::path& projectDirectory,
            std::wstring_view profileName,
            std::wstring_view title,
            int targetIndex) const;

        [[nodiscard]] std::vector<ProfileModOrderItem> deleteModSeparator(
            const std::filesystem::path& projectDirectory,
            std::wstring_view profileName,
            std::wstring_view separatorId) const;

        [[nodiscard]] std::vector<ProfileModOrderItem> moveModOrderItem(
            const std::filesystem::path& projectDirectory,
            std::wstring_view profileName,
            std::wstring_view orderItemId,
            int targetIndex) const;

        [[nodiscard]] bool isInitialized() const noexcept;

    private:
        Logger& logger_;
        ModService& mods_;
        const BuildPathSettingsService& pathSettings_;
        bool initialized_{false};
    };
}
