using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Threading;
using Fluxora.App.Services;
using Fluxora.Installer.Models;
using Fluxora.Installer.Services;
using Forms = System.Windows.Forms;

namespace Fluxora.Installer;

public partial class MainWindow : Window
{
    private readonly InstallerText text = new();
    private readonly LegalDocumentService legalDocumentService = new();
    private readonly InstallerNativeBridge nativeBridge;
    private readonly PayloadResourceService payloadResourceService = new();
    private readonly InstallerLogService logService = new();
    private readonly WindowChromeService chromeService;
    private readonly IReadOnlyList<InstallerLanguage> languages =
    [
        new("en", "English", "English"),
        new("de", "Deutsch", "German"),
        new("ru", "Русский", "Russian")
    ];

    private int stepIndex;
    private int lastAnimatedStepIndex = -1;
    private bool showingPrivacy = true;
    private bool isInstalling;
    private InstallerResult? installerResult;

    public MainWindow()
    {
        InitializeComponent();
        chromeService = new WindowChromeService(this);
        chromeService.Attach();
        logService.Initialize();
        RegisterCrashHandlers();
        nativeBridge = new InstallerNativeBridge(logService);

        LanguageComboBox.ItemsSource = languages;
        LanguageComboBox.SelectedItem = PickDefaultLanguage();
        InstallPathTextBox.Text = DefaultInstallPath();

        UpdateTexts();
        UpdateLegalDocument();
        UpdateStep();
    }

    private InstallerLanguage PickDefaultLanguage()
    {
        return languages[0];
    }

    private static string DefaultInstallPath()
    {
        string localAppData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        if (string.IsNullOrWhiteSpace(localAppData))
        {
            localAppData = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
        }

        return Path.Combine(localAppData, "Programs", "Fluxora");
    }

    private static string NormalizeInstallPath(string path)
    {
        string trimmed = path.Trim();
        if (string.IsNullOrWhiteSpace(trimmed))
        {
            return trimmed;
        }

        try
        {
            string fullPath = Path.GetFullPath(trimmed);
            string? root = Path.GetPathRoot(fullPath);
            if (!string.IsNullOrWhiteSpace(root) && IsRootPath(fullPath, root))
            {
                return Path.Combine(root, "Fluxora");
            }

            return fullPath.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        }
        catch
        {
            return trimmed;
        }
    }

    private static bool IsRootPath(string fullPath, string root)
    {
        string normalizedPath = fullPath.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        string normalizedRoot = root.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        return string.Equals(normalizedPath, normalizedRoot, StringComparison.OrdinalIgnoreCase);
    }

    private string T(string key) => text[key];

    private void UpdateTexts()
    {
        Title = T("WindowTitle");
        WindowTitleText.Text = T("WindowTitle");
        AppNameText.Text = T("AppName");
        AppSubtitleText.Text = T("AppSubtitle");

        StepText0.Text = T("StepLanguage");
        StepText1.Text = T("StepLegal");
        StepText2.Text = T("StepTarget");
        StepText3.Text = T("StepProgress");
        StepText4.Text = T("StepResult");

        LanguageTitleText.Text = T("LanguageTitle");
        LanguageBodyText.Text = T("LanguageBody");
        LegalTitleText.Text = T("LegalTitle");
        LegalBodyText.Text = T("LegalBody");
        PrivacyTabButton.Content = T("PrivacyTab");
        TermsTabButton.Content = T("TermsTab");
        AcceptPrivacyCheckBox.Content = T("AcceptPrivacy");
        AcceptTermsCheckBox.Content = T("AcceptTerms");
        TargetTitleText.Text = T("TargetTitle");
        TargetBodyText.Text = T("TargetBody");
        TargetLabelText.Text = T("TargetLabel");
        BrowseButton.Content = T("Browse");
        ShortcutCheckBox.Content = T("Shortcut");
        TargetHintText.Text = T("TargetHint");

        BackButton.Content = T("Back");
        NextButton.Content = T("Next");
        InstallButton.Content = T("Install");
        CancelButton.Content = stepIndex == 4 ? T("Close") : T("Cancel");
        CloseResultButton.Content = T("Close");
        LaunchButton.Content = T("Launch");
        OpenFolderButton.Content = T("OpenFolder");

        if (stepIndex == 3)
        {
            ProgressTitleText.Text = T("ProgressTitle");
        }
    }

    private void UpdateLegalDocument()
    {
        LegalDocumentText.Text = legalDocumentService.ReadDocument(
            text.LanguageCode,
            showingPrivacy ? "privacy" : "terms");

        PrivacyTabButton.Background = showingPrivacy ? Brush("AccentBrush") : Brush("PanelRaisedBrush");
        PrivacyTabButton.BorderBrush = showingPrivacy ? Brush("AccentHoverBrush") : Brush("LineBrush");
        TermsTabButton.Background = showingPrivacy ? Brush("PanelRaisedBrush") : Brush("AccentBrush");
        TermsTabButton.BorderBrush = showingPrivacy ? Brush("LineBrush") : Brush("AccentHoverBrush");
    }

    private void UpdateStep()
    {
        StepLanguagePanel.Visibility = stepIndex == 0 ? Visibility.Visible : Visibility.Collapsed;
        StepLegalPanel.Visibility = stepIndex == 1 ? Visibility.Visible : Visibility.Collapsed;
        StepTargetPanel.Visibility = stepIndex == 2 ? Visibility.Visible : Visibility.Collapsed;
        StepProgressPanel.Visibility = stepIndex == 3 ? Visibility.Visible : Visibility.Collapsed;
        StepResultPanel.Visibility = stepIndex == 4 ? Visibility.Visible : Visibility.Collapsed;

        UpdateStepMarker(0, StepDot0, StepText0);
        UpdateStepMarker(1, StepDot1, StepText1);
        UpdateStepMarker(2, StepDot2, StepText2);
        UpdateStepMarker(3, StepDot3, StepText3);
        UpdateStepMarker(4, StepDot4, StepText4);

        BackButton.Visibility = stepIndex is > 0 and < 3 ? Visibility.Visible : Visibility.Collapsed;
        NextButton.Visibility = stepIndex < 2 ? Visibility.Visible : Visibility.Collapsed;
        InstallButton.Visibility = stepIndex == 2 ? Visibility.Visible : Visibility.Collapsed;
        CloseResultButton.Visibility = stepIndex == 4 ? Visibility.Visible : Visibility.Collapsed;
        CancelButton.Visibility = stepIndex == 4 ? Visibility.Collapsed : Visibility.Visible;

        BackButton.IsEnabled = !isInstalling;
        NextButton.IsEnabled = CanGoNext();
        InstallButton.IsEnabled = CanInstall();
        CancelButton.IsEnabled = !isInstalling;
        CloseResultButton.IsEnabled = !isInstalling;

        UpdateTexts();
        AnimateCurrentStepIfNeeded();
    }

    private void UpdateStepMarker(int markerIndex, Border dot, TextBlock label)
    {
        bool isActive = markerIndex == stepIndex;
        bool isDone = markerIndex < stepIndex;

        dot.Background = isActive || isDone ? Brush("AccentBrush") : Brush("PanelRaisedBrush");
        dot.BorderBrush = isActive || isDone ? Brush("AccentHoverBrush") : Brush("LineBrush");
        label.Foreground = isActive ? Brush("TextBrush") : isDone ? Brush("TextSecondaryBrush") : Brush("MutedTextBrush");
        label.FontWeight = isActive ? FontWeights.Black : FontWeights.SemiBold;

        if (dot.Child is TextBlock number)
        {
            number.Foreground = isActive || isDone ? System.Windows.Media.Brushes.White : Brush("TextSecondaryBrush");
        }
    }

    private System.Windows.Media.Brush Brush(string key) => (System.Windows.Media.Brush)FindResource(key);

    private void AnimateCurrentStepIfNeeded()
    {
        if (lastAnimatedStepIndex == stepIndex)
        {
            return;
        }

        lastAnimatedStepIndex = stepIndex;
        if (PanelForStep(stepIndex) is not FrameworkElement panel)
        {
            return;
        }

        panel.Opacity = 0;
        TranslateTransform transform = new(0, 12);
        panel.RenderTransform = transform;

        CubicEase ease = new() { EasingMode = EasingMode.EaseOut };
        panel.BeginAnimation(
            OpacityProperty,
            new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(220))
            {
                EasingFunction = ease
            });

        transform.BeginAnimation(
            TranslateTransform.YProperty,
            new DoubleAnimation(12, 0, TimeSpan.FromMilliseconds(260))
            {
                EasingFunction = ease
            });
    }

    private FrameworkElement? PanelForStep(int index)
    {
        return index switch
        {
            0 => StepLanguagePanel,
            1 => StepLegalPanel,
            2 => StepTargetPanel,
            3 => StepProgressPanel,
            4 => StepResultPanel,
            _ => null
        };
    }

    private bool CanGoNext()
    {
        return stepIndex switch
        {
            0 => LanguageComboBox.SelectedItem is InstallerLanguage,
            1 => AcceptPrivacyCheckBox.IsChecked == true && AcceptTermsCheckBox.IsChecked == true,
            _ => false
        };
    }

    private bool CanInstall()
    {
        return !isInstalling && !string.IsNullOrWhiteSpace(InstallPathTextBox.Text);
    }

    private void OnSourceInitialized(object? sender, EventArgs e)
    {
    }

    private void OnClosed(object? sender, EventArgs e)
    {
        logService.Dispose();
    }

    private void RegisterCrashHandlers()
    {
        System.Windows.Application.Current.DispatcherUnhandledException += OnDispatcherUnhandledException;
        AppDomain.CurrentDomain.UnhandledException += OnUnhandledException;
        TaskScheduler.UnobservedTaskException += OnUnobservedTaskException;
    }

    private void OnDispatcherUnhandledException(object sender, DispatcherUnhandledExceptionEventArgs e)
    {
        logService.CrashError("Unhandled dispatcher exception.", e.Exception);
    }

    private void OnUnhandledException(object sender, UnhandledExceptionEventArgs e)
    {
        if (e.ExceptionObject is Exception exception)
        {
            logService.CrashError("Unhandled application domain exception.", exception);
            return;
        }

        logService.CrashError($"Unhandled application domain exception object: {e.ExceptionObject}");
    }

    private void OnUnobservedTaskException(object? sender, UnobservedTaskExceptionEventArgs e)
    {
        logService.CrashError("Unobserved task exception.", e.Exception);
    }

    private void OnDragMove(object sender, MouseButtonEventArgs e)
    {
        if (e.ChangedButton != MouseButton.Left)
        {
            return;
        }

        try
        {
            DragMove();
        }
        catch
        {
        }
    }

    private void OnMinimizeClick(object sender, RoutedEventArgs e)
    {
        WindowState = WindowState.Minimized;
    }

    private void OnCloseClick(object sender, RoutedEventArgs e)
    {
        if (isInstalling)
        {
            return;
        }

        Close();
    }

    private void OnBackClick(object sender, RoutedEventArgs e)
    {
        if (isInstalling || stepIndex <= 0)
        {
            return;
        }

        stepIndex--;
        UpdateStep();
    }

    private void OnNextClick(object sender, RoutedEventArgs e)
    {
        if (!CanGoNext())
        {
            return;
        }

        stepIndex++;
        UpdateStep();
    }

    private void OnLanguageSelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (LanguageComboBox.SelectedItem is not InstallerLanguage language)
        {
            return;
        }

        text.LanguageCode = language.Code;
        UpdateTexts();
        UpdateLegalDocument();
        UpdateStep();
    }

    private void OnPrivacyTabClick(object sender, RoutedEventArgs e)
    {
        showingPrivacy = true;
        UpdateLegalDocument();
    }

    private void OnTermsTabClick(object sender, RoutedEventArgs e)
    {
        showingPrivacy = false;
        UpdateLegalDocument();
    }

    private void OnLegalAcceptanceChanged(object sender, RoutedEventArgs e)
    {
        UpdateStep();
    }

    private void OnInstallPathChanged(object sender, TextChangedEventArgs e)
    {
        InstallButton.IsEnabled = CanInstall();
    }

    private void OnBrowseClick(object sender, RoutedEventArgs e)
    {
        using Forms.FolderBrowserDialog dialog = new()
        {
            Description = T("TargetLabel"),
            UseDescriptionForTitle = true,
            SelectedPath = Directory.Exists(InstallPathTextBox.Text) ? InstallPathTextBox.Text : DefaultInstallPath()
        };

        if (dialog.ShowDialog() == Forms.DialogResult.OK)
        {
            InstallPathTextBox.Text = NormalizeInstallPath(dialog.SelectedPath);
        }
    }

    private async void OnInstallClick(object sender, RoutedEventArgs e)
    {
        if (!CanInstall())
        {
            ShowError(T("MissingPath"));
            return;
        }

        string payloadPath = string.Empty;
        string installPath = NormalizeInstallPath(InstallPathTextBox.Text);
        using InstallerLogService.OperationScope operation = logService.BeginOperation(
            "InstallPackage",
            $"target=\"{installPath}\", createDesktopShortcut={ShortcutCheckBox.IsChecked == true}");
        try
        {
            if (!nativeBridge.IsAvailable())
            {
                throw new InvalidOperationException(T("MissingNative"));
            }

            isInstalling = true;
            stepIndex = 3;
            installerResult = null;
            InstallProgressBar.Value = 0;
            ProgressPercentText.Text = "0%";
            ProgressTitleText.Text = T("ProgressTitle");
            ProgressDetailText.Text = T("ProgressPreparing");
            ProgressPhaseText.Text = T("ProgressPreparing");
            UpdateStep();

            InstallPathTextBox.Text = installPath;
            nativeBridge.ValidateInstallDirectory(installPath);
            payloadPath = payloadResourceService.ExtractPayloadToTemp();
            logService.Info($"Starting install. target=\"{installPath}\"");

            installerResult = await nativeBridge.InstallPackageAsync(
                payloadPath,
                installPath,
                ShortcutCheckBox.IsChecked == true,
                OnNativeProgress);

            logService.Info($"Install completed. app=\"{installerResult.ApplicationPath}\"");
            operation.Complete($"app=\"{installerResult.ApplicationPath}\", installDirectory=\"{installerResult.InstallDirectory}\"");
            ShowSuccess(installerResult);
        }
        catch (Exception exception)
        {
            operation.Fail(exception);
            logService.Error("Install failed.", exception);
            ShowError(exception.Message);
        }
        finally
        {
            payloadResourceService.TryDeletePayload(payloadPath);
            isInstalling = false;
            UpdateStep();
        }
    }

    private void OnNativeProgress(InstallerProgress progress)
    {
        Dispatcher.BeginInvoke(() =>
        {
            double percent = Math.Clamp(progress.Percent, 0, 100);
            InstallProgressBar.Value = percent;
            ProgressPercentText.Text = $"{percent:0}%";

            string phaseText = progress.Phase switch
            {
                "preparing" => T("ProgressPreparing"),
                "copying" => T("ProgressCopying"),
                "finalizing" => T("ProgressFinalizing"),
                "completed" => T("ProgressCompleted"),
                _ => T("ProgressCopying")
            };

            ProgressPhaseText.Text = phaseText;
            ProgressDetailText.Text = string.IsNullOrWhiteSpace(progress.CurrentItem)
                ? phaseText
                : $"{phaseText}: {progress.CurrentItem}";
        });
    }

    private void ShowSuccess(InstallerResult result)
    {
        stepIndex = 4;
        ResultIcon.Data = (Geometry)FindResource("Icon.ConflictPlus");
        ResultIcon.Foreground = Brush("SuccessBrush");
        ResultTitleText.Text = T("SuccessTitle");
        ResultDetailText.Text = $"{T("SuccessDetail")}{Environment.NewLine}{result.InstallDirectory}";
        LaunchButton.Visibility = Visibility.Visible;
        OpenFolderButton.Visibility = Visibility.Visible;
        UpdateStep();
    }

    private void ShowError(string detail)
    {
        stepIndex = 4;
        ResultIcon.Data = (Geometry)FindResource("Icon.AlertTriangle");
        ResultIcon.Foreground = Brush("DangerBrush");
        ResultTitleText.Text = T("ErrorTitle");
        ResultDetailText.Text = detail;
        LaunchButton.Visibility = Visibility.Collapsed;
        OpenFolderButton.Visibility = Visibility.Collapsed;
        UpdateStep();
    }

    private void OnLaunchClick(object sender, RoutedEventArgs e)
    {
        if (installerResult is null || string.IsNullOrWhiteSpace(installerResult.ApplicationPath))
        {
            return;
        }

        Process.Start(new ProcessStartInfo(installerResult.ApplicationPath)
        {
            UseShellExecute = true,
            WorkingDirectory = Path.GetDirectoryName(installerResult.ApplicationPath) ?? installerResult.InstallDirectory
        });
    }

    private void OnOpenFolderClick(object sender, RoutedEventArgs e)
    {
        string? folder = installerResult?.InstallDirectory;
        if (string.IsNullOrWhiteSpace(folder))
        {
            folder = InstallPathTextBox.Text;
        }

        if (!string.IsNullOrWhiteSpace(folder))
        {
            Process.Start(new ProcessStartInfo("explorer.exe", $"\"{folder}\"")
            {
                UseShellExecute = true
            });
        }
    }
}
