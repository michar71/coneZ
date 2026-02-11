using Avalonia.Media.Imaging;

namespace Mayhem.ViewModels;

public sealed class VideoFrameThumbnail
{
    public long TimestampMs { get; }
    public double X { get; set; }
    public Bitmap Bitmap { get; }

    public VideoFrameThumbnail(long timestampMs, Bitmap bitmap, double x)
    {
        TimestampMs = timestampMs;
        Bitmap = bitmap;
        X = x;
    }
}
