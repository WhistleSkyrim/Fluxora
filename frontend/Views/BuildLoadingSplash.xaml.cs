using System.ComponentModel;
using System.Windows;
using System.Windows.Media.Animation;
using Fluxora.App.ViewModels;

namespace Fluxora.App.Views;

public partial class BuildLoadingSplash : System.Windows.Controls.UserControl
{
    private const int PhraseFadeOutMilliseconds = 160;
    private const int PhraseFadeInMilliseconds = 220;

    private INotifyPropertyChanged? notifySource;
    private string currentPhrase = string.Empty;

    public BuildLoadingSplash()
    {
        InitializeComponent();
        DataContextChanged += OnSplashDataContextChanged;
    }

    private void OnSplashDataContextChanged(object sender, DependencyPropertyChangedEventArgs e)
    {
        if (notifySource is not null)
        {
            notifySource.PropertyChanged -= OnSourcePropertyChanged;
        }

        notifySource = e.NewValue as INotifyPropertyChanged;
        if (notifySource is not null)
        {
            notifySource.PropertyChanged += OnSourcePropertyChanged;
        }

        SetPhraseText(GetPhraseFromDataContext());
    }

    private void OnSourcePropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(MainWindowViewModel.BuildLoadingSplashPhrase))
        {
            Dispatcher.Invoke(() => AnimatePhraseChange(GetPhraseFromDataContext()));
            return;
        }

        if (e.PropertyName == nameof(MainWindowViewModel.IsBuildLoadingSplashVisible) &&
            DataContext is MainWindowViewModel { IsBuildLoadingSplashVisible: true })
        {
            Dispatcher.Invoke(() => SetPhraseText(GetPhraseFromDataContext()));
        }
    }

    private string GetPhraseFromDataContext()
    {
        return DataContext is MainWindowViewModel viewModel
            ? viewModel.BuildLoadingSplashPhrase
            : string.Empty;
    }

    private void SetPhraseText(string phrase)
    {
        currentPhrase = phrase;
        PhraseText.Text = phrase;
        PhraseText.Opacity = 1;
        PhraseTranslate.Y = 0;
    }

    private void AnimatePhraseChange(string nextPhrase)
    {
        if (string.IsNullOrWhiteSpace(nextPhrase) ||
            string.Equals(currentPhrase, nextPhrase, StringComparison.Ordinal))
        {
            return;
        }

        DoubleAnimation fadeOut = new(0, TimeSpan.FromMilliseconds(PhraseFadeOutMilliseconds))
        {
            EasingFunction = new CubicEase { EasingMode = EasingMode.EaseIn }
        };
        DoubleAnimation slideOut = new(-8, TimeSpan.FromMilliseconds(PhraseFadeOutMilliseconds))
        {
            EasingFunction = new CubicEase { EasingMode = EasingMode.EaseIn }
        };

        fadeOut.Completed += (_, _) =>
        {
            currentPhrase = nextPhrase;
            PhraseText.Text = nextPhrase;
            PhraseTranslate.Y = 8;

            DoubleAnimation fadeIn = new(1, TimeSpan.FromMilliseconds(PhraseFadeInMilliseconds))
            {
                EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
            };
            DoubleAnimation slideIn = new(0, TimeSpan.FromMilliseconds(PhraseFadeInMilliseconds))
            {
                EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
            };

            PhraseText.BeginAnimation(OpacityProperty, fadeIn);
            PhraseTranslate.BeginAnimation(System.Windows.Media.TranslateTransform.YProperty, slideIn);
        };

        PhraseText.BeginAnimation(OpacityProperty, fadeOut);
        PhraseTranslate.BeginAnimation(System.Windows.Media.TranslateTransform.YProperty, slideOut);
    }
}
