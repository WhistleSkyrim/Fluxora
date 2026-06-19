using System.IO;
using Fluxora.App.Services;

namespace Fluxora.App.Tests.Services;

public sealed class ApplicationLogServiceTests
{
    [Fact]
    public async Task BeginOperationAddsOperationIdToUiAndOperationsLogs()
    {
        using ApplicationLogService logger = new();
        await logger.InitializeAsync(TestContext.Current.CancellationToken);
        string marker = $"operation-log-test-{Guid.NewGuid():N}";

        string operationId;
        using (ApplicationLogService.OperationLogScope operation =
            logger.BeginOperation("TestOperation", $"marker={marker}"))
        {
            operationId = operation.OperationId;
            Assert.False(string.IsNullOrWhiteSpace(operationId));
            Assert.Equal(operationId, ApplicationLogService.CurrentOperationId);

            logger.BridgeInfo("TestBridge", $"bridge-marker={marker}");
            operation.Complete($"complete-marker={marker}");
        }

        Assert.Equal(string.Empty, ApplicationLogService.CurrentOperationId);

        string uiLog = await File.ReadAllTextAsync(logger.UiLogPath, TestContext.Current.CancellationToken);
        string operationsLog = await File.ReadAllTextAsync(logger.OperationsLogPath, TestContext.Current.CancellationToken);

        Assert.Contains(marker, uiLog);
        Assert.Contains($"op={operationId}", uiLog);
        Assert.Contains(marker, operationsLog);
        Assert.Contains($"op={operationId}", operationsLog);
        Assert.Contains("TestBridge", operationsLog);
    }
}
