using Fluxora.App.Models;

namespace Fluxora.App.Tests.Models;

public sealed class ModEntryTests
{
    [Fact]
    public void IsEnabled_RaisesPropertyChanged()
    {
        ModEntry entry = new()
        {
            Id = "mod-1",
            Name = "SkyUI"
        };
        List<string> changedProperties = [];
        entry.PropertyChanged += (_, args) => changedProperties.Add(args.PropertyName ?? string.Empty);

        entry.IsEnabled = true;

        Assert.Contains(nameof(ModEntry.IsEnabled), changedProperties);
    }
}
