using System.IO;
using System.Xml;
using System.Xml.Linq;

namespace Fluxora.App.Tests.Views;

public sealed class ProgressBarBindingModeTests
{
    [Fact]
    public void ProgressBars_UseOneWayValueBindings()
    {
        DirectoryInfo repositoryRoot = FindRepositoryRoot();
        string frontendDirectory = Path.Combine(repositoryRoot.FullName, "frontend");
        List<string> offenders = new();

        foreach (string xamlPath in Directory.EnumerateFiles(frontendDirectory, "*.xaml", SearchOption.AllDirectories))
        {
            string relativePath = Path.GetRelativePath(repositoryRoot.FullName, xamlPath);
            if (IsGeneratedPath(relativePath))
            {
                continue;
            }

            XDocument document = XDocument.Load(xamlPath, LoadOptions.SetLineInfo);
            foreach (XElement progressBar in document.Descendants().Where(element => element.Name.LocalName == "ProgressBar"))
            {
                XAttribute? valueAttribute = progressBar.Attributes().FirstOrDefault(attribute => attribute.Name.LocalName == "Value");
                if (valueAttribute is null)
                {
                    continue;
                }

                string binding = valueAttribute.Value.Trim();
                if (!binding.StartsWith("{Binding", StringComparison.Ordinal) ||
                    binding.Contains("Mode=OneWay", StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }

                IXmlLineInfo lineInfo = valueAttribute;
                offenders.Add($"{relativePath}:{lineInfo.LineNumber} -> {binding}");
            }
        }

        Assert.True(
            offenders.Count == 0,
            "ProgressBar.Value binds TwoWay by default. Display-only progress bars must use Mode=OneWay:" +
            Environment.NewLine +
            string.Join(Environment.NewLine, offenders));
    }

    private static DirectoryInfo FindRepositoryRoot()
    {
        DirectoryInfo? directory = new(AppContext.BaseDirectory);
        while (directory is not null)
        {
            if (Directory.Exists(Path.Combine(directory.FullName, "frontend")) &&
                Directory.Exists(Path.Combine(directory.FullName, "frontend.Tests")))
            {
                return directory;
            }

            directory = directory.Parent;
        }

        throw new DirectoryNotFoundException("Could not find Fluxora repository root.");
    }

    private static bool IsGeneratedPath(string relativePath)
    {
        string normalized = relativePath.Replace(Path.AltDirectorySeparatorChar, Path.DirectorySeparatorChar);
        return normalized.Contains($"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}", StringComparison.OrdinalIgnoreCase) ||
            normalized.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}", StringComparison.OrdinalIgnoreCase);
    }
}
