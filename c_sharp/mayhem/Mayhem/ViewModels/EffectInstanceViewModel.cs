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
    private IBrush? _fillCache;

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
            if (_fillCache != null) return _fillCache;
            var c = Effect.Color;
            _fillCache = new SolidColorBrush(Color.FromRgb(c.R, c.G, c.B));
            return _fillCache;
        }
    }

    private static readonly IBrush SelectedBorder = new SolidColorBrush(Color.Parse("#FF4444"));
    private static readonly IBrush UnselectedBorder = new SolidColorBrush(Color.Parse("#4A4A4A"));

    public IBrush Border => IsSelected ? SelectedBorder : UnselectedBorder;

    public Thickness BorderThickness => IsSelected ? new Thickness(2) : new Thickness(1);

    public string Label
    {
        get
        {
            if (Effect is ScriptEffect script)
            {
                return System.IO.Path.GetFileNameWithoutExtension(script.ScriptLink);
            }
            if (Effect is ParamSetEffect paramSet)
            {
                return $"Set {paramSet.ParamName}";
            }
            if (Effect is ParamChangeEffect paramChange)
            {
                return $"Change {paramChange.ParamName}";
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

    public void NotifyColorChanged()
    {
        _fillCache = null;
        OnPropertyChanged(nameof(Fill));
    }

    public void NotifyLabelChanged()
    {
        OnPropertyChanged(nameof(Label));
    }

    public void MoveToChannel(ChannelEntry channel)
    {
        Channel = channel;
    }
}
