using System.Windows;
using System.Windows.Input;

namespace Fluxora.App.Views;

public partial class FluxPackInstallProcessSplash : System.Windows.Controls.UserControl
{
    public static readonly DependencyProperty CloseCommandProperty = DependencyProperty.Register(
        nameof(CloseCommand),
        typeof(ICommand),
        typeof(FluxPackInstallProcessSplash),
        new PropertyMetadata(null));

    public FluxPackInstallProcessSplash()
    {
        InitializeComponent();
    }

    public ICommand? CloseCommand
    {
        get => (ICommand?)GetValue(CloseCommandProperty);
        set => SetValue(CloseCommandProperty, value);
    }
}
