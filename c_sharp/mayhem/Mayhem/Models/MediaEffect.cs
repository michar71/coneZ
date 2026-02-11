namespace Mayhem.Models;

public sealed class MediaEffect : Effect
{
    public string MediaLink { get; set; }

    public MediaEffect(int startMs, int durationMs, string mediaLink)
        : base(startMs, durationMs, EffectType.Media, RgbColor.Pink)
    {
        MediaLink = mediaLink;
    }

    internal override EffectDto ToDto()
    {
        var dto = base.ToDto();
        dto.MediaLink = MediaLink;
        return dto;
    }
}
