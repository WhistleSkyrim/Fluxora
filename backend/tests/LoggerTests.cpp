#include "FluxoraCore/Services/Logger.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

namespace
{
    std::string readFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::in | std::ios::binary);
        std::ostringstream content;
        content << file.rdbuf();
        return content.str();
    }
}

namespace fluxora::tests
{
    TEST(LoggerTests, WritesOperationIdToCoreLog)
    {
        Logger logger;
        const std::wstring operationId = L"logger-test-operation";
        const std::string marker = "logger-test-marker";

        Logger::setOperationId(operationId);
        logger.initialize();
        ASSERT_TRUE(logger.isInitialized());
        ASSERT_FALSE(logger.logPath().empty());

        logger.write(LogLevel::Info, "LoggerTests", marker);
        logger.shutdown();
        Logger::clearOperationId();

        const std::string content = readFile(logger.logPath());
        EXPECT_NE(content.find(marker), std::string::npos);
        EXPECT_NE(content.find("op=logger-test-operation"), std::string::npos);
        EXPECT_NE(content.find("operationId=logger-test-operation"), std::string::npos);
        EXPECT_TRUE(Logger::operationId().empty());
    }

    TEST(LoggerTests, WritesOperationDiagnosticsToOperationsLog)
    {
        Logger logger;
        const std::wstring operationId = L"logger-operation-channel";
        const std::string marker = "operation-diagnostics-marker operationIdFieldCheck";

        Logger::setOperationId(operationId);
        logger.initialize();
        ASSERT_TRUE(logger.isInitialized());
        ASSERT_FALSE(logger.operationsLogPath().empty());

        logger.writeOperation(LogLevel::Info, "LoggerTests", marker);
        logger.shutdown();
        Logger::clearOperationId();

        const std::string content = readFile(logger.operationsLogPath());
        EXPECT_NE(content.find(marker), std::string::npos);
        EXPECT_NE(content.find("op=logger-operation-channel"), std::string::npos);
        EXPECT_NE(content.find("operationId=logger-operation-channel"), std::string::npos);
    }
}
