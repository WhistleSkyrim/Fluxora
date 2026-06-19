using System.Windows;
using Fluxora.App.ViewModels;

namespace Fluxora.App.Views;

public partial class ModOperationProcessSplash : System.Windows.Controls.UserControl
{
    public ModOperationProcessSplash()
    {
        InitializeComponent();
    }

    private void CloseButton_Click(object sender, RoutedEventArgs e)
    {
        if (DataContext is ModOperationProcessViewModel viewModel)
        {
            viewModel.Close();
        }
    }
}
