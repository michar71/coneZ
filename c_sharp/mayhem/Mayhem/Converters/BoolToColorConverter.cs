using System;
using System.Globalization;
using Avalonia.Data.Converters;
using Avalonia.Media;

namespace Mayhem.Converters;

public sealed class BoolToColorConverter : IValueConverter
{
    public static readonly BoolToColorConverter Instance = new();

    private static readonly IBrush SelectedBrush = new SolidColorBrush(Color.Parse("#4DA3FF"));
    private static readonly IBrush UnselectedBrush = new SolidColorBrush(Color.Parse("#2B6CB0"));

    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        return value is bool b && b ? SelectedBrush : UnselectedBrush;
    }

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        throw new NotSupportedException();
    }
}
