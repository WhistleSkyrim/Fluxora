using Fluxora.App.Models;
using Fluxora.App.ViewModels;

namespace Fluxora.App.Tests.ViewModels;

public sealed class ModFileTreeNodeTests
{
    [Fact]
    public void DirectoryWithChildren_StartsWithPlaceholderChild()
    {
        ModFileTreeNode node = new(new ModFileTreeEntry
        {
            Name = "textures",
            RelativePath = "meshes/textures",
            IsDirectory = true,
            HasChildren = true
        });

        Assert.True(node.IsDirectory);
        Assert.True(node.HasChildren);
        ModFileTreeNode child = Assert.Single(node.Children);
        Assert.True(child.IsPlaceholder);
        Assert.Equal("Загрузка...", child.Name);
    }

    [Fact]
    public void FileNode_FormatsSizeAndConflictText()
    {
        ModFileTreeNode node = new(new ModFileTreeEntry
        {
            Name = string.Empty,
            RelativePath = "textures/armor.dds",
            Size = 1536,
            ConflictState = "conflict",
            ConflictOwners = ["SkyUI", "USSEP"]
        });

        Assert.Equal("textures/armor.dds", node.Name);
        Assert.Matches(@"^1[,.]5 KB$", node.SizeText);
        Assert.True(node.HasConflict);
        Assert.Equal("Конфликт · SkyUI, USSEP", node.ConflictText);
    }
}
