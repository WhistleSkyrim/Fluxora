#pragma once

#include "FluxoraCore/Services/Logger.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace fluxora
{
    struct BulkFileCopyRoot
    {
        std::filesystem::path sourceDirectory;
        std::filesystem::path destinationDirectory;
        std::wstring currentStep;
        std::function<bool(const std::filesystem::path&)> shouldSkip;
    };

    struct BulkFileCopyProgress
    {
        std::wstring currentStep;
        std::filesystem::path currentItem;
        std::uintmax_t copiedBytes{0};
        std::uintmax_t totalBytes{0};
    };

    struct BulkFileCopyOptions
    {
        std::uintmax_t totalBytes{0};
        std::size_t maxWorkers{0};
        std::function<void(const BulkFileCopyProgress&)> progress;
        std::function<void(LogLevel, std::string_view)> diagnostics;
    };

    class BulkFileCopyService final
    {
    public:
        explicit BulkFileCopyService(const Logger& logger) noexcept;

        std::uintmax_t copy(
            const std::vector<BulkFileCopyRoot>& roots,
            const BulkFileCopyOptions& options) const;

    private:
        const Logger& logger_;
    };
}
