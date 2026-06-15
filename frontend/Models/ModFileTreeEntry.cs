namespace Fluxora.App.Models;

public sealed class ModFileTreeEntry
{
    public string Name { get; init; } = string.Empty;
    public string RelativePath { get; init; } = string.Empty;
    public bool IsDirectory { get; init; }
    public bool HasChildren { get; init; }
    public ulong Size { get; init; }
    public string ConflictState { get; init; } = string.Empty;
    public IReadOnlyList<string> ConflictOwners { get; init; } = Array.Empty<string>();
}
