#pragma once

#include "FluxoraCore/GameSupport/ProjectFingerprint.hpp"
#include "FluxoraCore/Services/IService.hpp"
#include "FluxoraCore/Services/TemplateService.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fluxora
{
    class Logger;
    class TemplateService;
    struct ProjectCreateRequest
    {
        std::wstring name;
        std::wstring templateId;
        std::filesystem::path gamePath;
        std::filesystem::path installRootDirectory;
        bool validateGameDirectory{true};
    };

    struct ProjectDescriptor
    {
        std::wstring name;
        std::wstring templateId;
        std::wstring gameName;
        std::filesystem::path gamePath;
        std::filesystem::path installRootDirectory;
        std::filesystem::path projectDirectory;
        std::filesystem::path configPath;
        std::optional<ProjectFingerprint> fingerprint;
    };

    struct ProjectOpenResult
    {
        ProjectDescriptor project;
        BuildTemplate resolvedTemplate;
    };

    struct ProjectDeleteProgress
    {
        std::wstring phase;
        std::wstring currentStep;
        std::wstring currentItem;
        int overallPercent{0};
        std::uintmax_t deletedBytes{0};
        std::uintmax_t totalBytes{0};
        std::uintmax_t deletedEntries{0};
        std::uintmax_t totalEntries{0};
    };

    struct ProjectDeleteRequest
    {
        std::filesystem::path configPath;
        std::function<void(const ProjectDeleteProgress&)> progress;
    };

    class ProjectService final : public IService
    {
    public:
        ProjectService(Logger& logger, const TemplateService& templates) noexcept;

        void initialize() override;
        void shutdown() override;

        [[nodiscard]] std::filesystem::path buildProjectDirectory(
            const std::filesystem::path& installRootDirectory,
            std::wstring_view projectName) const;

        ProjectDescriptor createProject(const ProjectCreateRequest& request);
        ProjectOpenResult readProjectConfigSummary(const std::filesystem::path& configPath) const;
        std::vector<ProjectOpenResult> listProjectConfigSummaries(
            const std::filesystem::path& buildConfigsDirectory) const;
        ProjectOpenResult openProjectConfig(const std::filesystem::path& configPath);
        ProjectOpenResult renameProject(
            const std::filesystem::path& configPath,
            std::wstring_view newName);
        void deleteProject(const std::filesystem::path& configPath);
        void deleteProject(const ProjectDeleteRequest& request);

        [[nodiscard]] const std::vector<ProjectDescriptor>& projects() const noexcept;
        [[nodiscard]] bool isInitialized() const noexcept;

    private:
        void materializeTemplate(
            const std::filesystem::path& projectDirectory,
            const BuildTemplate& resolved) const;

        void writeBuildManifest(
            const ProjectDescriptor& project,
            const BuildTemplate& resolved) const;

        Logger& logger_;
        const TemplateService& templates_;
        std::vector<ProjectDescriptor> projects_;
        bool initialized_{false};
    };
}
