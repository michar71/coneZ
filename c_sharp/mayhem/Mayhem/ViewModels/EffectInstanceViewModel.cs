using Avalonia.Media;
using Avalonia;
using Effect = Mayhem.Models.Effect;
using Mayhem.Models;

namespace Mayhem.ViewModels;

public sealed class EffectInstanceViewModel : ObservableObject
{
    private double _x;
    private double _width;
    private bool _isSelected;

    public Effect Effect { get; }
    public ChannelEntry Channel { get; private set; }

    public EffectInstanceViewModel(Effect effect, ChannelEntry channel)
    {
        Effect = effect;
        Channel = channel;
    }

    public double X
    {
        get => _x;
        set => SetField(ref _x, value);
    }

    public double Width
    {
        get => _width;
        set => SetField(ref _width, value);
    }

    public bool IsSelected
    {
        get => _isSelected;
        set
        {
            if (SetField(ref _isSelected, value))
            {
                OnPropertyChanged(nameof(Border));
                OnPropertyChanged(nameof(BorderThickness));
            }
        }
    }

    public IBrush Fill
    {
        get
        {
            var c = Effect.Color;
            return new SolidColorBrush(Color.FromRgb(c.R, c.G, c.B));
        }
    }

    public IBrush Border
    {
        get => IsSelected ? new SolidColorBrush(Color.Parse("#FF4444")) : new SolidColorBrush(Color.Parse("#4A4A4A"));
    }

    public Thickness BorderThickness => IsSelected ? new Thickness(2) : new Thickness(1);

    public string Label
    {
        get
        {
            if (Effect is ScriptEffect script)
            {
                return System.IO.Path.GetFileNameWithoutExtension(script.ScriptLink);
            }
            return Effect.Type.ToString();
        }
    }

    public void UpdateLayout(MainWindowViewModel vm)
    {
        var start = Effect.StartMs;
        var end = Effect.StartMs + Effect.DurationMs;
        var x = vm.TimeToX(start);
        var width = vm.TimeToX(end) - x;
        if (width < 4)
        {
            width = 4;
        }
        X = x;
        Width = width;
    }

    public void MoveToChannel(ChannelEntry channel)
    {
        Channel = channel;
    }
}
