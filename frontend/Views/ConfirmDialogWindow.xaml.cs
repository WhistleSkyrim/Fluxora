using System.Windows;
using System.Windows.Documents;
using System.Windows.Input;
using Fluxora.App.Models;

namespace Fluxora.App.Views;

/// <summary>
/// Borderless confirmation dialog used in place of the native <c>MessageBox</c>. It has no title
/// bar — only a custom close cross — and is driven entirely by a <see cref="ConfirmDialogOptions"/>
/// payload, so it carries no scenario-specific logic.
/// </summary>
public partial class ConfirmDialogWindow : Window
{
    public ConfirmDialogWindow(ConfirmDialogOptions options)
    {
        ArgumentNullException.ThrowIfNull(options);

        InitializeComponent();
        ApplyOptions(options);
    }

    private void ApplyOptions(ConfirmDialogOptions options)
    {
        Title = options.Heading;
        HeadingTextBlock.Text = options.Heading;
        BuildMessage(options.Message, options.Highlight);
        DetailsItemsControl.ItemsSource = options.Details;
        ConfirmTextBlock.Text = options.ConfirmText;
        CancelTextBlock.Text = options.CancelText;

        if (!options.IsDestructive)
        {
            ConfirmButton.Style = (Style)FindResource("SecondaryDialogButtonStyle");
        }
    }

    /// <summary>Renders the body, emphasising <paramref name="highlight"/> in bold when it occurs.</summary>
    private void BuildMessage(string message, string? highlight)
    {
        MessageTextBlock.Inlines.Clear();

        int index = string.IsNullOrEmpty(highlight)
            ? -1
            : message.IndexOf(highlight, StringComparison.Ordinal);

        if (index < 0)
        {
            MessageTextBlock.Inlines.Add(new Run(message));
            return;
        }

        if (index > 0)
        {
            MessageTextBlock.Inlines.Add(new Run(message[..index]));
        }

        MessageTextBlock.Inlines.Add(new Bold(new Run(highlight!))
        {
            Foreground = (System.Windows.Media.Brush)FindResource("TextBrush")
        });

        int tail = index + highlight!.Length;
        if (tail < message.Length)
        {
            MessageTextBlock.Inlines.Add(new Run(message[tail..]));
        }
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
        catch (InvalidOperationException)
        {
        }
    }

    private void OnConfirmClick(object sender, RoutedEventArgs e)
    {
        DialogResult = true;
        Close();
    }

    private void OnCancelClick(object sender, RoutedEventArgs e)
    {
        DialogResult = false;
        Close();
    }
}
