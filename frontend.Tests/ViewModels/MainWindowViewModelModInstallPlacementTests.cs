using Fluxora.App.Models;
using Fluxora.App.Services;
using Fluxora.App.ViewModels;

namespace Fluxora.App.Tests.ViewModels;

public sealed class MainWindowViewModelModInstallPlacementTests
{
    [Fact]
    public void ExistingModInstallModeValuesMatchNativeApiContract()
    {
        Assert.Equal(0, (int)ExistingModInstallMode.FailIfExists);
        Assert.Equal(1, (int)ExistingModInstallMode.Replace);
        Assert.Equal(2, (int)ExistingModInstallMode.Merge);
    }

    [Fact]
    public void FindInstalledModByName_MatchesInstalledModCaseInsensitively()
    {
        ModEntry separator = new()
        {
            Id = "separator-1",
            Kind = "separator",
            Name = "SkyUI"
        };
        ModEntry installed = new()
        {
            Id = @"C:\Fluxora\TestProject\mods\SkyUI",
            Name = "SkyUI"
        };

        ModEntry? resolved = MainWindowViewModel.FindInstalledModByName(
            [separator, installed],
            "  skyui  ");

        Assert.Same(installed, resolved);
    }

    [Fact]
    public void FindInstalledModByName_ReturnsNullWhenOnlySeparatorMatches()
    {
        ModEntry separator = new()
        {
            Id = "separator-1",
            Kind = "separator",
            Name = "Gameplay"
        };

        ModEntry? resolved = MainWindowViewModel.FindInstalledModByName(
            [separator],
            "Gameplay");

        Assert.Null(resolved);
    }

    [Fact]
    public void FindInstalledModByName_MatchesInstalledFolderName()
    {
        ModEntry installed = new()
        {
            Id = @"C:\Fluxora\TestProject\mods\Real Folder Name",
            Name = "Pretty Display Name"
        };

        ModEntry? resolved = MainWindowViewModel.FindInstalledModByName(
            [installed],
            "Real Folder Name");

        Assert.Same(installed, resolved);
    }

    [Fact]
    public void CanOpenInstalledModInExplorer_AllowsInstalledMod()
    {
        ModEntry installed = new()
        {
            Id = @"C:\Fluxora\TestProject\mods\SkyUI",
            Name = "SkyUI"
        };

        Assert.True(MainWindowViewModel.CanOpenInstalledModInExplorer(installed));
    }

    [Fact]
    public void CanOpenInstalledModInExplorer_RejectsSeparator()
    {
        ModEntry separator = new()
        {
            Id = "separator-1",
            Kind = "separator",
            Name = "Gameplay"
        };

        Assert.False(MainWindowViewModel.CanOpenInstalledModInExplorer(separator));
    }

    [Fact]
    public void ResolveInstalledModOrderEntry_UsesFreshProfileOrderId()
    {
        ModEntry installedMod = new()
        {
            Id = @"C:\Fluxora\TestProject\mods\SkyUI",
            Name = "SkyUI"
        };
        ModEntry freshOrderEntry = new()
        {
            Id = @"c:\fluxora\testproject\mods\skyui",
            OrderId = "profile-order-skyui",
            Name = "SkyUI"
        };

        ModEntry resolved = MainWindowViewModel.ResolveInstalledModOrderEntry(
            [freshOrderEntry],
            installedMod);

        Assert.Same(freshOrderEntry, resolved);
        Assert.Equal("profile-order-skyui", resolved.OrderId);
    }

    [Fact]
    public void ResolveInstalledModOrderEntry_RejectsEntryWithoutOrderId()
    {
        ModEntry installedMod = new()
        {
            Id = @"C:\Fluxora\TestProject\mods\SkyUI",
            Name = "SkyUI"
        };
        ModEntry staleEntry = new()
        {
            Id = installedMod.Id,
            Name = "SkyUI"
        };

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(
            () => MainWindowViewModel.ResolveInstalledModOrderEntry([staleEntry], installedMod));

        Assert.Equal("Installed mod profile order item was not found.", exception.Message);
    }

    [Fact]
    public void ResolveFomodInstallName_UsesModuleNameBeforeDownloadName()
    {
        FomodInstallerInfo installer = new()
        {
            IsFomod = true,
            ModuleName = "  Northern Roads  "
        };
        DownloadEntry download = new()
        {
            Id = "download-1",
            Name = "Archive Name"
        };

        string modName = MainWindowViewModel.ResolveFomodInstallName(installer, download);

        Assert.Equal("Northern Roads", modName);
    }

    [Fact]
    public void ResolveFomodInstallName_FallsBackToDownloadFileName()
    {
        FomodInstallerInfo installer = new()
        {
            IsFomod = true
        };
        DownloadEntry download = new()
        {
            Id = "download-1",
            Name = " ",
            FileName = "patcher.fomod"
        };

        string modName = MainWindowViewModel.ResolveFomodInstallName(installer, download);

        Assert.Equal("patcher", modName);
    }
}
