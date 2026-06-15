using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;

namespace Fluxora.App.Services;

public static class DataGridInteractionService
{
    public static readonly DependencyProperty RowDoubleClickCommandProperty =
        DependencyProperty.RegisterAttached(
            "RowDoubleClickCommand",
            typeof(ICommand),
            typeof(DataGridInteractionService),
            new PropertyMetadata(null, OnRowDoubleClickCommandChanged));

    public static void SetRowDoubleClickCommand(DependencyObject element, ICommand? value)
    {
        element.SetValue(RowDoubleClickCommandProperty, value);
    }

    public static ICommand? GetRowDoubleClickCommand(DependencyObject element)
    {
        return (ICommand?)element.GetValue(RowDoubleClickCommandProperty);
    }

    private static void OnRowDoubleClickCommandChanged(DependencyObject element, DependencyPropertyChangedEventArgs e)
    {
        if (element is not DataGrid dataGrid)
        {
            return;
        }

        dataGrid.MouseDoubleClick -= OnDataGridMouseDoubleClick;
        if (e.NewValue is ICommand)
        {
            dataGrid.MouseDoubleClick += OnDataGridMouseDoubleClick;
        }
    }

    private static void OnDataGridMouseDoubleClick(object sender, MouseButtonEventArgs e)
    {
        if (sender is not DataGrid dataGrid ||
            FindVisualParent<DataGridRow>(e.OriginalSource as DependencyObject) is not { } row)
        {
            return;
        }

        ICommand? command = GetRowDoubleClickCommand(dataGrid);
        object? parameter = row.Item;
        if (command?.CanExecute(parameter) == true)
        {
            command.Execute(parameter);
            e.Handled = true;
        }
    }

    private static T? FindVisualParent<T>(DependencyObject? current) where T : DependencyObject
    {
        while (current is not null)
        {
            if (current is T match)
            {
                return match;
            }

            current = VisualTreeHelper.GetParent(current);
        }

        return null;
    }
}
