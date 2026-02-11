using System;
using System.Globalization;
using Avalonia;
using Avalonia.Data.Converters;

namespace Mayhem.Converters;

public sealed class BoolToVisibilityConverter : IValueConverter
{
    public static readonly BoolToVisibilityConverter Instance = new(false);
    public static readonly BoolToVisibilityConverter Inverse = new(true);

    private readonly bool _invert;

    private BoolToVisibilityConverter(bool invert)
    {
        _invert = invert;
    }

    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        var flag = value is bool b && b;
        if (_invert)
        {
            flag = !flag;
        }
        return flag ? true : false;
    }

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        throw new NotSupportedException();
    }
}
