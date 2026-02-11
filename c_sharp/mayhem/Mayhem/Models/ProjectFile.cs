using System;
using System.IO;
using System.Text.Json;

namespace Mayhem.Models;

public sealed class ProjectFile
{
    public int Version { get; set; } = 1;
    public Project.ProjectDto? Project { get; set; }
    public LayoutSettings? Layout { get; set; }

    public static void Save(string path, Project project, LayoutSettings layout)
    {
        var file = new ProjectFile
        {
            Version = 1,
            Project = project.ToDto(),
            Layout = layout
        };

        var json = JsonSerializer.Serialize(file, JsonUtil.Options);
        File.WriteAllText(path, json);
    }

    public static ProjectFile Load(string path)
    {
        var json = File.ReadAllText(path);
        return JsonSerializer.Deserialize<ProjectFile>(json, JsonUtil.Options) ??
               throw new JsonException("Failed to deserialize project file.");
    }

    public Project ToProject()
    {
        if (Project == null)
        {
            return new Project(string.Empty);
        }

        return Mayhem.Models.Project.FromDto(Project);
    }
}

public sealed class LayoutSettings
{
    public double WindowWidth { get; set; }
    public double WindowHeight { get; set; }
    public double LeftPanelWidth { get; set; }
    public double RightPanelWidth { get; set; }
    public double DebugPanelHeight { get; set; }
    public double VideoPanelHeight { get; set; }
    public double ChannelLaneHeight { get; set; }
    public double Zoom { get; set; }
}
