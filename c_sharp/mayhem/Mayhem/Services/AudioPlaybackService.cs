using System;
using OpenTK.Audio.OpenAL;

namespace Mayhem.Services;

public sealed class AudioPlaybackService : IDisposable
{
    private ALDevice _device;
    private ALContext _context;
    private int _buffer;
    private int _source;
    private bool _initialized;
    private int _channels;
    private int _sampleRate;
    private short[]? _samples;
    private int _queuedOffset;

    public void Initialize()
    {
        if (_initialized)
        {
            return;
        }

        _device = ALC.OpenDevice(null);
        if (_device == ALDevice.Null)
        {
            return;
        }

        unsafe
        {
            _context = ALC.CreateContext(_device, (int*)null);
        }
        if (_context == ALContext.Null)
        {
            ALC.CloseDevice(_device);
            _device = ALDevice.Null;
            return;
        }
        ALC.MakeContextCurrent(_context);

        _buffer = AL.GenBuffer();
        _source = AL.GenSource();
        if (AL.GetError() != ALError.NoError)
        {
            ALC.MakeContextCurrent(ALContext.Null);
            ALC.DestroyContext(_context);
            _context = ALContext.Null;
            ALC.CloseDevice(_device);
            _device = ALDevice.Null;
            return;
        }
        _initialized = true;
    }

    public void LoadPcm(short[] samples, int sampleRate, int channels)
    {
        Initialize();
        _channels = channels;
        _sampleRate = sampleRate;
        _samples = samples;
        _queuedOffset = 0;

        var format = channels switch
        {
            1 => ALFormat.Mono16,
            2 => ALFormat.Stereo16,
            _ => ALFormat.Stereo16
        };

        QueueBuffer(samples, 0, format, sampleRate);
    }

    public void Play()
    {
        if (!_initialized) return;
        AL.SourcePlay(_source);
    }

    public void Pause()
    {
        if (!_initialized) return;
        AL.SourcePause(_source);
    }

    public void Stop()
    {
        if (!_initialized) return;
        AL.SourceStop(_source);
    }

    public bool IsPlaying
    {
        get
        {
            if (!_initialized) return false;
            AL.GetSource(_source, ALGetSourcei.SourceState, out int state);
            return (ALSourceState)state == ALSourceState.Playing;
        }
    }

    public double GetPositionSeconds()
    {
        if (!_initialized) return 0;
        AL.GetSource(_source, ALSourcef.SecOffset, out var seconds);
        return seconds;
    }

    public void SeekSeconds(double seconds)
    {
        if (!_initialized) return;
        if (seconds < 0)
        {
            seconds = 0;
        }
        if (_sampleRate <= 0 || _samples == null)
        {
            return;
        }

        var frameOffset = (int)Math.Round(seconds * _sampleRate);
        if (frameOffset < 0)
        {
            frameOffset = 0;
        }

        var totalFrames = _samples.Length / Math.Max(1, _channels);
        if (frameOffset >= totalFrames)
        {
            frameOffset = totalFrames - 1;
        }

        _queuedOffset = frameOffset;
        RequeueFromOffset();
    }

    private void RequeueFromOffset()
    {
        if (_samples == null)
        {
            return;
        }

        var format = _channels switch
        {
            1 => ALFormat.Mono16,
            2 => ALFormat.Stereo16,
            _ => ALFormat.Stereo16
        };

        QueueBuffer(_samples, _queuedOffset, format, _sampleRate);
    }

    private void QueueBuffer(short[] samples, int frameOffset, ALFormat format, int sampleRate)
    {
        var sampleOffset = frameOffset * Math.Max(1, _channels);
        if (sampleOffset < 0 || sampleOffset >= samples.Length)
        {
            sampleOffset = 0;
        }

        var remainingSamples = samples.Length - sampleOffset;
        if (remainingSamples <= 0)
        {
            remainingSamples = samples.Length;
            sampleOffset = 0;
        }

        var byteCount = remainingSamples * sizeof(short);

        AL.SourceStop(_source);
        AL.Source(_source, ALSourcei.Buffer, 0);

        unsafe
        {
            fixed (short* ptr = samples)
            {
                var offsetPtr = (byte*)(ptr + sampleOffset);
                AL.BufferData(_buffer, format, (nint)offsetPtr, byteCount, sampleRate);
            }
        }
        AL.Source(_source, ALSourcei.Buffer, _buffer);
    }

    public void Dispose()
    {
        if (!_initialized)
        {
            return;
        }

        AL.SourceStop(_source);
        AL.DeleteSource(_source);
        AL.DeleteBuffer(_buffer);

        if (_context != ALContext.Null)
        {
            ALC.MakeContextCurrent(ALContext.Null);
            ALC.DestroyContext(_context);
            _context = ALContext.Null;
        }

        if (_device != ALDevice.Null)
        {
            ALC.CloseDevice(_device);
            _device = ALDevice.Null;
        }

        _initialized = false;
    }
}
