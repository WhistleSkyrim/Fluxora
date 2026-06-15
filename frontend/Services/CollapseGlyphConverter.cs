using System.Globalization;
using System.Windows.Data;

namespace Fluxora.App.Services;

/// <summary>
/// Maps a separator's folded state to a Segoe MDL2 chevron glyph: a right chevron (U+E76C) when
/// collapsed (group hidden) and a down chevron (U+E70D) when expanded (group shown).
/// </summary>
public sealed class CollapseGlyphConverter : IValueConverter
{
    private static readonly string ChevronRight = ((char)0xE76C).ToString();
    private static readonly string ChevronDown = ((char)0xE70D).ToString();

    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
    {
        return value is true ? ChevronRight : ChevronDown;
    }

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
    {
        throw new NotSupportedException();
    }
}
