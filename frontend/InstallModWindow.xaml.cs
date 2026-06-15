using System.IO;
using System.Windows;
using System.Windows.Input;
using System.Windows.Threading;
using Fluxora.App.Services;

namespace Fluxora.App;

public partial class InstallModWindow : Window
{
    private readonly WindowChromeService windowChromeService;
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
        : this(
            suggestedName,
            "Установка мода",
            "Укажите название, под которым мод появится в папке mods.",
            "Установить")
    {
    }

    public InstallModWindow(
        string suggestedName,
        string title,
        string description,
        string acceptText)
    {
        InitializeComponent();
        windowChromeService = new WindowChromeService(this);
        windowChromeService.Attach();
        Title = title;
        TitleTextBlock.Text = title;
        DescriptionTextBlock.Text = description;
        AcceptButtonTextBlock.Text = acceptText;
        ModNameTextBox.Text = suggestedName;
    }

    public string ModName => NormalizeModName(ModNameTextBox.Text);

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        Dispatcher.BeginInvoke(
            new Action(() =>
            {
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
}
