using System;
using Avalonia.Media.Imaging;

namespace Mayhem.ViewModels;

public sealed class VideoFrameThumbnail : IDisposable
{
    private bool _disposed;

    public long TimestampMs { get; }
    public double X { get; set; }
    public Bitmap Bitmap { get; }

    public VideoFrameThumbnail(long timestampMs, Bitmap bitmap, double x)
    {
        TimestampMs = timestampMs;
        Bitmap = bitmap;
        X = x;
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        Bitmap.Dispose();
    }
}
