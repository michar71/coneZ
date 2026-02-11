namespace Mayhem.Models;

public sealed class ScriptEffect : Effect
{
    public string ScriptLink { get; set; }
    public ParamList Params { get; }

    public ScriptEffect(int startMs, int durationMs, string scriptLink, ParamList? @params = null)
        : base(startMs, durationMs, EffectType.Script, RgbColor.LightGreen)
    {
        ScriptLink = scriptLink;
        Params = @params ?? new ParamList();
    }

    internal override EffectDto ToDto()
    {
        var dto = base.ToDto();
        dto.ScriptLink = ScriptLink;
        dto.Params = Params.ToList();
        return dto;
    }
}
