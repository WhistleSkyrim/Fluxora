using System.IO;
using Fluxora.App.Services;

namespace Fluxora.App.Tests.Services;

public sealed class ModArchiveFileFilterTests : IDisposable
{
    private readonly string tempDirectory = Path.Combine(Path.GetTempPath(), $"fluxora-filter-tests-{Guid.NewGuid():N}");

    public ModArchiveFileFilterTests()
    {
        Directory.CreateDirectory(tempDirectory);
    }

    [Theory]
    [InlineData("mod.zip")]
    [InlineData("mod.fomod")]
    [InlineData("mod.omod")]
    [InlineData("mod.tar.gz")]
    [InlineData("mod.7z.001")]
    [InlineData("Skyrim - Textures.ba2")]
    [InlineData("Skyrim - Meshes.bsa")]
    public void IsSupportedFileName_AcceptsModArchiveExtensions(string fileName)
    {
        Assert.True(ModArchiveFileFilter.IsSupportedFileName(fileName));
    }

    [Theory]
    [InlineData("notes.txt")]
    [InlineData("image.png")]
    [InlineData("archive.zip.tmp")]
    public void IsSupportedFileName_RejectsNonArchiveExtensions(string fileName)
    {
        Assert.False(ModArchiveFileFilter.IsSupportedFileName(fileName));
    }

    [Fact]
    public void GetSupportedExistingFiles_ReturnsOnlyExistingSupportedDistinctFiles()
    {
        string archivePath = Path.Combine(tempDirectory, "mod.tar.gz");
        string unsupportedPath = Path.Combine(tempDirectory, "notes.txt");
        File.WriteAllText(archivePath, "archive");
        File.WriteAllText(unsupportedPath, "notes");

        IReadOnlyList<string> files = ModArchiveFileFilter.GetSupportedExistingFiles(new[]
        {
            archivePath,
            archivePath.ToUpperInvariant(),
            unsupportedPath,
            Path.Combine(tempDirectory, "missing.zip")
        });

        string file = Assert.Single(files);
        Assert.Equal(archivePath, file);
    }

    public void Dispose()
    {
        if (Directory.Exists(tempDirectory))
        {
            Directory.Delete(tempDirectory, recursive: true);
        }
    }
}
