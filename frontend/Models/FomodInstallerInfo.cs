namespace Fluxora.App.Models;

public sealed class FomodInstallerInfo
{
    public bool IsFomod { get; init; }
    public string ModuleName { get; init; } = string.Empty;
    public string ModuleVersion { get; init; } = string.Empty;
    public string ModuleId { get; init; } = string.Empty;
    public string ModuleImagePath { get; init; } = string.Empty;
    public string MemoryKey { get; init; } = string.Empty;
    public bool HasPreviousSelection { get; init; }
    public List<string> PreviousSelectedOptionIds { get; init; } = new();
    public List<FomodFileDependencyStateInfo> FileDependencies { get; init; } = new();
    public List<FomodStepInfo> Steps { get; init; } = new();
}

public sealed class FomodFileDependencyStateInfo
{
    public string File { get; init; } = string.Empty;
    public bool Exists { get; init; }
}

public sealed class FomodStepInfo
{
    public string Id { get; init; } = string.Empty;
    public string Name { get; init; } = string.Empty;
    public FomodDependencyInfo? Visible { get; init; }
    public List<FomodGroupInfo> Groups { get; init; } = new();
}

public sealed class FomodGroupInfo
{
    public string Id { get; init; } = string.Empty;
    public string Name { get; init; } = string.Empty;
    public string Type { get; init; } = "SelectAny";
    public List<FomodOptionInfo> Options { get; init; } = new();
}

public sealed class FomodOptionInfo
{
    public string Id { get; init; } = string.Empty;
    public string Name { get; init; } = string.Empty;
    public string Description { get; init; } = string.Empty;
    public string ImagePath { get; init; } = string.Empty;
    public string Type { get; init; } = "Optional";
    public string DefaultType { get; init; } = "Optional";
    public List<FomodConditionFlagInfo> Flags { get; init; } = new();
    public List<FomodTypePatternInfo> TypePatterns { get; init; } = new();
}

public sealed class FomodConditionFlagInfo
{
    public string Name { get; init; } = string.Empty;
    public string Value { get; init; } = string.Empty;
}

public sealed class FomodTypePatternInfo
{
    public FomodDependencyInfo? Dependencies { get; init; }
    public string Type { get; init; } = "Optional";
}

public sealed class FomodDependencyInfo
{
    public string Kind { get; init; } = string.Empty;
    public string Operator { get; init; } = "And";
    public string File { get; init; } = string.Empty;
    public string State { get; init; } = string.Empty;
    public string Flag { get; init; } = string.Empty;
    public string Value { get; init; } = string.Empty;
    public string Version { get; init; } = string.Empty;
    public List<FomodDependencyInfo> Children { get; init; } = new();
}
