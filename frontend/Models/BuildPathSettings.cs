using System.IO;
using System.Text.Json.Serialization;

namespace Fluxora.App.Models;

public sealed class BuildPathSettings
{
    [JsonPropertyName("gameDirectory")]
    public string GameDirectory { get; set; } = string.Empty;

    [JsonPropertyName("modsDirectory")]
    public string ModsDirectory { get; set; } = string.Empty;

    [JsonPropertyName("profilesDirectory")]
    public string ProfilesDirectory { get; set; } = string.Empty;

    [JsonPropertyName("downloadsDirectory")]
    public string DownloadsDirectory { get; set; } = string.Empty;

    [JsonPropertyName("overwriteDirectory")]
    public string OverwriteDirectory { get; set; } = string.Empty;

    public BuildPathSettings Clone()
    {
        return new BuildPathSettings
        {
            GameDirectory = GameDirectory,
            ModsDirectory = ModsDirectory,
            ProfilesDirectory = ProfilesDirectory,
            DownloadsDirectory = DownloadsDirectory,
            OverwriteDirectory = OverwriteDirectory
        };
    }

    public void ApplyFallbacks(string projectDirectory, string gamePath)
    {
        if (string.IsNullOrWhiteSpace(GameDirectory))
        {
            GameDirectory = gamePath;
        }

        if (string.IsNullOrWhiteSpace(ModsDirectory))
        {
            ModsDirectory = Path.Combine(projectDirectory, "mods");
        }

        if (string.IsNullOrWhiteSpace(ProfilesDirectory))
        {
            ProfilesDirectory = Path.Combine(projectDirectory, "profiles");
        }

        if (string.IsNullOrWhiteSpace(DownloadsDirectory))
        {
            DownloadsDirectory = Path.Combine(projectDirectory, "downloads");
        }

        if (string.IsNullOrWhiteSpace(OverwriteDirectory))
        {
            OverwriteDirectory = Path.Combine(projectDirectory, "overwrite");
        }
    }
}
