using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;

namespace Mayhem.Models;

public sealed class Project
{
    public string MediaLink { get; set; }
    public ChannelList Channels { get; }
    public CueList Cues { get; }

    public Project(string mediaLink, ChannelList? channels = null, CueList? cues = null)
    {
        MediaLink = mediaLink;
        Channels = channels ?? new ChannelList();
        Cues = cues ?? new CueList();
    }

    public string ToJson() => JsonSerializer.Serialize(ToDto(), JsonUtil.Options);

    public static Project FromJson(string json)
    {
        var dto = JsonSerializer.Deserialize<ProjectDto>(json, JsonUtil.Options) ??
                  throw new JsonException("Failed to deserialize Project.");
        return FromDto(dto);
    }

    public void SaveAsJson(string path)
    {
        var json = ToJson();
        File.WriteAllText(path, json);
    }

    public static Project LoadFromJson(string path)
    {
        var json = File.ReadAllText(path);
        return FromJson(json);
    }

    internal ProjectDto ToDto()
    {
        return new ProjectDto
        {
            MediaLink = MediaLink,
            Channels = Channels.ToDtoList(),
            Cues = Cues.ToList()
        };
    }

    public static Project FromDto(ProjectDto dto)
    {
        var channels = ChannelList.FromDtoList(dto.Channels ?? new List<Channel.ChannelDto>());
        var cues = CueList.FromList(dto.Cues ?? new List<Cue>());
        return new Project(dto.MediaLink ?? string.Empty, channels, cues);
    }

    public sealed class ProjectDto
    {
        public string? MediaLink { get; set; }
        public List<Channel.ChannelDto>? Channels { get; set; }
        public List<Cue>? Cues { get; set; }
    }

}
