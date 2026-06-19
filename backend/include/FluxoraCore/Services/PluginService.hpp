#pragma once

#include "FluxoraCore/GameSupport/IGameSupport.hpp"
#include "FluxoraCore/Services/IService.hpp"
#include "FluxoraCore/Services/TemplateService.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace fluxora
{
    class Logger;
    class BuildPathSettingsService;
    struct GameHealthCheckResult;

    struct PluginEntry
    {
        std::wstring orderId;
        std::wstring kind;
        int order{0};
        std::wstring name;
        std::wstring extension;
        std::wstring sourceMod;
        bool isEnabled{true};
        bool isMaster{false};
        bool isLight{false};
        bool isLocked{false};
        std::wstring lockReason;
        std::wstring separatorTitle;
    };

    struct PluginRuleContext
    {
        const IPluginRulesProvider* rulesProvider{nullptr};
        const CapabilitySet* capabilities{nullptr};
        const GameHealthCheckResult* health{nullptr};
        std::wstring defaultProfileName;
        std::wstring gameId;
    };

    class PluginService final : public IService
    {
    public:
        PluginService(
            Logger& logger,
            const BuildPathSettingsService& pathSettings) noexcept;

        void initialize() override;
        void shutdown() override;

        [[nodiscard]] std::vector<PluginEntry> listPlugins(
            const std::filesystem::path& projectDirectory,
            const BuildTemplate& resolvedTemplate,
            std::wstring_view profileName) const;

        [[nodiscard]] std::vector<PluginEntry> listPlugins(
            const std::filesystem::path& projectDirectory,
            const PluginRuleContext& rules,
            std::wstring_view profileName) const;

        [[nodiscard]] std::vector<PluginEntry> movePlugin(
            const std::filesystem::path& projectDirectory,
            const BuildTemplate& resolvedTemplate,
            std::wstring_view profileName,
            std::wstring_view orderItemId,
            int targetIndex) const;

        [[nodiscard]] std::vector<PluginEntry> movePlugin(
            const std::filesystem::path& projectDirectory,
            const PluginRuleContext& rules,
            std::wstring_view profileName,
            std::wstring_view orderItemId,
            int targetIndex) const;

        [[nodiscard]] std::vector<PluginEntry> createPluginSeparator(
            const std::filesystem::path& projectDirectory,
            const BuildTemplate& resolvedTemplate,
            std::wstring_view profileName,
            std::wstring_view title,
            int targetIndex) const;

        [[nodiscard]] std::vector<PluginEntry> createPluginSeparator(
            const std::filesystem::path& projectDirectory,
            const PluginRuleContext& rules,
            std::wstring_view profileName,
            std::wstring_view title,
            int targetIndex) const;

        [[nodiscard]] std::vector<PluginEntry> deletePluginSeparator(
            const std::filesystem::path& projectDirectory,
            const BuildTemplate& resolvedTemplate,
            std::wstring_view profileName,
            std::wstring_view separatorId) const;

        [[nodiscard]] std::vector<PluginEntry> deletePluginSeparator(
            const std::filesystem::path& projectDirectory,
            const PluginRuleContext& rules,
            std::wstring_view profileName,
            std::wstring_view separatorId) const;

        [[nodiscard]] std::vector<PluginEntry> setPluginEnabled(
            const std::filesystem::path& projectDirectory,
            const BuildTemplate& resolvedTemplate,
            std::wstring_view profileName,
            std::wstring_view pluginName,
            bool isEnabled) const;

        [[nodiscard]] std::vector<PluginEntry> setPluginEnabled(
            const std::filesystem::path& projectDirectory,
            const PluginRuleContext& rules,
            std::wstring_view profileName,
            std::wstring_view pluginName,
            bool isEnabled) const;

        [[nodiscard]] bool isInitialized() const noexcept;

    private:
        Logger& logger_;
        const BuildPathSettingsService& pathSettings_;
        bool initialized_{false};
    };
}
