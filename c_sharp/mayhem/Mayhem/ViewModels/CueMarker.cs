using Mayhem.Models;

namespace Mayhem.ViewModels;

public sealed class CueMarker : ObservableObject
{
    public Cue Cue { get; }
    private double _x;
    private bool _isSelected;

    public CueMarker(Cue cue, double x)
    {
        Cue = cue;
        _x = x;
    }

    public double X
    {
        get => _x;
        set => SetField(ref _x, value);
    }

    public bool IsSelected
    {
        get => _isSelected;
        set => SetField(ref _isSelected, value);
    }
}
