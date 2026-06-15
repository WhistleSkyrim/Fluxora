#pragma once

#include "FluxoraCore/Services/IService.hpp"

#include <string>

namespace fluxora
{
    class AppSettingsService;
    class Logger;

    struct NexusModsAuthStatus
    {
        bool isConfigured{false};
        bool isLinked{false};
        std::wstring displayName;
        std::wstring userId;
        std::wstring message;
        std::wstring clientId;
        std::wstring redirectUri;
    };

    class NexusModsAuthService final : public IService
    {
    public:
        NexusModsAuthService(Logger& logger, AppSettingsService& settings) noexcept;

        void initialize() override;
        void shutdown() override;

        [[nodiscard]] NexusModsAuthStatus status() const;
        NexusModsAuthStatus connect();
        NexusModsAuthStatus disconnect();

        [[nodiscard]] bool isInitialized() const noexcept;

    private:
        Logger& logger_;
        AppSettingsService& settings_;
        bool initialized_{false};
    };
}
