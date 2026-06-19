using System.Globalization;
using System.IO;
using System.Windows;
using System.Windows.Data;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace Fluxora.App.Services;

public sealed class FomodImageSourceConverter : IValueConverter
{
    private static readonly object CacheGate = new();
    private static readonly Dictionary<string, ImageSource> Cache = new(StringComparer.OrdinalIgnoreCase);

    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
    {
        string imagePath = value as string ?? string.Empty;
        ImageSource? source = LoadImage(imagePath, DecodePixelWidth(parameter));
        return source ?? DependencyProperty.UnsetValue;
    }

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
    {
        throw new NotSupportedException();
    }

    private static int DecodePixelWidth(object parameter)
    {
        if (parameter is string text &&
            int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out int parsed) &&
            parsed > 0)
        {
            return parsed;
        }

        return 900;
    }

    private static ImageSource? LoadImage(string path, int decodePixelWidth)
    {
        if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
        {
            return null;
        }

        string cacheKey = path + "|" + decodePixelWidth.ToString(CultureInfo.InvariantCulture);
        lock (CacheGate)
        {
            if (Cache.TryGetValue(cacheKey, out ImageSource? cached))
            {
                return cached;
            }

            try
            {
                BitmapImage image = new();
                image.BeginInit();
                image.CacheOption = BitmapCacheOption.OnLoad;
                image.CreateOptions = BitmapCreateOptions.IgnoreColorProfile;
                image.DecodePixelWidth = decodePixelWidth;
                image.UriSource = new Uri(path, UriKind.Absolute);
                image.EndInit();
                image.Freeze();
                Cache[cacheKey] = image;
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
