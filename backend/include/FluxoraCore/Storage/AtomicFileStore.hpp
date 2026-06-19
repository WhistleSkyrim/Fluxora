#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace fluxora
{
    class Logger;

    enum class ProjectStateValidation
    {
        None,
        Utf8Text,
        JsonObject
    };

    enum class AtomicWriteFailurePoint
    {
        None,
        AfterTempFileValidated,
        AfterBackupCreated,
        BeforeReplace
    };

    struct AtomicFileWriteOptions
    {
        std::wstring stateName;
        ProjectStateValidation validation{ProjectStateValidation::Utf8Text};
        std::function<void(const std::filesystem::path&)> validator;
        bool keepBackup{true};

        // Test hooks used to exercise crash and disk-full recovery paths without
        // depending on host-level failures.
        AtomicWriteFailurePoint simulateFailurePoint{AtomicWriteFailurePoint::None};
        std::optional<std::size_t> simulateDiskFullAfterBytes;
    };

    enum class AtomicFileRecoveryAction
    {
        None,
        RemovedStaleTemp,
        RestoredBackup,
        PromotedTemp
    };

    struct AtomicFileRecoveryResult
    {
        AtomicFileRecoveryAction action{AtomicFileRecoveryAction::None};
        std::filesystem::path targetPath;
        std::filesystem::path backupPath;
        std::vector<std::filesystem::path> removedTempFiles;
    };

    class AtomicFileStore final
    {
    public:
        void writeTextFile(
            const std::filesystem::path& path,
            const std::string& content,
            const AtomicFileWriteOptions& options = {}) const;

        [[nodiscard]] AtomicFileRecoveryResult recoverFile(
            const std::filesystem::path& path,
            const AtomicFileWriteOptions& options = {},
            Logger* logger = nullptr) const;

        static void validateFile(
            const std::filesystem::path& path,
            const AtomicFileWriteOptions& options);

        [[nodiscard]] static std::filesystem::path backupPathFor(
            const std::filesystem::path& path);

        [[nodiscard]] static bool isManagedTempFileFor(
            const std::filesystem::path& targetPath,
            const std::filesystem::path& candidatePath);
    };
}
