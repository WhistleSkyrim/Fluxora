using System.IO;
using System.Windows;
using System.Windows.Input;
using System.Windows.Threading;
using Fluxora.App.Models;
using Fluxora.App.Services;

namespace Fluxora.App;

public partial class InstallModWindow : Window
{
    private readonly WindowChromeService windowChromeService;
    private readonly bool isConflictResolutionMode;
    private static readonly char[] TrimCharacters = [' ', '\t', '\r', '\n', '.'];
    private static readonly char[] InvalidFileNameCharacters = Path.GetInvalidFileNameChars();
    private static readonly HashSet<string> ReservedDeviceNames = new(StringComparer.OrdinalIgnoreCase)
    {
        "CON",
        "PRN",
        "AUX",
        "NUL",
        "COM1",
        "COM2",
        "COM3",
        "COM4",
        "COM5",
        "COM6",
        "COM7",
        "COM8",
        "COM9",
        "LPT1",
        "LPT2",
        "LPT3",
        "LPT4",
        "LPT5",
        "LPT6",
        "LPT7",
        "LPT8",
        "LPT9"
    };

    public InstallModWindow(string suggestedName)
        : this(suggestedName, null)
    {
    }

    public InstallModWindow(string suggestedName, ContentLayoutPreview? layoutPreview)
        : this(
            suggestedName,
            "Установка мода",
            "Укажите название, под которым мод появится в папке mods.",
            "Установить",
            layoutPreview)
    {
    }

    public InstallModWindow(
        string suggestedName,
        string title,
        string description,
        string acceptText)
        : this(suggestedName, title, description, acceptText, null)
    {
    }

    public InstallModWindow(
        string suggestedName,
        string title,
        string description,
        string acceptText,
        ContentLayoutPreview? layoutPreview)
        : this(suggestedName, title, description, acceptText, false, layoutPreview)
    {
    }

    private InstallModWindow(
        string suggestedName,
        string title,
        string description,
        string acceptText,
        bool isConflictResolutionMode,
        ContentLayoutPreview? layoutPreview = null)
    {
        InitializeComponent();
        this.isConflictResolutionMode = isConflictResolutionMode;
        windowChromeService = new WindowChromeService(this);
        windowChromeService.Attach();
        Title = title;
        TitleTextBlock.Text = title;
        DescriptionTextBlock.Text = description;
        AcceptButtonTextBlock.Text = acceptText;
        ModNameTextBox.Text = suggestedName;
        ApplyPlacementPreview(layoutPreview);

        if (isConflictResolutionMode)
        {
            Height = 392;
            MinHeight = 392;
            ModNameTextBox.Visibility = Visibility.Collapsed;
            ValidationMessageTextBlock.Visibility = Visibility.Collapsed;
            ConflictPanel.Visibility = Visibility.Visible;
            AcceptButton.Visibility = Visibility.Collapsed;
            ReplaceDescriptionTextBlock.Text = $"Удалить старую папку \"{suggestedName}\" и поставить новый мод.";
            MergeDescriptionTextBlock.Text = "Добавить новые файлы в старую папку, совпадающие файлы перезаписать.";
        }
    }

    public string ModName => NormalizeModName(ModNameTextBox.Text);

    public ExistingModInstallMode ExistingModMode { get; private set; } = ExistingModInstallMode.FailIfExists;

    public static InstallModWindow CreateConflictResolutionDialog(string modName)
    {
        string normalizedName = NormalizeModName(modName);
        return new InstallModWindow(
            normalizedName,
            "Мод уже есть",
            $"В сборке уже есть мод \"{normalizedName}\". Выберите, что сделать с новой установкой.",
            "Установить",
            true);
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        Dispatcher.BeginInvoke(
            new Action(() =>
            {
                if (isConflictResolutionMode)
                {
                    ReplaceActionButton.Focus();
                    Keyboard.Focus(ReplaceActionButton);
                    return;
                }

                ModNameTextBox.Focus();
                Keyboard.Focus(ModNameTextBox);
                ModNameTextBox.SelectAll();
            }),
            DispatcherPriority.Input);
    }

    private void OnOkClick(object sender, RoutedEventArgs e)
    {
        if (!TryValidateModName(out string validationMessage))
        {
            ShowValidationMessage(validationMessage);
            ModNameTextBox.Focus();
            return;
        }

        DialogResult = true;
        Close();
    }

    private void OnModNameTextChanged(object sender, System.Windows.Controls.TextChangedEventArgs e)
    {
        if (ValidationMessageTextBlock.Visibility == Visibility.Visible)
        {
            if (TryValidateModName(out string validationMessage))
            {
                HideValidationMessage();
            }
            else
            {
                ValidationMessageTextBlock.Text = validationMessage;
            }
        }
    }

    private void OnWindowDragMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (e.ChangedButton != MouseButton.Left)
        {
            return;
        }

        try
        {
            DragMove();
        }
        catch (InvalidOperationException)
        {
        }
    }

    private void OnCancelClick(object sender, RoutedEventArgs e)
    {
        DialogResult = false;
        Close();
    }

    private void OnReplaceClick(object sender, RoutedEventArgs e)
    {
        ExistingModMode = ExistingModInstallMode.Replace;
        DialogResult = true;
        Close();
    }

    private void OnMergeClick(object sender, RoutedEventArgs e)
    {
        ExistingModMode = ExistingModInstallMode.Merge;
        DialogResult = true;
        Close();
    }

    private static string NormalizeModName(string value)
    {
        return value.Trim(TrimCharacters);
    }

    private bool TryValidateModName(out string validationMessage)
    {
        string name = ModName;
        if (string.IsNullOrWhiteSpace(name))
        {
            validationMessage = "Введите название.";
            return false;
        }

        if (name.IndexOfAny(InvalidFileNameCharacters) >= 0)
        {
            validationMessage = "Название содержит символы, которые нельзя использовать в имени папки.";
            return false;
        }

        string deviceName = name.Split('.')[0];
        if (ReservedDeviceNames.Contains(deviceName))
        {
            validationMessage = "Такое название зарезервировано Windows. Выберите другое.";
            return false;
        }

        validationMessage = string.Empty;
        return true;
    }

    private void ShowValidationMessage(string message)
    {
        ValidationMessageTextBlock.Text = message;
        ValidationMessageTextBlock.Visibility = Visibility.Visible;
    }

    private void HideValidationMessage()
    {
        ValidationMessageTextBlock.Text = string.Empty;
        ValidationMessageTextBlock.Visibility = Visibility.Collapsed;
    }

    private void ApplyPlacementPreview(ContentLayoutPreview? preview)
    {
        if (preview is null)
        {
            return;
        }

        Height = Math.Max(Height, 432);
        MinHeight = Math.Max(MinHeight, 432);
        PlacementPreviewPanel.Visibility = Visibility.Visible;
        PlacementPreviewSummaryTextBlock.Text = BuildPlacementPreviewSummary(preview);
        PlacementPreviewItemsControl.ItemsSource = BuildPlacementPreviewLines(preview);

        if (!preview.CanInstall)
        {
            AcceptButton.IsEnabled = false;
            ValidationMessageTextBlock.Text = "Архив заблокирован правилами размещения.";
            ValidationMessageTextBlock.Visibility = Visibility.Visible;
        }
    }

    private static string BuildPlacementPreviewSummary(ContentLayoutPreview preview)
    {
        string summary = string.IsNullOrWhiteSpace(preview.ExplanationSummary)
            ? "Fluxora построила план размещения для выбранной игры."
            : preview.ExplanationSummary;

        return preview.Summary.TotalEntries > 0
            ? $"{summary} Файлов: {preview.Summary.TotalEntries}, к установке: {preview.Summary.PlannedEntries}."
            : summary;
    }

    private static List<string> BuildPlacementPreviewLines(ContentLayoutPreview preview)
    {
        List<string> lines = new();
        foreach (ContentLayoutFinding finding in preview.ValidationFindings.Where(finding => finding.BlocksInstall))
        {
            string prefix = string.IsNullOrWhiteSpace(finding.Path) ? "Блокер" : $"Блокер · {finding.Path}";
            lines.Add($"{prefix}: {finding.Message}");
        }

        foreach (ContentLayoutFinding finding in preview.ValidationFindings.Where(finding => !finding.BlocksInstall))
        {
            string prefix = string.IsNullOrWhiteSpace(finding.Path) ? "Предупреждение" : $"Предупреждение · {finding.Path}";
            lines.Add($"{prefix}: {finding.Message}");
        }

        foreach (ContentLayoutPreviewEntry entry in preview.Entries.Take(8))
        {
            string target = entry.Target == "blocked"
                ? "blocked"
                : $"{entry.Target}/{entry.TargetRelativePath}".TrimEnd('/');
            lines.Add($"{entry.SourcePath} -> {target}: {entry.Explanation}");
        }

        if (preview.Entries.Count > 8)
        {
            lines.Add($"Ещё файлов: {preview.Entries.Count - 8}");
        }

        return lines;
    }
}
