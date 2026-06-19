#include "FluxoraCore/Services/HookService.hpp"

#include "FluxoraCore/Services/Logger.hpp"

#include <gtest/gtest.h>

namespace fluxora::tests
{
    TEST(HookServiceTests, InitializeAndShutdownAreIdempotent)
    {
        Logger logger;
        HookService service(logger);

        EXPECT_FALSE(service.isInitialized());

        service.initialize();
        service.initialize();
        EXPECT_TRUE(service.isInitialized());

        service.shutdown();
        service.shutdown();
        EXPECT_FALSE(service.isInitialized());
    }
}
