namespace Mayhem.Models;

public sealed class FxEffect : Effect
{
    public uint FxId { get; set; }
    public ParamList Params { get; }

    public FxEffect(int startMs, int durationMs, uint fxId, ParamList? @params = null)
        : base(startMs, durationMs, EffectType.Fx, RgbColor.LightBlue)
    {
        FxId = fxId;
        Params = @params ?? new ParamList();
    }

    internal override EffectDto ToDto()
    {
        var dto = base.ToDto();
        dto.FxId = FxId;
        dto.Params = Params.ToList();
        return dto;
    }
}
