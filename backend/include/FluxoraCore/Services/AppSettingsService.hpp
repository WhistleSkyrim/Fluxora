#pragma once

#include "FluxoraCore/Services/IService.hpp"

#include <filesystem>
#include <string>

namespace fluxora
{
    class Logger;

    struct NexusModsStoredAuth
    {
        bool linked{false};
        std::wstring username;
        std::wstring userId;
        std::wstring tokenType;
        std::wstring expiresAtUtc;
        std::wstring protectedAccessToken;
        std::wstring protectedRefreshToken;
        std::wstring protectedApiKey;
    };

    class AppSettingsService final : public IService
    {
    public:
        explicit AppSettingsService(Logger& logger) noexcept;

        void initialize() override;
        void shutdown() override;

        [[nodiscard]] NexusModsStoredAuth loadNexusModsAuth() const;
        void saveNexusModsAuth(const NexusModsStoredAuth& auth) const;
        void clearNexusModsAuth() const;

        [[nodiscard]] std::wstring loadLanguageCode() const;
        void saveLanguageCode(std::wstring_view languageCode) const;

        [[nodiscard]] const std::filesystem::path& settingsPath() const noexcept;
        [[nodiscard]] const std::filesystem::path& appConfigPath() const noexcept;
        [[nodiscard]] bool isInitialized() const noexcept;

    private:
        [[nodiscard]] std::filesystem::path resolveSettingsPath() const;
        [[nodiscard]] std::filesystem::path resolveAppConfigPath() const;
        [[nodiscard]] std::wstring resolveDefaultLanguageCode() const;

        Logger& logger_;
        std::filesystem::path settingsPath_;
        std::filesystem::path appConfigPath_;
        bool initialized_{false};
    };
}
