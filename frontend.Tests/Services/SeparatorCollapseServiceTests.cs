using Fluxora.App.Models;
using Fluxora.App.Services;

namespace Fluxora.App.Tests.Services;

public sealed class SeparatorCollapseServiceTests
{
    [Fact]
    public void Toggle_CollapsesChildrenUntilNextSeparator()
    {
        SeparatorCollapseService service = new();
        List<ModEntry> rows =
        [
            Separator("visuals"),
            Mod("skyui"),
            Mod("textures"),
            Separator("gameplay"),
            Mod("ordinator")
        ];

        service.Toggle(rows[0]);
        service.Apply(rows);

        Assert.True(rows[0].IsCollapsed);
        Assert.False(rows[0].IsHidden);
        Assert.True(rows[1].IsHidden);
        Assert.True(rows[2].IsHidden);
        Assert.False(rows[3].IsHidden);
        Assert.False(rows[4].IsHidden);
        Assert.True(rows[4].IsUnderSeparator);
    }

    [Fact]
    public void Toggle_IgnoresNonSeparators()
    {
        SeparatorCollapseService service = new();
        List<ModEntry> rows = [Mod("skyui")];

        service.Toggle(rows[0]);
        service.Apply(rows);

        Assert.False(rows[0].IsHidden);
        Assert.False(rows[0].IsCollapsed);
        Assert.False(rows[0].IsUnderSeparator);
    }

    private static ModEntry Separator(string id)
    {
        return new ModEntry
        {
            Id = id,
            Kind = "separator",
            Name = id,
            SeparatorTitle = id
        };
    }

    private static ModEntry Mod(string id)
    {
        return new ModEntry
        {
            Id = id,
            Name = id
        };
    }
}
