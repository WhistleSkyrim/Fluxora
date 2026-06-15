#pragma once

#include "FluxoraCore/Services/IService.hpp"
#include "FluxoraCore/Services/ProjectService.hpp"
#include "FluxoraCore/Services/TemplateService.hpp"
#include "FluxoraCore/Storage/InstanceMetadataStore.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace fluxora
{
    class Logger;
    class BuildPathSettingsService;

    enum class ModOrganizerImportMode
    {
        CreateNew,
        ReplaceExisting
    };

    struct ModOrganizerImportAnalysis
    {
        std::filesystem::path sourceDirectory;
        std::filesystem::path destinationRootDirectory;
        std::filesystem::path targetProjectDirectory;
        std::filesystem::path targetConfigPath;
        std::wstring projectName;
        std::wstring profileName;
        std::wstring templateId;
        std::wstring gameName;
        std::filesystem::path gamePath;
        std::uintmax_t totalBytes{0};
        std::uintmax_t availableBytes{0};
        int modCount{0};
        int separatorCount{0};
        bool hasEnoughSpace{false};
        bool willOverwrite{false};
        bool canImport{false};
        std::wstring statusMessage;
        std::wstring warningMessage;
    };

    struct ModOrganizerImportProgress
    {
        std::wstring phase;
        std::wstring currentStep;
        std::wstring currentItem;
        int overallPercent{0};
        int copyPercent{0};
        int databasePercent{0};
        std::uintmax_t copiedBytes{0};
        std::uintmax_t totalBytes{0};
    };

    struct ModOrganizerImportRequest
    {
        std::filesystem::path sourceDirectory;
        std::filesystem::path destinationRootDirectory;
        std::filesystem::path existingConfigPath;
        ModOrganizerImportMode mode{ModOrganizerImportMode::CreateNew};
        std::function<void(const ModOrganizerImportProgress&)> progress;
    };

    struct ModOrganizerImportResult
    {
        ProjectOpenResult project;
        ModOrganizerImportAnalysis analysis;
    };

    class ModOrganizerImportService final : public IService
    {
    public:
        ModOrganizerImportService(
            Logger& logger,
            const TemplateService& templates,
            ProjectService& projects,
            BuildPathSettingsService& pathSettings) noexcept;

        void initialize() override;
        void shutdown() override;

        [[nodiscard]] ModOrganizerImportAnalysis analyze(
            const std::filesystem::path& sourceDirectory,
            const std::filesystem::path& destinationRootDirectory,
            const std::filesystem::path& existingConfigPath = {}) const;

        ModOrganizerImportResult importInstance(const ModOrganizerImportRequest& request) const;

        [[nodiscard]] bool isInitialized() const noexcept;

    private:
        Logger& logger_;
        const TemplateService& templates_;
        ProjectService& projects_;
        BuildPathSettingsService& pathSettings_;
        bool initialized_{false};
    };
}
