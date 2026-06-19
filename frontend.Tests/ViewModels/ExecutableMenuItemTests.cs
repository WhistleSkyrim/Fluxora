using Fluxora.App.Models;
using Fluxora.App.ViewModels;

namespace Fluxora.App.Tests.ViewModels;

public sealed class ExecutableMenuItemTests
{
    [Fact]
    public void Placeholder_RepresentsEmptyLaunchList()
    {
        ExecutableMenuItem item = ExecutableMenuItem.Placeholder();

        Assert.True(item.IsPlaceholder);
        Assert.False(item.OpensManager);
        Assert.Equal("__placeholder__", item.Id);
        Assert.Equal("Нет запусков", item.DisplayName);
    }

    [Fact]
    public void FromExecutable_ProjectsEntryIntoMenuText()
    {
        GameExecutableEntry executable = new()
        {
            Id = "skse",
            DisplayName = "SKSE",
            ExecutablePath = "C:\\Games\\Skyrim\\skse64_loader.exe",
            Arguments = "-forcesteamloader",
            IconPath = "skse.ico"
        };

        ExecutableMenuItem item = ExecutableMenuItem.FromExecutable(executable);

        Assert.False(item.IsPlaceholder);
        Assert.False(item.OpensManager);
        Assert.Equal("skse", item.Id);
        Assert.Equal("SKSE", item.DisplayName);
        Assert.Equal("C:\\Games\\Skyrim\\skse64_loader.exe -forcesteamloader", item.ToolTip);
        Assert.Equal("skse.ico", item.IconPath);
    }
}
