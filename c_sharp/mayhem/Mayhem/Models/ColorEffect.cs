namespace Mayhem.Models;

public sealed class ColorEffect : Effect
{
    public RgbColor StartRgb { get; set; }
    public RgbColor EndRgb { get; set; }

    public ColorEffect(int startMs, int durationMs, RgbColor startRgb, RgbColor endRgb)
        : base(startMs, durationMs, EffectType.Color, RgbColor.Yellow)
    {
        StartRgb = startRgb;
        EndRgb = endRgb;
    }

    internal override EffectDto ToDto()
    {
        var dto = base.ToDto();
        dto.StartRgb = StartRgb;
        dto.EndRgb = EndRgb;
        return dto;
    }
}
