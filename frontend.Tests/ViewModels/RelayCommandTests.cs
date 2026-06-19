using Fluxora.App.ViewModels;

namespace Fluxora.App.Tests.ViewModels;

public sealed class RelayCommandTests
{
    [Fact]
    public void RelayCommand_UsesCanExecuteAndRaisesCanExecuteChanged()
    {
        bool canExecute = false;
        int executions = 0;
        int canExecuteChanges = 0;
        RelayCommand command = new(() => executions++, () => canExecute);
        command.CanExecuteChanged += (_, _) => canExecuteChanges++;

        Assert.False(command.CanExecute(null));

        canExecute = true;
        command.RaiseCanExecuteChanged();
        command.Execute(null);

        Assert.True(command.CanExecute(null));
        Assert.Equal(1, executions);
        Assert.Equal(1, canExecuteChanges);
    }

    [Fact]
    public void RelayCommandOfT_PassesMatchingParameterAndDefaultsWrongType()
    {
        string? received = "not executed";
        RelayCommand<string> command = new(value => received = value);

        command.Execute("Skyrim.esm");
        Assert.Equal("Skyrim.esm", received);

        command.Execute(42);
        Assert.Null(received);
    }

    [Fact]
    public async Task AsyncRelayCommand_BlocksSecondExecuteWhileRunning()
    {
        TaskCompletionSource started = new(TaskCreationOptions.RunContinuationsAsynchronously);
        TaskCompletionSource finish = new(TaskCreationOptions.RunContinuationsAsynchronously);
        TaskCompletionSource becameDisabled = new(TaskCreationOptions.RunContinuationsAsynchronously);
        TaskCompletionSource becameEnabled = new(TaskCreationOptions.RunContinuationsAsynchronously);
        int executions = 0;

        AsyncRelayCommand command = new(async () =>
        {
            executions++;
            started.SetResult();
            await finish.Task;
        });

        command.CanExecuteChanged += (_, _) =>
        {
            if (command.CanExecute(null))
            {
                becameEnabled.TrySetResult();
            }
            else
            {
                becameDisabled.TrySetResult();
            }
        };

        command.Execute(null);
        await started.Task.WaitAsync(TimeSpan.FromSeconds(1), TestContext.Current.CancellationToken);
        await becameDisabled.Task.WaitAsync(TimeSpan.FromSeconds(1), TestContext.Current.CancellationToken);

        Assert.False(command.CanExecute(null));

        command.Execute(null);
        Assert.Equal(1, executions);

        finish.SetResult();
        await becameEnabled.Task.WaitAsync(TimeSpan.FromSeconds(1), TestContext.Current.CancellationToken);

        Assert.True(command.CanExecute(null));
        Assert.Equal(1, executions);
    }
}
