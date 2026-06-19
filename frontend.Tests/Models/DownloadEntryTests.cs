using Fluxora.App.Models;

namespace Fluxora.App.Tests.Models;

public sealed class DownloadEntryTests
{
    [Fact]
    public void DownloadingWithoutKnownProgress_UsesIndeterminateDisplay()
    {
        DownloadEntry entry = new()
        {
            Id = "download-1",
            Name = "SkyUI",
            IsDownloading = true,
            HasKnownProgress = false
        };

        Assert.True(entry.HasProgressDisplay);
        Assert.False(entry.HasProgressHeader);
        Assert.True(entry.IsProgressIndeterminate);
        Assert.Equal(string.Empty, entry.ProgressPercentText);
    }

    [Fact]
    public void ResumableKnownProgress_ShowsPercentAndCaption()
    {
        DownloadEntry entry = new()
        {
            Id = "download-2",
            Name = "USSEP",
            CanResume = true,
            HasKnownProgress = true,
            ProgressPercent = 64,
            ProgressText = "Пауза"
        };

        Assert.True(entry.HasProgressDisplay);
        Assert.True(entry.HasProgressHeader);
        Assert.True(entry.HasProgressCaption);
        Assert.False(entry.IsProgressIndeterminate);
        Assert.Equal("64%", entry.ProgressPercentText);
    }
}
