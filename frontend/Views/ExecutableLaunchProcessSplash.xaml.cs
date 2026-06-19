using System.Windows;
using System.Windows.Input;

namespace Fluxora.App.Views;

public partial class ExecutableLaunchProcessSplash : System.Windows.Controls.UserControl
{
    public static readonly DependencyProperty CloseCommandProperty = DependencyProperty.Register(
        nameof(CloseCommand),
        typeof(ICommand),
        typeof(ExecutableLaunchProcessSplash),
        new PropertyMetadata(null));

    public ExecutableLaunchProcessSplash()
    {
        InitializeComponent();
    }

    public ICommand? CloseCommand
    {
        get => (ICommand?)GetValue(CloseCommandProperty);
        set => SetValue(CloseCommandProperty, value);
    }
}
