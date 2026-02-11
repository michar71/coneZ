using System;
using Avalonia.Collections;

namespace Mayhem.ViewModels;

public sealed class DebugLog
{
    private readonly AvaloniaList<string> _lines = new();

    public AvaloniaList<string> Lines => _lines;

    public void Add(string message)
    {
        var timestamp = DateTime.Now.ToString("HH:mm:ss");
        _lines.Add($"[{timestamp}] {message}");
    }

    public void Clear()
    {
        _lines.Clear();
    }
}
