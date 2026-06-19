using Fluxora.App.Models;
using Fluxora.App.ViewModels;

namespace Fluxora.App.Tests.ViewModels;

public sealed class FomodInstallerViewModelTests
{
    [Fact]
    public void Constructor_SelectsRecommendedOptionForExactlyOneGroup()
    {
        FomodInstallerViewModel viewModel = new(CreateInstaller());

        FomodGroupViewModel group = viewModel.Steps[0].Groups[0];

        Assert.True(group.Options[0].IsSelected);
        Assert.False(group.Options[1].IsSelected);
        Assert.True(viewModel.CanMoveNext);
    }

    [Fact]
    public void Constructor_RequiresExplicitReadAcknowledgement()
    {
        FomodInstallerViewModel viewModel = new(CreateReadInstaller());

        FomodOptionViewModel acknowledgement = viewModel.Steps[0].Groups[0].Options[0];

        Assert.True(acknowledgement.IsAcknowledgementOption);
        Assert.False(acknowledgement.IsSelected);
        Assert.False(viewModel.CanMoveNext);
        Assert.False(viewModel.CanPrimaryAction);
        Assert.Equal("Подтвердите, что вы прочитали информацию этого шага.", viewModel.ValidationMessage);

        acknowledgement.IsSelected = true;

        Assert.True(viewModel.CanMoveNext);
        Assert.True(viewModel.CanPrimaryAction);
        Assert.Equal(string.Empty, viewModel.ValidationMessage);
    }

    [Fact]
    public void Constructor_AutoSelectsRecommendedOptionFromFileDependencyState()
    {
        FomodInstallerViewModel viewModel = new(CreateFileDependencyInstaller(fileExists: true));

        FomodOptionViewModel patch = viewModel.Steps[0].Groups[0].Options[0];
        FomodOptionViewModel baseOption = viewModel.Steps[0].Groups[0].Options[1];

        Assert.True(patch.IsSelected);
        Assert.False(baseOption.IsSelected);
        Assert.Equal("Рекомендовано FOMOD", patch.TypeBadgeText);
        Assert.Contains("установленным", patch.TypeExplanationText);
    }

    [Fact]
    public void SelectExactlyOne_DeselectsPreviousOption()
    {
        FomodInstallerViewModel viewModel = new(CreateInstaller());
        FomodGroupViewModel group = viewModel.Steps[0].Groups[0];

        group.Options[1].IsSelected = true;

        Assert.False(group.Options[0].IsSelected);
        Assert.True(group.Options[1].IsSelected);
        Assert.Equal(["step-one-variant-option-b"], viewModel.SelectedOptionIds);
    }

    [Fact]
    public void UsePreviousSelections_AppliesCompatiblePreviousOptionIds()
    {
        FomodInstallerInfo installer = CreateInstaller(hasPreviousSelection: true);
        FomodInstallerViewModel viewModel = new(installer);

        viewModel.UsePreviousSelectionsCommand.Execute(null);

        FomodGroupViewModel group = viewModel.Steps[0].Groups[0];
        Assert.False(group.Options[0].IsSelected);
        Assert.True(group.Options[1].IsSelected);
        Assert.True(group.Options[1].WasPreviouslySelected);
    }

    [Fact]
    public void FlagDependency_RevealsLaterStepWhenSelectedOptionSetsFlag()
    {
        FomodInstallerViewModel viewModel = new(CreateInstaller());

        Assert.Contains(viewModel.VisibleSteps, step => step.Id == "step-two");
        Assert.Contains(viewModel.NavigationSteps, step => step.Id == "step-two");
        Assert.Equal("1 / 2", viewModel.StepCounterText);

        FomodGroupViewModel group = viewModel.Steps[0].Groups[0];
        group.Options[1].IsSelected = true;

        Assert.DoesNotContain(viewModel.VisibleSteps, step => step.Id == "step-two");
        FomodStepViewModel disabledStep = Assert.Single(viewModel.NavigationSteps, step => step.Id == "step-two");
        Assert.False(disabledStep.IsVisible);
        Assert.Equal("Недоступно", disabledStep.StatusText);
        Assert.Equal("1 / 2", viewModel.StepCounterText);
    }

    [Fact]
    public void Constructor_TargetsFirstInvalidRequiredChoiceGroup()
    {
        FomodInstallerViewModel viewModel = new(CreateRequiredChoiceInstallerWithoutDefault());

        FomodGroupViewModel group = viewModel.Steps[0].Groups[0];

        Assert.False(viewModel.CanPrimaryAction);
        Assert.Same(group, viewModel.ValidationTargetGroup);
        Assert.True(group.IsValidationTarget);
        Assert.Equal("Выберите один вариант в группе \"Required choice\".", viewModel.ValidationMessage);
    }

    [Fact]
    public void SelectingRequiredChoice_ClearsValidationTargetGroup()
    {
        FomodInstallerViewModel viewModel = new(CreateRequiredChoiceInstallerWithoutDefault());
        FomodGroupViewModel group = viewModel.Steps[0].Groups[0];

        group.Options[1].IsSelected = true;

        Assert.True(viewModel.CanPrimaryAction);
        Assert.Null(viewModel.ValidationTargetGroup);
        Assert.False(group.IsValidationTarget);
        Assert.Equal(string.Empty, viewModel.ValidationMessage);
    }

    [Fact]
    public void ValidationTarget_UsesFirstInvalidGroup()
    {
        FomodInstallerViewModel viewModel = new(CreateMultipleInvalidGroupsInstaller());

        FomodGroupViewModel first = viewModel.Steps[0].Groups[0];
        FomodGroupViewModel second = viewModel.Steps[0].Groups[1];

        Assert.Same(first, viewModel.ValidationTargetGroup);
        Assert.True(first.IsValidationTarget);
        Assert.False(second.IsValidationTarget);

        first.Options[0].IsSelected = true;

        Assert.Same(second, viewModel.ValidationTargetGroup);
        Assert.False(first.IsValidationTarget);
        Assert.True(second.IsValidationTarget);
    }

    [Fact]
    public void OptionPreviewImage_UsesOptionImageThenModuleFallback()
    {
        FomodInstallerViewModel viewModel = new(CreateInstallerWithImages());

        FomodOptionViewModel optionWithImage = viewModel.Steps[0].Groups[0].Options[0];
        FomodOptionViewModel optionWithoutImage = viewModel.Steps[0].Groups[0].Options[1];

        Assert.Equal("C:\\previews\\option-a.png", optionWithImage.PreviewImagePath);
        Assert.True(optionWithImage.HasPreviewImage);
        Assert.Equal("C:\\previews\\module.png", optionWithoutImage.PreviewImagePath);
        Assert.True(optionWithoutImage.HasPreviewImage);
    }

    private static FomodInstallerInfo CreateInstaller(bool hasPreviousSelection = false)
    {
        return new FomodInstallerInfo
        {
            IsFomod = true,
            ModuleName = "Example",
            HasPreviousSelection = hasPreviousSelection,
            PreviousSelectedOptionIds = hasPreviousSelection
                ? ["step-one-variant-option-b"]
                : [],
            Steps =
            {
                new FomodStepInfo
                {
                    Id = "step-one",
                    Name = "Step One",
                    Groups =
                    {
                        new FomodGroupInfo
                        {
                            Id = "variant",
                            Name = "Variant",
                            Type = "SelectExactlyOne",
                            Options =
                            {
                                new FomodOptionInfo
                                {
                                    Id = "step-one-variant-option-a",
                                    Name = "Option A",
                                    Type = "Recommended",
                                    DefaultType = "Recommended",
                                    Flags =
                                    {
                                        new FomodConditionFlagInfo
                                        {
                                            Name = "variant",
                                            Value = "A"
                                        }
                                    }
                                },
                                new FomodOptionInfo
                                {
                                    Id = "step-one-variant-option-b",
                                    Name = "Option B",
                                    Type = "Optional",
                                    DefaultType = "Optional",
                                    Flags =
                                    {
                                        new FomodConditionFlagInfo
                                        {
                                            Name = "variant",
                                            Value = "B"
                                        }
                                    }
                                }
                            }
                        }
                    }
                },
                new FomodStepInfo
                {
                    Id = "step-two",
                    Name = "Step Two",
                    Visible = new FomodDependencyInfo
                    {
                        Kind = "flag",
                        Flag = "variant",
                        Value = "A"
                    },
                    Groups =
                    {
                        new FomodGroupInfo
                        {
                            Id = "extras",
                            Name = "Extras",
                            Type = "SelectAny",
                            Options =
                            {
                                new FomodOptionInfo
                                {
                                    Id = "step-two-extras-patch",
                                    Name = "Patch"
                                }
                            }
                        }
                    }
                }
            }
        };
    }

    private static FomodInstallerInfo CreateReadInstaller()
    {
        return new FomodInstallerInfo
        {
            IsFomod = true,
            ModuleName = "Read Me",
            Steps =
            {
                new FomodStepInfo
                {
                    Id = "read",
                    Name = "Read",
                    Groups =
                    {
                        new FomodGroupInfo
                        {
                            Id = "ack",
                            Name = "Important Information",
                            Type = "SelectAny",
                            Options =
                            {
                                new FomodOptionInfo
                                {
                                    Id = "read-ack",
                                    Name = "I Have Read and Understand",
                                    Description = "Read this before continuing.",
                                    Type = "Optional",
                                    DefaultType = "Optional"
                                }
                            }
                        }
                    }
                },
                new FomodStepInfo
                {
                    Id = "choose",
                    Name = "Choose",
                    Groups =
                    {
                        new FomodGroupInfo
                        {
                            Id = "optional",
                            Name = "Optional",
                            Type = "SelectAny"
                        }
                    }
                }
            }
        };
    }

    private static FomodInstallerInfo CreateRequiredChoiceInstallerWithoutDefault()
    {
        return new FomodInstallerInfo
        {
            IsFomod = true,
            ModuleName = "Required choice",
            Steps =
            {
                new FomodStepInfo
                {
                    Id = "required",
                    Name = "Required",
                    Groups =
                    {
                        CreateRequiredChoiceGroup("required-choice", "Required choice")
                    }
                }
            }
        };
    }

    private static FomodInstallerInfo CreateMultipleInvalidGroupsInstaller()
    {
        return new FomodInstallerInfo
        {
            IsFomod = true,
            ModuleName = "Multiple choices",
            Steps =
            {
                new FomodStepInfo
                {
                    Id = "required",
                    Name = "Required",
                    Groups =
                    {
                        CreateRequiredChoiceGroup("first", "First"),
                        CreateRequiredChoiceGroup("second", "Second")
                    }
                }
            }
        };
    }

    private static FomodInstallerInfo CreateInstallerWithImages()
    {
        return new FomodInstallerInfo
        {
            IsFomod = true,
            ModuleName = "Images",
            ModuleImagePath = "C:\\previews\\module.png",
            Steps =
            {
                new FomodStepInfo
                {
                    Id = "images",
                    Name = "Images",
                    Groups =
                    {
                        new FomodGroupInfo
                        {
                            Id = "options",
                            Name = "Options",
                            Type = "SelectAny",
                            Options =
                            {
                                new FomodOptionInfo
                                {
                                    Id = "option-a",
                                    Name = "Option A",
                                    ImagePath = "C:\\previews\\option-a.png"
                                },
                                new FomodOptionInfo
                                {
                                    Id = "option-b",
                                    Name = "Option B"
                                }
                            }
                        }
                    }
                }
            }
        };
    }

    private static FomodGroupInfo CreateRequiredChoiceGroup(string id, string name)
    {
        return new FomodGroupInfo
        {
            Id = id,
            Name = name,
            Type = "SelectExactlyOne",
            Options =
            {
                new FomodOptionInfo
                {
                    Id = id + "-a",
                    Name = "Option A",
                    Type = "Optional",
                    DefaultType = "Optional"
                },
                new FomodOptionInfo
                {
                    Id = id + "-b",
                    Name = "Option B",
                    Type = "Optional",
                    DefaultType = "Optional"
                }
            }
        };
    }

    private static FomodInstallerInfo CreateFileDependencyInstaller(bool fileExists)
    {
        return new FomodInstallerInfo
        {
            IsFomod = true,
            ModuleName = "Detected Patch",
            FileDependencies =
            {
                new FomodFileDependencyStateInfo
                {
                    File = "Data/Lanterns Of Skyrim II.esp",
                    Exists = fileExists
                }
            },
            Steps =
            {
                new FomodStepInfo
                {
                    Id = "patches",
                    Name = "Patches",
                    Groups =
                    {
                        new FomodGroupInfo
                        {
                            Id = "lanterns",
                            Name = "Lanterns",
                            Type = "SelectExactlyOne",
                            Options =
                            {
                                new FomodOptionInfo
                                {
                                    Id = "patch",
                                    Name = "Lanterns patch",
                                    Type = "Optional",
                                    DefaultType = "Optional",
                                    TypePatterns =
                                    {
                                        new FomodTypePatternInfo
                                        {
                                            Type = "Recommended",
                                            Dependencies = new FomodDependencyInfo
                                            {
                                                Kind = "file",
                                                File = "Data\\Lanterns Of Skyrim II.esp",
                                                State = "Active"
                                            }
                                        }
                                    }
                                },
                                new FomodOptionInfo
                                {
                                    Id = "base",
                                    Name = "No patch",
                                    Type = "Optional",
                                    DefaultType = "Optional"
                                }
                            }
                        }
                    }
                }
            }
        };
    }
}
