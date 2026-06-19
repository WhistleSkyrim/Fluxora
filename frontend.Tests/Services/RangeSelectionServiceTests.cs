using Fluxora.App.Services;

namespace Fluxora.App.Tests.Services;

public sealed class RangeSelectionServiceTests
{
    [Fact]
    public void Extend_AddsRangesWithoutClearingPreviousSelections()
    {
        List<SelectableItem> items =
        [
            new("a"),
            new("b"),
            new("c"),
            new("d"),
            new("e"),
            new("f")
        ];

        string? anchor = RangeSelectionService.Apply(
            items,
            items[0],
            item => item.Id,
            item => item.IsSelected,
            static (item, selected) => item.IsSelected = selected,
            null,
            RangeSelectionGesture.Replace);
        anchor = RangeSelectionService.Apply(
            items,
            items[2],
            item => item.Id,
            item => item.IsSelected,
            static (item, selected) => item.IsSelected = selected,
            anchor,
            RangeSelectionGesture.Extend);
        anchor = RangeSelectionService.Apply(
            items,
            items[4],
            item => item.Id,
            item => item.IsSelected,
            static (item, selected) => item.IsSelected = selected,
            anchor,
            RangeSelectionGesture.Toggle);
        anchor = RangeSelectionService.Apply(
            items,
            items[5],
            item => item.Id,
            item => item.IsSelected,
            static (item, selected) => item.IsSelected = selected,
            anchor,
            RangeSelectionGesture.Extend);

        Assert.Equal("f", anchor);
        Assert.Equal(["a", "b", "c", "e", "f"], SelectedIds(items));
    }

    [Fact]
    public void Toggle_RemovesSelectedItemAndUsesItAsNextAnchor()
    {
        List<SelectableItem> items = [new("a"), new("b"), new("c")];
        string? anchor = RangeSelectionService.SelectAll(
            items,
            item => item.Id,
            static (item, selected) => item.IsSelected = selected);

        anchor = RangeSelectionService.Apply(
            items,
            items[1],
            item => item.Id,
            item => item.IsSelected,
            static (item, selected) => item.IsSelected = selected,
            anchor,
            RangeSelectionGesture.Toggle);

        Assert.Equal("b", anchor);
        Assert.Equal(["a", "c"], SelectedIds(items));
    }

    [Fact]
    public void Replace_ClearsPreviousSelection()
    {
        List<SelectableItem> items = [new("a"), new("b"), new("c")];
        items[0].IsSelected = true;
        items[2].IsSelected = true;

        string? anchor = RangeSelectionService.Apply(
            items,
            items[1],
            item => item.Id,
            item => item.IsSelected,
            static (item, selected) => item.IsSelected = selected,
            "a",
            RangeSelectionGesture.Replace);

        Assert.Equal("b", anchor);
        Assert.Equal(["b"], SelectedIds(items));
    }

    [Fact]
    public void Extend_MissingAnchorSelectsClickedItemOnly()
    {
        List<SelectableItem> items = [new("a"), new("b"), new("c")];

        string? anchor = RangeSelectionService.Apply(
            items,
            items[1],
            item => item.Id,
            item => item.IsSelected,
            static (item, selected) => item.IsSelected = selected,
            "missing",
            RangeSelectionGesture.Extend);

        Assert.Equal("b", anchor);
        Assert.Equal(["b"], SelectedIds(items));
    }

    [Fact]
    public void SelectAll_PreservesPreferredAnchorWhenItExists()
    {
        List<SelectableItem> items = [new("a"), new("b"), new("c")];

        string? anchor = RangeSelectionService.SelectAll(
            items,
            item => item.Id,
            static (item, selected) => item.IsSelected = selected,
            "b");

        Assert.Equal("b", anchor);
        Assert.Equal(["a", "b", "c"], SelectedIds(items));
    }

    private static string[] SelectedIds(IEnumerable<SelectableItem> items)
    {
        return items.Where(item => item.IsSelected).Select(item => item.Id).ToArray();
    }

    private sealed class SelectableItem
    {
        public SelectableItem(string id)
        {
            Id = id;
        }

        public string Id { get; }
        public bool IsSelected { get; set; }
    }
}
