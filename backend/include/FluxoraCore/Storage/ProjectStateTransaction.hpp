#pragma once

#include "FluxoraCore/Storage/AtomicFileStore.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace fluxora
{
    class Logger;

    struct ProjectStateTransactionFile
    {
        std::filesystem::path path;
        std::wstring stateName;
        ProjectStateValidation validation{ProjectStateValidation::Utf8Text};
    };

    struct ProjectStateTransactionRecovery
    {
        std::filesystem::path markerPath;
        std::vector<AtomicFileRecoveryResult> fileResults;
    };

    class ProjectStateTransaction final
    {
    public:
        ProjectStateTransaction(
            const AtomicFileStore& store,
            const std::filesystem::path& markerDirectory,
            std::wstring operationName,
            Logger* logger = nullptr);

        ProjectStateTransaction(const ProjectStateTransaction&) = delete;
        ProjectStateTransaction& operator=(const ProjectStateTransaction&) = delete;

        ~ProjectStateTransaction();

        void trackFile(
            const std::filesystem::path& path,
            std::wstring stateName,
            ProjectStateValidation validation = ProjectStateValidation::Utf8Text);

        void commit();

        [[nodiscard]] const std::filesystem::path& markerPath() const noexcept;

        [[nodiscard]] static std::vector<ProjectStateTransactionRecovery> recoverDirectory(
            const std::filesystem::path& markerDirectory,
            const AtomicFileStore& store,
            Logger* logger = nullptr);

        [[nodiscard]] static bool isTransactionMarker(
            const std::filesystem::path& path);

    private:
        void writeMarker();

        const AtomicFileStore& store_;
        std::filesystem::path markerPath_;
        std::wstring operationName_;
        Logger* logger_{nullptr};
        std::vector<ProjectStateTransactionFile> files_;
        bool committed_{false};
    };
}
