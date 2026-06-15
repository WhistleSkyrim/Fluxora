#include "FluxoraCore/Services/HookService.hpp"

#include "FluxoraCore/Services/Logger.hpp"

namespace fluxora
{
    HookService::HookService(Logger& logger) noexcept
        : logger_(logger)
    {
    }

    void HookService::initialize()
    {
        if (initialized_)
        {
            return;
        }

        initialized_ = true;
        logger_.write(LogLevel::Info, "Hook service initialized.");
    }

    void HookService::shutdown()
    {
        if (!initialized_)
        {
            return;
        }

        logger_.write(LogLevel::Info, "Hook service shut down.");
        initialized_ = false;
    }

    bool HookService::isInitialized() const noexcept
    {
        return initialized_;
    }
}
