#pragma once

#include "FluxoraCore/Services/IService.hpp"

#include <filesystem>
#include <mutex>
#include <string_view>

namespace fluxora
{
    enum class LogLevel
    {
        Debug,
        Info,
        Warning,
        Error
    };

    class Logger final : public IService
    {
    public:
        void initialize() override;
        void shutdown() override;

        void write(LogLevel level, std::string_view message) const;
        void write(LogLevel level, std::string_view category, std::string_view message) const;

        [[nodiscard]] bool isInitialized() const noexcept;
        [[nodiscard]] const std::filesystem::path& logPath() const noexcept;
        [[nodiscard]] std::filesystem::path logDirectory() const;

    private:
        mutable std::mutex mutex_;
        std::filesystem::path logPath_;
        bool initialized_{false};
        bool debugEnabled_{false};
    };
}
