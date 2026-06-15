#pragma once

#include "FluxoraCore/Services/IService.hpp"

#include <cstdint>
#include <filesystem>
#include <mutex>

namespace fluxora
{
    class Logger;

    class ExecutableIconService final : public IService
    {
    public:
        explicit ExecutableIconService(Logger& logger) noexcept;

        void initialize() override;
        void shutdown() override;

        [[nodiscard]] std::filesystem::path resolveIconPath(
            const std::filesystem::path& executablePath) const;

        [[nodiscard]] bool isInitialized() const noexcept;

    private:
        [[nodiscard]] std::filesystem::path resolveCacheDirectory() const;

        Logger& logger_;
        std::filesystem::path cacheDirectory_;
        mutable std::mutex cacheMutex_;
        std::uintptr_t gdiplusToken_{0};
        bool initialized_{false};
    };
}
