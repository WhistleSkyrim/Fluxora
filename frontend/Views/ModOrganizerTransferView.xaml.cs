using System.Windows;
using System.Windows.Input;

namespace Fluxora.App.Views;

public partial class ModOrganizerTransferView : System.Windows.Controls.UserControl
{
    public static readonly DependencyProperty CloseCommandProperty = DependencyProperty.Register(
        nameof(CloseCommand),
        typeof(ICommand),
        typeof(ModOrganizerTransferView),
        new PropertyMetadata(null));

    public ModOrganizerTransferView()
    {
        InitializeComponent();
    }

    public ICommand? CloseCommand
    {
        get => (ICommand?)GetValue(CloseCommandProperty);
        set => SetValue(CloseCommandProperty, value);
    }
}
