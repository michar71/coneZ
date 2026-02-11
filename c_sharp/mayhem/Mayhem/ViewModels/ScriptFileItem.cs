namespace Mayhem.ViewModels;

public sealed class ScriptFileItem
{
    public ScriptFileItem(string name, string relativePath, string fullPath)
    {
        Name = name;
        RelativePath = relativePath;
        FullPath = fullPath;
    }

    public string Name { get; }
    public string RelativePath { get; }
    public string FullPath { get; }
}
