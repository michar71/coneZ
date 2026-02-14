namespace Mayhem.Models;

public sealed class ParamSetEffect : Effect
{
    public string ParamName { get; set; }
    public float Value { get; set; }

    public ParamSetEffect(int startMs, int durationMs, string paramName, float value)
        : base(startMs, durationMs, EffectType.ParamSet, new RgbColor(180, 220, 255))
    {
        ParamName = paramName;
        Value = value;
    }

    internal override EffectDto ToDto()
    {
        var dto = base.ToDto();
        dto.ParamName = ParamName;
        dto.ParamValue = Value;
        return dto;
    }
}
