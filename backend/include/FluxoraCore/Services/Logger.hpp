#pragma once

#include "FluxoraCore/Services/IService.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace spdlog
{
    class logger;
}

namespace fluxora
{
    enum class LogLevel
    {
        Debug,
        Info,
        Warning,
        Error
    };

    enum class LogChannel
    {
        Core,
        Bridge,
        Operations,
        Crash
    };

    class Logger final : public IService
    {
    public:
        void initialize() override;
        void shutdown() override;

        void write(LogLevel level, std::string_view message) const;
        void write(LogLevel level, std::string_view category, std::string_view message) const;
        void write(LogChannel channel, LogLevel level, std::string_view category, std::string_view message) const;
        void writeOperation(LogLevel level, std::string_view category, std::string_view message) const;
        void writeCrash(LogLevel level, std::string_view category, std::string_view message) const;

        static void setOperationId(std::wstring_view operationId);
        static void clearOperationId();
        [[nodiscard]] static std::string operationId();

        [[nodiscard]] bool isInitialized() const noexcept;
        [[nodiscard]] const std::filesystem::path& logPath() const noexcept;
        [[nodiscard]] const std::filesystem::path& bridgeLogPath() const noexcept;
        [[nodiscard]] const std::filesystem::path& operationsLogPath() const noexcept;
        [[nodiscard]] const std::filesystem::path& crashLogPath() const noexcept;
        [[nodiscard]] std::filesystem::path logDirectory() const;

    private:
        mutable std::mutex mutex_;
        std::filesystem::path coreLogPath_;
        std::filesystem::path bridgeLogPath_;
        std::filesystem::path operationsLogPath_;
        std::filesystem::path crashLogPath_;
        std::shared_ptr<spdlog::logger> coreLogger_;
        std::shared_ptr<spdlog::logger> bridgeLogger_;
        std::shared_ptr<spdlog::logger> operationsLogger_;
        std::shared_ptr<spdlog::logger> crashLogger_;
        bool initialized_{false};
        bool debugEnabled_{false};
    };
}
