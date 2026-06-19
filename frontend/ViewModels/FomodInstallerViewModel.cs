using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Windows.Input;
using Fluxora.App.Models;

namespace Fluxora.App.ViewModels;

public sealed class FomodInstallerViewModel : INotifyPropertyChanged
{
    private readonly HashSet<string> previousSelectionIds;
    private readonly IReadOnlyDictionary<string, bool> fileDependencyStates;
    private int currentStepIndex;
    private bool isRecalculating;
    private FomodOptionViewModel? detailsOption;
    private FomodGroupViewModel? validationTargetGroup;
    private string validationMessage = string.Empty;

    public FomodInstallerViewModel(FomodInstallerInfo installer)
    {
        Installer = installer;
        previousSelectionIds = installer.PreviousSelectedOptionIds
            .Where(id => !string.IsNullOrWhiteSpace(id))
            .ToHashSet(StringComparer.OrdinalIgnoreCase);
        fileDependencyStates = installer.FileDependencies
            .Where(dependency => !string.IsNullOrWhiteSpace(dependency.File))
            .GroupBy(dependency => NormalizeDependencyFile(dependency.File), StringComparer.OrdinalIgnoreCase)
            .ToDictionary(
                group => group.Key,
                group => group.First().Exists,
                StringComparer.OrdinalIgnoreCase);

        foreach (FomodStepInfo step in installer.Steps)
        {
            Steps.Add(new FomodStepViewModel(this, step));
        }

        UsePreviousSelectionsCommand = new RelayCommand(UsePreviousSelections, () => HasPreviousSelection);
        PreviousStepCommand = new RelayCommand(() => MovePrevious(), () => CanMovePrevious);
        NextStepCommand = new RelayCommand(() => MoveNext(), () => CanMoveNext);

        Recalculate();
        ApplyDefaultSelections();
        Recalculate();
        EnsureDetailsOption();
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public FomodInstallerInfo Installer { get; }

    public ObservableCollection<FomodStepViewModel> Steps { get; } = new();

    public IReadOnlyList<FomodStepViewModel> NavigationSteps => Steps;

    public IReadOnlyList<FomodStepViewModel> VisibleSteps => Steps.Where(step => step.IsVisible).ToList();

    public FomodStepViewModel? CurrentStep => VisibleSteps.Count == 0
        ? null
        : VisibleSteps[Math.Clamp(currentStepIndex, 0, VisibleSteps.Count - 1)];

    public string ModuleTitle => string.IsNullOrWhiteSpace(Installer.ModuleName)
        ? "FOMOD"
        : Installer.ModuleName;

    public string ModuleSubtitle => string.IsNullOrWhiteSpace(Installer.ModuleVersion)
        ? "Настройка установки"
        : $"Версия {Installer.ModuleVersion}";

    public string ModuleImagePath => Installer.ModuleImagePath;

    public bool HasPreviousSelection => Installer.HasPreviousSelection && previousSelectionIds.Count > 0;

    public string PreviousSelectionText => HasPreviousSelection
        ? $"Найден прошлый выбор: {previousSelectionIds.Count} опц."
        : string.Empty;

    public int CurrentStepNumber => CurrentStep?.VisibleNumber ?? 0;

    public int StepCount => VisibleSteps.Count;

    public int NavigationStepCount => Steps.Count;

    public string StepCounterText => NavigationStepCount == 0
        ? "0 / 0"
        : $"{CurrentStepNumber} / {NavigationStepCount}";

    public bool CanMovePrevious => currentStepIndex > 0;

    public bool IsLastStep => StepCount == 0 || currentStepIndex >= StepCount - 1;

    public bool CanMoveNext => !IsLastStep && CanLeaveCurrentStep;

    public bool CanFinish => IsLastStep && CanLeaveCurrentStep;

    public bool CanLeaveCurrentStep => CurrentStep?.IsSelectionValid == true;

    public bool CanPrimaryAction => IsLastStep ? CanFinish : CanMoveNext;

    public string PrimaryButtonText => IsLastStep ? "Установить" : "Далее";

    public FomodOptionViewModel? DetailsOption
    {
        get => detailsOption;
        private set
        {
            if (ReferenceEquals(detailsOption, value))
            {
                return;
            }

            detailsOption = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(HasDetailsOption));
        }
    }

    public bool HasDetailsOption => DetailsOption is not null;

    public string ValidationMessage
    {
        get => validationMessage;
        private set => SetField(ref validationMessage, value);
    }

    public FomodGroupViewModel? ValidationTargetGroup
    {
        get => validationTargetGroup;
        private set
        {
            if (ReferenceEquals(validationTargetGroup, value))
            {
                return;
            }

            validationTargetGroup?.SetValidationTarget(false);
            validationTargetGroup = value;
            validationTargetGroup?.SetValidationTarget(true);
            OnPropertyChanged();
            OnPropertyChanged(nameof(HasValidationTargetGroup));
        }
    }

    public bool HasValidationTargetGroup => ValidationTargetGroup is not null;

    public ICommand UsePreviousSelectionsCommand { get; }
    public ICommand PreviousStepCommand { get; }
    public ICommand NextStepCommand { get; }

    public IReadOnlyList<string> SelectedOptionIds => Steps
        .Where(step => step.IsVisible)
        .SelectMany(step => step.Groups)
        .SelectMany(group => group.Options)
        .Where(option => option.IsSelected)
        .Select(option => option.Id)
        .Distinct(StringComparer.OrdinalIgnoreCase)
        .ToList();

    public bool IsPreviouslySelected(string optionId)
    {
        return previousSelectionIds.Contains(optionId);
    }

    public void ShowOptionDetails(FomodOptionViewModel? option)
    {
        if (option is null)
        {
            return;
        }

        DetailsOption = option;
    }

    public void NotifyOptionChanged()
    {
        if (isRecalculating)
        {
            return;
        }

        Recalculate();
    }

    public bool MoveNext()
    {
        if (!CanMoveNext)
        {
            UpdateValidationMessage();
            return false;
        }

        currentStepIndex++;
        RaiseNavigationStateChanged();
        EnsureDetailsOption();
        return true;
    }

    public bool MovePrevious()
    {
        if (!CanMovePrevious)
        {
            return false;
        }

        currentStepIndex--;
        RaiseNavigationStateChanged();
        EnsureDetailsOption();
        return true;
    }

    public bool TryFinish()
    {
        Recalculate();
        UpdateValidationMessage();
        return CanFinish;
    }

    private void UsePreviousSelections()
    {
        if (!HasPreviousSelection)
        {
            return;
        }

        isRecalculating = true;
        try
        {
            foreach (FomodGroupViewModel group in Steps.SelectMany(step => step.Groups))
            {
                group.ClearUserSelections();
            }

            foreach (FomodOptionViewModel option in Steps
                         .SelectMany(step => step.Groups)
                         .SelectMany(group => group.Options)
                         .Where(option => previousSelectionIds.Contains(option.Id)))
            {
                option.Owner.SelectOption(option, true);
            }
        }
        finally
        {
            isRecalculating = false;
        }

        Recalculate();
        currentStepIndex = 0;
        RaiseNavigationStateChanged();
        EnsureDetailsOption();
    }

    private void ApplyDefaultSelections()
    {
        isRecalculating = true;
        try
        {
            foreach (FomodGroupViewModel group in Steps.SelectMany(step => step.Groups))
            {
                group.ApplyDefaultSelection();
            }
        }
        finally
        {
            isRecalculating = false;
        }
    }

    private void Recalculate()
    {
        isRecalculating = true;
        try
        {
            Dictionary<string, string> flags = new(StringComparer.OrdinalIgnoreCase);
            foreach (FomodStepViewModel step in Steps)
            {
                step.IsVisible = FomodDependencyEvaluator.IsSatisfied(step.VisibleDependency, flags, fileDependencyStates);
                if (!step.IsVisible)
                {
                    continue;
                }

                foreach (FomodGroupViewModel group in step.Groups)
                {
                    group.RefreshOptionStates(flags, fileDependencyStates);
                    foreach (FomodOptionViewModel option in group.Options.Where(option => option.IsSelected))
                    {
                        foreach (FomodConditionFlagInfo flag in option.Flags)
                        {
                            if (!string.IsNullOrWhiteSpace(flag.Name))
                            {
                                flags[flag.Name] = flag.Value;
                            }
                        }
                    }
                }
            }
        }
        finally
        {
            isRecalculating = false;
        }

        if (currentStepIndex >= StepCount)
        {
            currentStepIndex = Math.Max(0, StepCount - 1);
        }

        UpdateValidationMessage();
        RaiseNavigationStateChanged();
        EnsureDetailsOption();
    }

    private void UpdateValidationMessage()
    {
        FomodStepViewModel? currentStep = CurrentStep;
        ValidationMessage = currentStep?.ValidationMessage ?? string.Empty;
        ValidationTargetGroup = currentStep?.FirstInvalidGroup;
    }

    private void RaiseNavigationStateChanged()
    {
        UpdateStepNavigationState();
        OnPropertyChanged(nameof(VisibleSteps));
        OnPropertyChanged(nameof(NavigationSteps));
        OnPropertyChanged(nameof(CurrentStep));
        OnPropertyChanged(nameof(CurrentStepNumber));
        OnPropertyChanged(nameof(StepCount));
        OnPropertyChanged(nameof(NavigationStepCount));
        OnPropertyChanged(nameof(StepCounterText));
        OnPropertyChanged(nameof(CanMovePrevious));
        OnPropertyChanged(nameof(CanMoveNext));
        OnPropertyChanged(nameof(CanFinish));
        OnPropertyChanged(nameof(CanLeaveCurrentStep));
        OnPropertyChanged(nameof(CanPrimaryAction));
        OnPropertyChanged(nameof(IsLastStep));
        OnPropertyChanged(nameof(PrimaryButtonText));
        OnPropertyChanged(nameof(SelectedOptionIds));
        (PreviousStepCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (NextStepCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (UsePreviousSelectionsCommand as RelayCommand)?.RaiseCanExecuteChanged();
    }

    private void EnsureDetailsOption()
    {
        IReadOnlyList<FomodOptionViewModel> currentOptions = CurrentStep?.Groups
            .SelectMany(group => group.Options)
            .ToList() ?? [];
        if (DetailsOption is not null && currentOptions.Contains(DetailsOption))
        {
            return;
        }

        DetailsOption = currentOptions.FirstOrDefault(option => option.IsSelected) ?? currentOptions.FirstOrDefault();
    }

    private void UpdateStepNavigationState()
    {
        List<FomodStepViewModel> visibleSteps = VisibleSteps.ToList();
        for (int index = 0; index < Steps.Count; index++)
        {
            FomodStepViewModel step = Steps[index];
            int visibleIndex = visibleSteps.IndexOf(step);
            step.SetNavigationState(
                index + 1,
                step.IsVisible && visibleIndex == currentStepIndex,
                step.IsVisible && visibleIndex >= 0 && visibleIndex < currentStepIndex);
        }
    }

    private static string NormalizeDependencyFile(string file)
    {
        return file.Trim()
            .Replace('/', '\\')
            .TrimStart('\\');
    }

    private void SetField<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return;
        }

        field = value;
        OnPropertyChanged(propertyName);
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}

public sealed class FomodStepViewModel : INotifyPropertyChanged
{
    private bool isVisible = true;
    private bool isCurrent;
    private bool isVisited;
    private int visibleNumber;

    public FomodStepViewModel(FomodInstallerViewModel owner, FomodStepInfo step)
    {
        Owner = owner;
        Id = step.Id;
        Name = string.IsNullOrWhiteSpace(step.Name) ? "Шаг" : step.Name;
        VisibleDependency = step.Visible;
        foreach (FomodGroupInfo group in step.Groups)
        {
            Groups.Add(new FomodGroupViewModel(owner, this, group));
        }
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public FomodInstallerViewModel Owner { get; }
    public string Id { get; }
    public string Name { get; }
    public FomodDependencyInfo? VisibleDependency { get; }
    public ObservableCollection<FomodGroupViewModel> Groups { get; } = new();

    public bool IsCurrent
    {
        get => isCurrent;
        private set => SetField(ref isCurrent, value);
    }

    public bool IsVisited
    {
        get => isVisited;
        private set => SetField(ref isVisited, value);
    }

    public int VisibleNumber
    {
        get => visibleNumber;
        private set => SetField(ref visibleNumber, value);
    }

    public string StepNumberText => VisibleNumber <= 0 ? string.Empty : VisibleNumber.ToString();

    public bool IsCompleted => IsVisited && IsSelectionValid;

    public string StatusText
    {
        get
        {
            if (IsCurrent)
            {
                return "Текущий шаг";
            }

            if (IsCompleted)
            {
                return "Пройден";
            }

            if (IsVisited)
            {
                return "Проверьте выбор";
            }

            if (!IsVisible)
            {
                return "Недоступно";
            }

            return "Дальше";
        }
    }

    public bool IsVisible
    {
        get => isVisible;
        set
        {
            if (isVisible == value)
            {
                return;
            }

            isVisible = value;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsVisible)));
            OnStateTextChanged();
        }
    }

    public bool IsSelectionValid => Groups.All(group => group.IsSelectionValid) &&
        Groups.SelectMany(group => group.Options)
            .Where(option => option.IsAcknowledgementOption)
            .All(option => option.IsSelected);

    public FomodGroupViewModel? FirstInvalidGroup => Groups.FirstOrDefault(group => !group.IsSelectionValid);

    public string ValidationMessage
    {
        get
        {
            string groupValidation = FirstInvalidGroup?.ValidationMessage ?? string.Empty;
            if (!string.IsNullOrWhiteSpace(groupValidation))
            {
                return groupValidation;
            }

            return Groups.SelectMany(group => group.Options).Any(option => option.IsAcknowledgementOption && !option.IsSelected)
                ? "Подтвердите, что вы прочитали информацию этого шага."
                : string.Empty;
        }
    }

    public void SetNavigationState(int number, bool current, bool visited)
    {
        VisibleNumber = number;
        IsCurrent = current;
        IsVisited = visited;
        OnStateTextChanged();
    }

    public void NotifySelectionStateChanged()
    {
        OnPropertyChanged(nameof(IsSelectionValid));
        OnPropertyChanged(nameof(ValidationMessage));
        OnStateTextChanged();
    }

    private void OnStateTextChanged()
    {
        OnPropertyChanged(nameof(StepNumberText));
        OnPropertyChanged(nameof(IsCompleted));
        OnPropertyChanged(nameof(StatusText));
    }

    private bool SetField<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }

        field = value;
        OnPropertyChanged(propertyName);
        return true;
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}

public sealed class FomodGroupViewModel : INotifyPropertyChanged
{
    private bool isValidationTarget;

    public FomodGroupViewModel(FomodInstallerViewModel owner, FomodStepViewModel step, FomodGroupInfo group)
    {
        Owner = owner;
        Step = step;
        Id = group.Id;
        Name = string.IsNullOrWhiteSpace(group.Name) ? "Опции" : group.Name;
        Type = string.IsNullOrWhiteSpace(group.Type) ? "SelectAny" : group.Type;
        foreach (FomodOptionInfo option in group.Options)
        {
            Options.Add(new FomodOptionViewModel(owner, this, option));
        }
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public FomodInstallerViewModel Owner { get; }
    public FomodStepViewModel Step { get; }
    public string Id { get; }
    public string Name { get; }
    public string Type { get; }
    public ObservableCollection<FomodOptionViewModel> Options { get; } = new();
    public bool IsSelectAll => IsGroupType("SelectAll");

    public bool IsValidationTarget
    {
        get => isValidationTarget;
        private set => SetField(ref isValidationTarget, value);
    }

    public bool IsSelectionValid
    {
        get
        {
            int selectedCount = Options.Count(option => option.IsSelected && option.IsUsable);
            if (IsExactlyOne)
            {
                return selectedCount == 1;
            }
            if (IsAtLeastOne)
            {
                return selectedCount >= 1;
            }
            if (IsAtMostOne)
            {
                return selectedCount <= 1;
            }

            return true;
        }
    }

    public string ValidationMessage
    {
        get
        {
            if (IsSelectionValid)
            {
                return string.Empty;
            }
            if (IsExactlyOne)
            {
                return $"Выберите один вариант в группе \"{Name}\".";
            }
            if (IsAtLeastOne)
            {
                return $"Выберите хотя бы один вариант в группе \"{Name}\".";
            }

            return $"В группе \"{Name}\" можно выбрать только один вариант.";
        }
    }

    private bool IsExactlyOne => IsGroupType("SelectExactlyOne");
    private bool IsAtLeastOne => IsGroupType("SelectAtLeastOne");
    private bool IsAtMostOne => IsGroupType("SelectAtMostOne");

    public void ApplyDefaultSelection()
    {
        foreach (FomodOptionViewModel option in Options)
        {
            option.SetSelectedSilently(option.IsAutoLocked);
        }

        if ((IsExactlyOne || IsAtLeastOne) && Options.All(option => !option.IsSelected))
        {
            FomodOptionViewModel? recommended = Options.FirstOrDefault(option =>
                option.IsRecommended &&
                option.IsUsable &&
                !option.RequiresExplicitSelection);
            recommended?.SetSelectedSilently(true);
        }
    }

    public void ClearUserSelections()
    {
        foreach (FomodOptionViewModel option in Options)
        {
            option.SetSelectedSilently(option.IsAutoLocked);
        }
    }

    public void SelectOption(FomodOptionViewModel option, bool selected)
    {
        Owner.ShowOptionDetails(option);
        if (!option.IsUsable || option.IsAutoLocked)
        {
            option.SetSelectedSilently(option.IsAutoLocked);
            return;
        }

        if (selected && (IsExactlyOne || IsAtMostOne))
        {
            foreach (FomodOptionViewModel other in Options.Where(other => !ReferenceEquals(other, option)))
            {
                other.SetSelectedSilently(false);
            }
        }

        option.SetSelectedSilently(selected);
        RaiseSelectionStateChanged();
        Owner.NotifyOptionChanged();
    }

    public void RefreshOptionStates(
        IReadOnlyDictionary<string, string> flags,
        IReadOnlyDictionary<string, bool> fileDependencyStates)
    {
        foreach (FomodOptionViewModel option in Options)
        {
            option.RefreshEffectiveType(flags, fileDependencyStates);
            if (!option.IsUsable)
            {
                option.SetSelectedSilently(false);
            }
            if (option.IsAutoLocked)
            {
                option.SetSelectedSilently(true);
            }
        }

        RaiseSelectionStateChanged();
    }

    internal void SetValidationTarget(bool value)
    {
        IsValidationTarget = value;
    }

    private bool IsGroupType(string type)
    {
        return string.Equals(Type, type, StringComparison.OrdinalIgnoreCase);
    }

    private void RaiseSelectionStateChanged()
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsSelectionValid)));
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(ValidationMessage)));
        Step.NotifySelectionStateChanged();
    }

    private bool SetField<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }

        field = value;
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        return true;
    }
}

public sealed class FomodOptionViewModel : INotifyPropertyChanged
{
    private bool isSelected;
    private string effectiveType;

    public FomodOptionViewModel(FomodInstallerViewModel owner, FomodGroupViewModel group, FomodOptionInfo option)
    {
        OwnerViewModel = owner;
        Owner = group;
        Id = option.Id;
        Name = string.IsNullOrWhiteSpace(option.Name) ? "Опция" : option.Name;
        Description = option.Description;
        ImagePath = option.ImagePath;
        Type = string.IsNullOrWhiteSpace(option.Type) ? "Optional" : option.Type;
        DefaultType = string.IsNullOrWhiteSpace(option.DefaultType) ? Type : option.DefaultType;
        effectiveType = Type;
        Flags = option.Flags;
        TypePatterns = option.TypePatterns;
        WasPreviouslySelected = owner.IsPreviouslySelected(Id);
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public FomodInstallerViewModel OwnerViewModel { get; }
    public FomodGroupViewModel Owner { get; }
    public string Id { get; }
    public string Name { get; }
    public string Description { get; }
    public string ImagePath { get; }
    public string Type { get; }
    public string DefaultType { get; }
    public IReadOnlyList<FomodConditionFlagInfo> Flags { get; }
    public IReadOnlyList<FomodTypePatternInfo> TypePatterns { get; }
    public bool WasPreviouslySelected { get; }
    public bool HasDescription => !string.IsNullOrWhiteSpace(Description);
    public bool HasMeaningfulDescription => HasDescription &&
        !string.Equals(NormalizeDisplayText(Name), NormalizeDisplayText(Description), StringComparison.OrdinalIgnoreCase);
    public string DisplayDescription => HasMeaningfulDescription
        ? Description
        : "Отдельное описание не указано.";
    public string PreviewImagePath => string.IsNullOrWhiteSpace(ImagePath)
        ? OwnerViewModel.ModuleImagePath
        : ImagePath;
    public bool HasPreviewImage => !string.IsNullOrWhiteSpace(PreviewImagePath);
    public string PreviousBadgeText => WasPreviouslySelected ? "Выбиралось раньше" : string.Empty;
    public bool IsAcknowledgementOption => LooksLikeAcknowledgement(Name);
    public bool RequiresExplicitSelection => IsAcknowledgementOption;
    public bool UsesInstalledFileState => TypePatterns.Any(pattern => FomodDependencyEvaluator.ContainsFileDependency(pattern.Dependencies));

    public bool IsSelected
    {
        get => isSelected;
        set => Owner.SelectOption(this, value);
    }

    public string EffectiveType
    {
        get => effectiveType;
        private set
        {
            if (string.Equals(effectiveType, value, StringComparison.OrdinalIgnoreCase))
            {
                return;
            }

            effectiveType = value;
            RaiseStateChanged();
        }
    }

    public string TypeBadgeText => EffectiveType switch
    {
        var type when string.Equals(type, "Required", StringComparison.OrdinalIgnoreCase) => "Обязательно",
        var type when string.Equals(type, "Recommended", StringComparison.OrdinalIgnoreCase) => "Рекомендовано FOMOD",
        var type when string.Equals(type, "NotUsable", StringComparison.OrdinalIgnoreCase) => "Недоступно",
        var type when string.Equals(type, "CouldBeUsable", StringComparison.OrdinalIgnoreCase) => "Возможно",
        _ => "Опционально"
    };

    public string TypeExplanationText
    {
        get
        {
            string source = UsesInstalledFileState
                ? " Состояние проверено по папке игры и установленным модам."
                : string.Empty;
            return EffectiveType switch
            {
                var type when string.Equals(type, "Required", StringComparison.OrdinalIgnoreCase) =>
                    "Установщик требует этот вариант." + source,
                var type when string.Equals(type, "Recommended", StringComparison.OrdinalIgnoreCase) =>
                    "Установщик рекомендует этот вариант." + source,
                var type when string.Equals(type, "NotUsable", StringComparison.OrdinalIgnoreCase) =>
                    "Этот вариант недоступен для текущего набора файлов." + source,
                var type when string.Equals(type, "CouldBeUsable", StringComparison.OrdinalIgnoreCase) =>
                    "Вариант может подойти, но установщик не может подтвердить это точно." + source,
                _ => UsesInstalledFileState
                    ? "Зависимости проверены по установленным файлам."
                    : "Обычный необязательный вариант."
            };
        }
    }

    public string SelectionStateText
    {
        get
        {
            if (IsSelected && UsesInstalledFileState)
            {
                return "Автовыбрано по установленным файлам";
            }

            if (IsSelected)
            {
                return "Выбрано";
            }

            return "Не выбрано";
        }
    }

    public bool IsRequired => string.Equals(EffectiveType, "Required", StringComparison.OrdinalIgnoreCase);
    public bool IsRecommended => string.Equals(EffectiveType, "Recommended", StringComparison.OrdinalIgnoreCase);
    public bool IsUsable => !string.Equals(EffectiveType, "NotUsable", StringComparison.OrdinalIgnoreCase);
    public bool IsAutoLocked => !RequiresExplicitSelection && (IsRequired || Owner.IsSelectAll);
    public bool CanToggle => IsUsable && !IsAutoLocked;

    public void SetSelectedSilently(bool selected)
    {
        if (isSelected == selected)
        {
            return;
        }

        isSelected = selected;
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsSelected)));
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(SelectionStateText)));
    }

    public void RefreshEffectiveType(
        IReadOnlyDictionary<string, string> flags,
        IReadOnlyDictionary<string, bool> fileDependencyStates)
    {
        foreach (FomodTypePatternInfo pattern in TypePatterns)
        {
            if (FomodDependencyEvaluator.IsSatisfied(pattern.Dependencies, flags, fileDependencyStates))
            {
                EffectiveType = string.IsNullOrWhiteSpace(pattern.Type) ? "Optional" : pattern.Type;
                return;
            }
        }

        EffectiveType = string.IsNullOrWhiteSpace(DefaultType) ? Type : DefaultType;
    }

    private void RaiseStateChanged()
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(EffectiveType)));
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(TypeBadgeText)));
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(TypeExplanationText)));
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsRequired)));
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsRecommended)));
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsUsable)));
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsAutoLocked)));
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(CanToggle)));
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(SelectionStateText)));
    }

    private static bool LooksLikeAcknowledgement(string text)
    {
        string normalized = NormalizeDisplayText(text);
        bool english = normalized.Contains("read", StringComparison.OrdinalIgnoreCase) &&
            (normalized.Contains("understand", StringComparison.OrdinalIgnoreCase) ||
             normalized.Contains("understood", StringComparison.OrdinalIgnoreCase));
        bool russian = normalized.Contains("прочитал", StringComparison.OrdinalIgnoreCase) &&
            (normalized.Contains("понял", StringComparison.OrdinalIgnoreCase) ||
             normalized.Contains("понимаю", StringComparison.OrdinalIgnoreCase));

        return english || russian;
    }

    private static string NormalizeDisplayText(string text)
    {
        return string.Join(
            ' ',
            text.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries));
    }
}

internal static class FomodDependencyEvaluator
{
    public static bool IsSatisfied(
        FomodDependencyInfo? dependency,
        IReadOnlyDictionary<string, string> flags,
        IReadOnlyDictionary<string, bool> fileDependencyStates)
    {
        if (dependency is null || string.IsNullOrWhiteSpace(dependency.Kind))
        {
            return true;
        }

        if (string.Equals(dependency.Kind, "flag", StringComparison.OrdinalIgnoreCase))
        {
            return flags.TryGetValue(dependency.Flag, out string? value) &&
                string.Equals(value, dependency.Value, StringComparison.Ordinal);
        }

        if (string.Equals(dependency.Kind, "file", StringComparison.OrdinalIgnoreCase))
        {
            bool exists = fileDependencyStates.TryGetValue(NormalizeDependencyFile(dependency.File), out bool fileExists) &&
                fileExists;
            return string.Equals(dependency.State, "Missing", StringComparison.OrdinalIgnoreCase)
                ? !exists
                : exists;
        }

        if (string.Equals(dependency.Kind, "composite", StringComparison.OrdinalIgnoreCase))
        {
            bool useOr = string.Equals(dependency.Operator, "Or", StringComparison.OrdinalIgnoreCase);
            if (dependency.Children.Count == 0)
            {
                return true;
            }

            return useOr
                ? dependency.Children.Any(child => IsSatisfied(child, flags, fileDependencyStates))
                : dependency.Children.All(child => IsSatisfied(child, flags, fileDependencyStates));
        }

        return true;
    }

    public static bool ContainsFileDependency(FomodDependencyInfo? dependency)
    {
        if (dependency is null)
        {
            return false;
        }

        if (string.Equals(dependency.Kind, "file", StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }

        return dependency.Children.Any(ContainsFileDependency);
    }

    private static string NormalizeDependencyFile(string file)
    {
        return file.Trim()
            .Replace('/', '\\')
            .TrimStart('\\');
    }
}
