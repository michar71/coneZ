namespace Mayhem.Models;

public sealed class ColorEffect : Effect
{
    public RgbColor StartRgb { get; set; }
    public RgbColor EndRgb { get; set; }
    public int Offset { get; set; }
    public int Window { get; set; }

    public ColorEffect(int startMs, int durationMs, RgbColor startRgb, RgbColor endRgb, int offset = 0, int window = 100)
        : base(startMs, durationMs, EffectType.Color, startRgb)
    {
        StartRgb = startRgb;
        EndRgb = endRgb;
        Offset = offset;
        Window = window;
    }

    internal override EffectDto ToDto()
    {
        var dto = base.ToDto();
        dto.StartRgb = StartRgb;
        dto.EndRgb = EndRgb;
        dto.Offset = Offset;
        dto.Window = Window;
        return dto;
    }
}
