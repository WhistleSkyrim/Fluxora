#pragma once

#include "FluxoraCore/Services/IService.hpp"

namespace fluxora
{
    class Logger;

    class HookService final : public IService
    {
    public:
        explicit HookService(Logger& logger) noexcept;

        void initialize() override;
        void shutdown() override;

        [[nodiscard]] bool isInitialized() const noexcept;

    private:
        Logger& logger_;
        bool initialized_{false};
    };
}
