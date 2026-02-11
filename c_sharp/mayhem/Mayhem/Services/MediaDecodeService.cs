using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using FFmpeg.AutoGen;
using Mayhem.ViewModels;

namespace Mayhem.Services;

public sealed unsafe class MediaDecodeService
{
    private const int TargetWaveformSamples = 2000;
    private const int TargetVideoFps = 10;

    public Task<MediaDecodeResult> DecodeAsync(string path, CancellationToken cancellationToken, Action<string>? log = null)
    {
        return Task.Run(() => DecodeInternal(path, cancellationToken, log), cancellationToken);
    }

    private static MediaDecodeResult DecodeInternal(string path, CancellationToken cancellationToken, Action<string>? log)
    {
        if (!File.Exists(path))
        {
            log?.Invoke("FFmpeg decode aborted: file does not exist.");
            return new MediaDecodeResult(MediaKind.None, 0, null, null);
        }

        FfmpegLoader.Initialize(log);

        AVFormatContext* formatContext = null;
        try
        {
            if (ffmpeg.avformat_open_input(&formatContext, path, null, null) != 0)
            {
                log?.Invoke("FFmpeg avformat_open_input failed.");
                return new MediaDecodeResult(MediaKind.Unknown, 0, null, null);
            }
        }
        catch (NotSupportedException ex)
        {
            log?.Invoke($"FFmpeg avformat_open_input not supported: {ex.Message}");
            return new MediaDecodeResult(MediaKind.Unknown, 0, null, null);
        }

        try
        {
            if (ffmpeg.avformat_find_stream_info(formatContext, null) != 0)
            {
                log?.Invoke("FFmpeg avformat_find_stream_info failed.");
                return new MediaDecodeResult(MediaKind.Unknown, 0, null, null);
            }

            var audioStreamIndex = ffmpeg.av_find_best_stream(formatContext, AVMediaType.AVMEDIA_TYPE_AUDIO, -1, -1, null, 0);
            var videoStreamIndex = ffmpeg.av_find_best_stream(formatContext, AVMediaType.AVMEDIA_TYPE_VIDEO, -1, -1, null, 0);

            DecodedAudio? audio = null;
            DecodedVideo? video = null;

            log?.Invoke($"FFmpeg stream indexes: audio={audioStreamIndex}, video={videoStreamIndex}");

            if (audioStreamIndex >= 0)
            {
                audio = DecodeAudio(path, cancellationToken);
                log?.Invoke($"FFmpeg audio decode: samples={(audio.Samples.Length)}, rate={audio.SampleRate}, channels={audio.Channels}");
            }

            if (videoStreamIndex >= 0)
            {
                video = DecodeVideo(path, cancellationToken);
                log?.Invoke($"FFmpeg video decode: frames={video.Frames.Count}, size={video.Width}x{video.Height}");
            }

            var durationMs = formatContext->duration > 0
                ? (long)(formatContext->duration * 1000.0 / ffmpeg.AV_TIME_BASE)
                : 0;

            var kind = MediaKind.None;
            if (video != null && video.Frames.Count > 0)
            {
                kind = MediaKind.Video;
            }
            else if (audio != null && audio.Samples.Length > 0)
            {
                kind = MediaKind.Audio;
            }

            return new MediaDecodeResult(kind, durationMs, audio, video);
        }
        finally
        {
            ffmpeg.avformat_close_input(&formatContext);
        }
    }

    private static DecodedAudio DecodeAudio(string path, CancellationToken cancellationToken)
    {
        AVFormatContext* formatContext = null;
        if (ffmpeg.avformat_open_input(&formatContext, path, null, null) != 0)
        {
            return new DecodedAudio(Array.Empty<short>(), 0, 0, new List<double>());
        }

        try
        {
            if (ffmpeg.avformat_find_stream_info(formatContext, null) != 0)
            {
                return new DecodedAudio(Array.Empty<short>(), 0, 0, new List<double>());
            }

            var streamIndex = ffmpeg.av_find_best_stream(formatContext, AVMediaType.AVMEDIA_TYPE_AUDIO, -1, -1, null, 0);
            if (streamIndex < 0)
            {
                return new DecodedAudio(Array.Empty<short>(), 0, 0, new List<double>());
            }

            var stream = formatContext->streams[streamIndex];
            var codecParameters = stream->codecpar;
            var codec = ffmpeg.avcodec_find_decoder(codecParameters->codec_id);
            if (codec == null)
            {
                return new DecodedAudio(Array.Empty<short>(), 0, 0, new List<double>());
            }

            var codecContext = ffmpeg.avcodec_alloc_context3(codec);
            if (codecContext == null)
            {
                return new DecodedAudio(Array.Empty<short>(), 0, 0, new List<double>());
            }

            try
            {
                if (ffmpeg.avcodec_parameters_to_context(codecContext, codecParameters) < 0)
                {
                    return new DecodedAudio(Array.Empty<short>(), 0, 0, new List<double>());
                }

                if (ffmpeg.avcodec_open2(codecContext, codec, null) < 0)
                {
                    return new DecodedAudio(Array.Empty<short>(), 0, 0, new List<double>());
                }

                var outputChannels = Math.Min(2, codecContext->ch_layout.nb_channels);
                var outLayout = new AVChannelLayout();
                ffmpeg.av_channel_layout_default(&outLayout, outputChannels);

                var swr = ffmpeg.swr_alloc();
                ffmpeg.av_opt_set_chlayout(swr, "in_chlayout", &codecContext->ch_layout, 0);
                ffmpeg.av_opt_set_int(swr, "in_sample_rate", codecContext->sample_rate, 0);
                ffmpeg.av_opt_set_sample_fmt(swr, "in_sample_fmt", codecContext->sample_fmt, 0);
                ffmpeg.av_opt_set_chlayout(swr, "out_chlayout", &outLayout, 0);
                ffmpeg.av_opt_set_int(swr, "out_sample_rate", codecContext->sample_rate, 0);
                ffmpeg.av_opt_set_sample_fmt(swr, "out_sample_fmt", AVSampleFormat.AV_SAMPLE_FMT_S16, 0);
                ffmpeg.swr_init(swr);

                var packet = ffmpeg.av_packet_alloc();
                var frame = ffmpeg.av_frame_alloc();
                var samples = new List<short>();

                try
                {
                    byte** outData = stackalloc byte*[1];
                    while (ffmpeg.av_read_frame(formatContext, packet) >= 0)
                    {
                        if (cancellationToken.IsCancellationRequested)
                        {
                            break;
                        }

                        if (packet->stream_index != streamIndex)
                        {
                            ffmpeg.av_packet_unref(packet);
                            continue;
                        }

                        if (ffmpeg.avcodec_send_packet(codecContext, packet) < 0)
                        {
                            ffmpeg.av_packet_unref(packet);
                            continue;
                        }

                        ffmpeg.av_packet_unref(packet);

                        while (ffmpeg.avcodec_receive_frame(codecContext, frame) == 0)
                        {
                            var outSamples = ffmpeg.swr_get_out_samples(swr, frame->nb_samples);
                            var outBufferSize = ffmpeg.av_samples_get_buffer_size(null, outputChannels, outSamples, AVSampleFormat.AV_SAMPLE_FMT_S16, 1);
                            if (outBufferSize <= 0)
                            {
                                ffmpeg.av_frame_unref(frame);
                                continue;
                            }

                            var outBuffer = (byte*)ffmpeg.av_malloc((ulong)outBufferSize);
                            try
                            {
                            outData[0] = outBuffer;
                            var outSamplesConverted = ffmpeg.swr_convert(swr, outData, outSamples, frame->extended_data, frame->nb_samples);
                                if (outSamplesConverted > 0)
                                {
                                    var totalSamples = outSamplesConverted * outputChannels;
                                    for (var i = 0; i < totalSamples; i++)
                                    {
                                        var value = *(short*)(outBuffer + i * sizeof(short));
                                        samples.Add(value);
                                    }
                                }
                            }
                            finally
                            {
                                ffmpeg.av_free(outBuffer);
                            }

                            ffmpeg.av_frame_unref(frame);
                        }
                    }
                }
                finally
                {
                    ffmpeg.av_packet_free(&packet);
                    ffmpeg.av_frame_free(&frame);
                    ffmpeg.swr_free(&swr);
                    ffmpeg.av_channel_layout_uninit(&outLayout);
                }

                var waveform = BuildWaveform(samples, outputChannels);
                return new DecodedAudio(samples.ToArray(), codecContext->sample_rate, outputChannels, waveform);
            }
            finally
            {
                ffmpeg.avcodec_free_context(&codecContext);
            }
        }
        finally
        {
            ffmpeg.avformat_close_input(&formatContext);
        }
    }

    private static List<double> BuildWaveform(List<short> samples, int channels)
    {
        if (samples.Count == 0 || channels == 0)
        {
            return new List<double>();
        }

        var totalSamples = samples.Count / channels;
        var samplesPerBucket = Math.Max(1, totalSamples / TargetWaveformSamples);
        var buckets = Math.Max(1, totalSamples / samplesPerBucket);
        var waveform = new List<double>(buckets);

        for (var i = 0; i < buckets; i++)
        {
            double max = 0;
            var start = i * samplesPerBucket;
            var end = Math.Min(totalSamples, start + samplesPerBucket);
            for (var s = start; s < end; s++)
            {
                var baseIndex = s * channels;
                for (var ch = 0; ch < channels; ch++)
                {
                    var sample = samples[baseIndex + ch];
                    var amp = Math.Abs((int)sample) / 32768.0;
                    if (amp > max)
                    {
                        max = amp;
                    }
                }
            }
            waveform.Add(max);
        }

        return waveform;
    }

    private static DecodedVideo DecodeVideo(string path, CancellationToken cancellationToken)
    {
        AVFormatContext* formatContext = null;
        if (ffmpeg.avformat_open_input(&formatContext, path, null, null) != 0)
        {
            return new DecodedVideo(0, 0, new List<VideoFrameData>());
        }

        try
        {
            if (ffmpeg.avformat_find_stream_info(formatContext, null) != 0)
            {
                return new DecodedVideo(0, 0, new List<VideoFrameData>());
            }

            var streamIndex = ffmpeg.av_find_best_stream(formatContext, AVMediaType.AVMEDIA_TYPE_VIDEO, -1, -1, null, 0);
            if (streamIndex < 0)
            {
                return new DecodedVideo(0, 0, new List<VideoFrameData>());
            }

            var stream = formatContext->streams[streamIndex];
            var codecParameters = stream->codecpar;
            var codec = ffmpeg.avcodec_find_decoder(codecParameters->codec_id);
            if (codec == null)
            {
                return new DecodedVideo(0, 0, new List<VideoFrameData>());
            }

            var codecContext = ffmpeg.avcodec_alloc_context3(codec);
            if (codecContext == null)
            {
                return new DecodedVideo(0, 0, new List<VideoFrameData>());
            }

            try
            {
                if (ffmpeg.avcodec_parameters_to_context(codecContext, codecParameters) < 0)
                {
                    return new DecodedVideo(0, 0, new List<VideoFrameData>());
                }

                if (ffmpeg.avcodec_open2(codecContext, codec, null) < 0)
                {
                    return new DecodedVideo(0, 0, new List<VideoFrameData>());
                }

                var width = codecContext->width;
                var height = codecContext->height;
                var sws = ffmpeg.sws_getContext(width, height, codecContext->pix_fmt,
                    width, height, AVPixelFormat.AV_PIX_FMT_BGRA, (int)SwsFlags.SWS_BICUBIC, null, null, null);

                var packet = ffmpeg.av_packet_alloc();
                var frame = ffmpeg.av_frame_alloc();
                var rgbFrame = ffmpeg.av_frame_alloc();

                var bufferSize = ffmpeg.av_image_get_buffer_size(AVPixelFormat.AV_PIX_FMT_BGRA, width, height, 1);
                var buffer = (byte*)ffmpeg.av_malloc((ulong)bufferSize);
                var dstData4 = new byte_ptrArray4();
                var dstLinesize4 = new int_array4();
                ffmpeg.av_image_fill_arrays(ref dstData4, ref dstLinesize4, buffer, AVPixelFormat.AV_PIX_FMT_BGRA, width, height, 1);
                rgbFrame->data[0] = dstData4[0];
                rgbFrame->linesize[0] = dstLinesize4[0];

                var frames = new List<VideoFrameData>();
                var timeBase = stream->time_base;
                var nextFrameMs = 0L;
                var frameIntervalMs = 1000L / TargetVideoFps;

                try
                {
                    while (ffmpeg.av_read_frame(formatContext, packet) >= 0)
                    {
                        if (cancellationToken.IsCancellationRequested)
                        {
                            break;
                        }

                        if (packet->stream_index != streamIndex)
                        {
                            ffmpeg.av_packet_unref(packet);
                            continue;
                        }

                        if (ffmpeg.avcodec_send_packet(codecContext, packet) < 0)
                        {
                            ffmpeg.av_packet_unref(packet);
                            continue;
                        }

                        ffmpeg.av_packet_unref(packet);

                        while (ffmpeg.avcodec_receive_frame(codecContext, frame) == 0)
                        {
                            var pts = frame->best_effort_timestamp;
                            if (pts == ffmpeg.AV_NOPTS_VALUE)
                            {
                                ffmpeg.av_frame_unref(frame);
                                continue;
                            }

                            var timeSeconds = pts * ffmpeg.av_q2d(timeBase);
                            var timestampMs = (long)(timeSeconds * 1000.0);

                            if (timestampMs < nextFrameMs)
                            {
                                ffmpeg.av_frame_unref(frame);
                                continue;
                            }

                            ffmpeg.sws_scale(sws, frame->data, frame->linesize, 0, height, rgbFrame->data, rgbFrame->linesize);

                            var managed = new byte[bufferSize];
                            Marshal.Copy((IntPtr)buffer, managed, 0, bufferSize);
                            frames.Add(new VideoFrameData(timestampMs, managed, width, height));

                            nextFrameMs += frameIntervalMs;
                            ffmpeg.av_frame_unref(frame);
                        }
                    }
                }
                finally
                {
                    ffmpeg.av_packet_free(&packet);
                    ffmpeg.av_frame_free(&frame);
                    ffmpeg.av_frame_free(&rgbFrame);
                    ffmpeg.av_free(buffer);
                    ffmpeg.sws_freeContext(sws);
                }

                return new DecodedVideo(width, height, frames);
            }
            finally
            {
                ffmpeg.avcodec_free_context(&codecContext);
            }
        }
        finally
        {
            ffmpeg.avformat_close_input(&formatContext);
        }
    }
}
