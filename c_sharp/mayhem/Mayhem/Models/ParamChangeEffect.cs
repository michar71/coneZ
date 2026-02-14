namespace Mayhem.Models;

public sealed class ParamChangeEffect : Effect
{
    public string ParamName { get; set; }
    public float StartValue { get; set; }
    public float EndValue { get; set; }
    public int Offset { get; set; }
    public int Window { get; set; }

    public ParamChangeEffect(
        int startMs,
        int durationMs,
        string paramName,
        float startValue,
        float endValue,
        int offset = 0,
        int window = 100)
        : base(startMs, durationMs, EffectType.ParamChange, new RgbColor(170, 255, 190))
    {
        ParamName = paramName;
        StartValue = startValue;
        EndValue = endValue;
        Offset = offset;
        Window = window;
    }

    internal override EffectDto ToDto()
    {
        var dto = base.ToDto();
        dto.ParamName = ParamName;
        dto.ParamStartValue = StartValue;
        dto.ParamEndValue = EndValue;
        dto.Offset = Offset;
        dto.Window = Window;
        return dto;
    }
}
