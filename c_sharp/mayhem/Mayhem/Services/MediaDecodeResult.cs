using System.Collections.Generic;
using Mayhem.ViewModels;

namespace Mayhem.Services;

public sealed class MediaDecodeResult
{
    public MediaKind Kind { get; }
    public long DurationMs { get; }
    public DecodedAudio? Audio { get; }
    public DecodedVideo? Video { get; }

    public MediaDecodeResult(MediaKind kind, long durationMs, DecodedAudio? audio, DecodedVideo? video)
    {
        Kind = kind;
        DurationMs = durationMs;
        Audio = audio;
        Video = video;
    }
}

public sealed class DecodedAudio
{
    public short[] Samples { get; }
    public int SampleRate { get; }
    public int Channels { get; }
    public List<double> WaveformSamples { get; }

    public DecodedAudio(short[] samples, int sampleRate, int channels, List<double> waveformSamples)
    {
        Samples = samples;
        SampleRate = sampleRate;
        Channels = channels;
        WaveformSamples = waveformSamples;
    }
}

public sealed class DecodedVideo
{
    public int Width { get; }
    public int Height { get; }
    public List<VideoFrameData> Frames { get; }

    public DecodedVideo(int width, int height, List<VideoFrameData> frames)
    {
        Width = width;
        Height = height;
        Frames = frames;
    }
}

public sealed class VideoFrameData
{
    public long TimestampMs { get; }
    public byte[] Rgba { get; }
    public int Width { get; }
    public int Height { get; }

    public VideoFrameData(long timestampMs, byte[] rgba, int width, int height)
    {
        TimestampMs = timestampMs;
        Rgba = rgba;
        Width = width;
        Height = height;
    }
}
