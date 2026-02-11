using System.Text.Json;

namespace Mayhem.Models;

public sealed class Cue
{
    public int TimeMs { get; set; }
    public CueType Type { get; set; }
    public RgbColor Color { get; set; }

    public Cue(int timeMs, CueType type, RgbColor color)
    {
        TimeMs = timeMs;
        Type = type;
        Color = color;
    }

    public static Cue CreateUserCue(int timeMs) => new(timeMs, CueType.UserCue, RgbColor.Red);

    public string ToJson() => JsonSerializer.Serialize(this, JsonUtil.Options);

    public static Cue FromJson(string json) =>
        JsonSerializer.Deserialize<Cue>(json, JsonUtil.Options) ??
        throw new JsonException("Failed to deserialize Cue.");
}
