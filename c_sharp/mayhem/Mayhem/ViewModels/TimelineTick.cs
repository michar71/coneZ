namespace Mayhem.ViewModels;

public sealed class TimelineTick
{
    public double X { get; }
    public string Label { get; }

    public TimelineTick(double x, string label)
    {
        X = x;
        Label = label;
    }
}
