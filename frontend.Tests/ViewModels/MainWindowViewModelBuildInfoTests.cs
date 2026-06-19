using System.IO;
using System.Threading;
using Fluxora.App.Models;
using Fluxora.App.ViewModels;

namespace Fluxora.App.Tests.ViewModels;

public sealed class MainWindowViewModelBuildInfoTests
{
    [Fact]
    public void CountInstalledModStates_IgnoresSeparators()
    {
        (int active, int disabled) = MainWindowViewModel.CountInstalledModStates(
        [
            new ModEntry { Id = "active-1", Name = "SkyUI", IsEnabled = true },
            new ModEntry { Id = "separator-1", Name = "Visuals", Kind = "separator", IsEnabled = true },
            new ModEntry { Id = "disabled-1", Name = "ENB Helper", IsEnabled = false },
            new ModEntry { Id = "active-2", Name = "Address Library", IsEnabled = true }
        ]);

        Assert.Equal(2, active);
        Assert.Equal(1, disabled);
    }

    [Fact]
    public void ApplyAllModEnabledState_UpdatesOnlyMods()
    {
        ModEntry active = new() { Id = "active", Name = "SkyUI", IsEnabled = true };
        ModEntry disabled = new() { Id = "disabled", Name = "ENB Helper", IsEnabled = false };
        ModEntry separator = new() { Id = "separator", Name = "Visuals", Kind = "separator", IsEnabled = false };

        int changed = MainWindowViewModel.ApplyAllModEnabledState([active, disabled, separator], true);

        Assert.Equal(1, changed);
        Assert.True(active.IsEnabled);
        Assert.True(disabled.IsEnabled);
        Assert.False(separator.IsEnabled);
    }

    [Fact]
    public void FormatProjectTimestamp_UsesFallbackWhenMissing()
    {
        Assert.Equal(
            "Ещё не запускалась",
            MainWindowViewModel.FormatProjectTimestamp(null, "Ещё не запускалась"));
    }

    [Fact]
    public void FormatProjectTimestamp_FormatsLocalDateTime()
    {
        DateTimeOffset timestamp = new(2026, 6, 17, 7, 35, 0, TimeSpan.Zero);

        Assert.Equal(
            timestamp.LocalDateTime.ToString("dd.MM.yyyy HH:mm"),
            MainWindowViewModel.FormatProjectTimestamp(timestamp, "missing"));
    }

    [Fact]
    public void CalculateDirectorySize_SumsNestedFiles()
    {
        string directory = Path.Combine(Path.GetTempPath(), "FluxoraSizeTest-" + Guid.NewGuid());
        string nested = Path.Combine(directory, "mods", "SkyUI");
        Directory.CreateDirectory(nested);

        try
        {
            File.WriteAllBytes(Path.Combine(directory, "fluxora.build.json"), new byte[17]);
            File.WriteAllBytes(Path.Combine(nested, "plugin.esp"), new byte[31]);

            ulong size = MainWindowViewModel.CalculateDirectorySize(directory, CancellationToken.None);

            Assert.Equal(48UL, size);
        }
        finally
        {
            if (Directory.Exists(directory))
            {
                Directory.Delete(directory, recursive: true);
            }
        }
    }

    [Fact]
    public void ProjectSizeCacheKey_NormalizesEquivalentDirectoryText()
    {
        string directory = Path.Combine(Path.GetTempPath(), "FluxoraSizeKeyTest-" + Guid.NewGuid());
        Directory.CreateDirectory(directory);

        try
        {
            string withCurrentDirectorySegment = Path.Combine(directory, ".");
            string withTrailingSeparator = directory + Path.DirectorySeparatorChar;

            Assert.Equal(
                MainWindowViewModel.ProjectSizeCacheKey(withCurrentDirectorySegment),
                MainWindowViewModel.ProjectSizeCacheKey(withTrailingSeparator));
        }
        finally
        {
            if (Directory.Exists(directory))
            {
                Directory.Delete(directory, recursive: true);
            }
        }
    }
}
