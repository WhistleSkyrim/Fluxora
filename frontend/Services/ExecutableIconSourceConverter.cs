using System.Globalization;
using System.IO;
using System.Windows;
using System.Windows.Data;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace Fluxora.App.Services;

public sealed class ExecutableIconSourceConverter : IValueConverter
{
    private const string PlaceholderUri = "pack://application:,,,/Fluxora.png";
    private static readonly object CacheGate = new();
    private static readonly Dictionary<string, ImageSource> Cache = new(StringComparer.OrdinalIgnoreCase);

    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
    {
        string iconPath = value as string ?? string.Empty;
        ImageSource? source = LoadIcon(iconPath);
        source ??= LoadIcon(PlaceholderUri);
        return source ?? DependencyProperty.UnsetValue;
    }

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
    {
        throw new NotSupportedException();
    }

    private static ImageSource? LoadIcon(string pathOrUri)
    {
        if (string.IsNullOrWhiteSpace(pathOrUri))
        {
            return null;
        }

        bool isPackUri = pathOrUri.StartsWith("pack://", StringComparison.OrdinalIgnoreCase);
        if (!isPackUri && !File.Exists(pathOrUri))
        {
            return null;
        }

        lock (CacheGate)
        {
            if (Cache.TryGetValue(pathOrUri, out ImageSource? cached))
            {
                return cached;
            }

            try
            {
                BitmapImage image = new();
                image.BeginInit();
                image.CacheOption = BitmapCacheOption.OnLoad;
                image.CreateOptions = BitmapCreateOptions.IgnoreColorProfile;
                image.DecodePixelWidth = 32;
                image.UriSource = new Uri(pathOrUri, UriKind.Absolute);
                image.EndInit();
                image.Freeze();
                Cache[pathOrUri] = image;
                return image;
            }
            catch (IOException)
            {
                return null;
            }
            catch (UnauthorizedAccessException)
            {
                return null;
            }
            catch (NotSupportedException)
            {
                return null;
            }
            catch (UriFormatException)
            {
                return null;
            }
        }
    }
}
