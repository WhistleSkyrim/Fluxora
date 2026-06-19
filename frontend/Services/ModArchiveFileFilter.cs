using System.IO;

namespace Fluxora.App.Services;

public static class ModArchiveFileFilter
{
    private static readonly string[] CompoundExtensions =
    {
        ".tar.gz",
        ".tar.bz2",
        ".tar.xz",
        ".tar.zst",
        ".7z.001"
    };

    private static readonly HashSet<string> SupportedExtensions = new(StringComparer.OrdinalIgnoreCase)
    {
        ".zip",
        ".7z",
        ".7z.001",
        ".rar",
        ".fomod",
        ".omod",
        ".tar",
        ".tar.gz",
        ".tgz",
        ".tar.bz2",
        ".tbz",
        ".tbz2",
        ".tar.xz",
        ".txz",
        ".tar.zst",
        ".gz",
        ".bz2",
        ".xz",
        ".zst",
        ".cab",
        ".iso",
        ".wim",
        ".arj",
        ".lzh",
        ".lha",
        ".ba2",
        ".bsa"
    };

    public static bool IsSupportedFileName(string fileName)
    {
        return SupportedExtensions.Contains(GetArchiveExtension(fileName));
    }

    public static IReadOnlyList<string> GetSupportedExistingFiles(IEnumerable<string> paths)
    {
        return paths
            .Where(path => !string.IsNullOrWhiteSpace(path))
            .Where(File.Exists)
            .Where(IsSupportedFileName)
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToList();
    }

    private static string GetArchiveExtension(string fileName)
    {
        string lowerFileName = fileName.ToLowerInvariant();
        foreach (string extension in CompoundExtensions)
        {
            if (lowerFileName.EndsWith(extension, StringComparison.Ordinal))
            {
                return extension;
            }
        }

        return Path.GetExtension(fileName);
    }
}
