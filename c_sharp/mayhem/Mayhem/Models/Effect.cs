using System;
using System.Collections.Generic;
using System.Text.Json;

namespace Mayhem.Models;

public abstract class Effect
{
    public int StartMs { get; set; }
    public int DurationMs { get; set; }
    public EffectType Type { get; protected set; }
    public RgbColor Color { get; protected set; }

    protected Effect(int startMs, int durationMs, EffectType type, RgbColor color)
    {
        StartMs = startMs;
        DurationMs = durationMs;
        Type = type;
        Color = color;
    }

    internal virtual EffectDto ToDto()
    {
        return new EffectDto
        {
            StartMs = StartMs,
            DurationMs = DurationMs,
            Type = Type,
            Color = Color
        };
    }

    public string ToJson() => JsonSerializer.Serialize(ToDto(), JsonUtil.Options);

    public static Effect FromJson(string json)
    {
        var dto = JsonSerializer.Deserialize<EffectDto>(json, JsonUtil.Options) ??
                  throw new JsonException("Failed to deserialize Effect.");
        return FromDto(dto);
    }

    internal static Effect FromDto(EffectDto dto)
    {
        Effect effect = dto.Type switch
        {
            EffectType.Color => new ColorEffect(dto.StartMs, dto.DurationMs,
                NormalizeColor(dto.StartRgb, RgbColor.Yellow),
                NormalizeColor(dto.EndRgb, RgbColor.Yellow)),
            EffectType.Fx => new FxEffect(dto.StartMs, dto.DurationMs, dto.FxId ?? 0,
                ParamList.FromList(dto.Params ?? new List<float>())),
            EffectType.Script => new ScriptEffect(dto.StartMs, dto.DurationMs,
                dto.ScriptLink ?? string.Empty,
                ParamList.FromList(dto.Params ?? new List<float>())),
            EffectType.Media => new MediaEffect(dto.StartMs, dto.DurationMs,
                dto.MediaLink ?? string.Empty),
            _ => throw new JsonException($"Unsupported effect type: {dto.Type}")
        };
        effect.Color = NormalizeColor(dto.Color, effect.Color);
        if (effect is ColorEffect colorEffect)
        {
            effect.Color = colorEffect.StartRgb;
        }
        return effect;
    }

    internal void SetColor(RgbColor color)
    {
        Color = color;
    }

    private static RgbColor NormalizeColor(RgbColor? color, RgbColor fallback)
    {
        if (color == null)
        {
            return fallback;
        }

        return color.Value;
    }

    public sealed class EffectDto
    {
        public int StartMs { get; set; }
        public int DurationMs { get; set; }
        public EffectType Type { get; set; }
        public RgbColor Color { get; set; }

        public RgbColor? StartRgb { get; set; }
        public RgbColor? EndRgb { get; set; }

        public uint? FxId { get; set; }
        public List<float>? Params { get; set; }

        public string? ScriptLink { get; set; }
        public string? MediaLink { get; set; }
    }
}
