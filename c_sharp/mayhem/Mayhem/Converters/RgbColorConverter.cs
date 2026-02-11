using System;
using Avalonia.Data.Converters;
using Avalonia.Media;
using Mayhem.Models;

namespace Mayhem.Converters;

public sealed class RgbColorConverter : IValueConverter
{
    public static readonly RgbColorConverter Instance = new();

    public object? Convert(object? value, Type targetType, object? parameter, System.Globalization.CultureInfo culture)
    {
        if (value is RgbColor rgb)
        {
            return Color.FromRgb(rgb.R, rgb.G, rgb.B);
        }
        return Colors.Transparent;
    }

    public object? ConvertBack(object? value, Type targetType, object? parameter, System.Globalization.CultureInfo culture)
    {
        if (value is Color color)
        {
            return new RgbColor(color.R, color.G, color.B);
        }
        return new RgbColor(0, 0, 0);
    }
}
