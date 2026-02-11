using System;
using System.Collections.Generic;
using System.Text.Json;

namespace Mayhem.Models;

public sealed class Channel
{
    public int ChannelId { get; set; }
    public string ChannelName { get; set; }
    public EffectList Effects { get; }

    public Channel(int channelId, string channelName, EffectList? effects = null)
    {
        ChannelId = channelId;
        ChannelName = channelName;
        Effects = effects ?? new EffectList();
    }

    public string ToJson() => JsonSerializer.Serialize(ToDto(), JsonUtil.Options);

    public static Channel FromJson(string json)
    {
        var dto = JsonSerializer.Deserialize<ChannelDto>(json, JsonUtil.Options) ??
                  throw new JsonException("Failed to deserialize Channel.");
        return FromDto(dto);
    }

    internal ChannelDto ToDto()
    {
        return new ChannelDto
        {
            ChannelId = ChannelId,
            ChannelName = ChannelName,
            Effects = Effects.ToDtoList()
        };
    }

    internal static Channel FromDto(ChannelDto dto)
    {
        var effects = EffectList.FromDtoList(dto.Effects ?? new List<Effect.EffectDto>());
        return new Channel(dto.ChannelId, dto.ChannelName ?? string.Empty, effects);
    }

    public sealed class ChannelDto
    {
        public int ChannelId { get; set; }
        public string? ChannelName { get; set; }
        public List<Effect.EffectDto>? Effects { get; set; }
    }
}
