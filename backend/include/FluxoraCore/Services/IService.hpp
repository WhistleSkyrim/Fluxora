#pragma once

namespace fluxora
{
    class IService
    {
    public:
        virtual ~IService() = default;

        virtual void initialize() = 0;
        virtual void shutdown() = 0;
    };
}
