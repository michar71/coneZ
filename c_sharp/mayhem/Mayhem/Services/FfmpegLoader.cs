using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using FFmpeg.AutoGen;

namespace Mayhem.Services;

public static class FfmpegLoader
{
    private static bool _initialized;
    private static readonly List<nint> _handles = new();

    public static void Initialize(Action<string>? log = null)
    {
        if (_initialized)
        {
            return;
        }

        var rootPath = ResolveFFmpegPath();
        if (string.IsNullOrWhiteSpace(rootPath))
        {
            throw new DirectoryNotFoundException("FFmpeg libraries not found. Set FFMPEG_PATH to the directory containing libavformat.");
        }

        log?.Invoke($"FFmpeg root path: {rootPath}");
        ffmpeg.RootPath = rootPath;
        RegisterResolver(rootPath, log);
        LoadNativeLibraries(rootPath, log);

        try
        {
            ffmpeg.av_log_set_level(ffmpeg.AV_LOG_ERROR);
        }
        catch (NotSupportedException)
        {
            // Some dynamic bindings don't expose this API on all platforms.
        }

        try
        {
            ffmpeg.avformat_network_init();
        }
        catch (NotSupportedException)
        {
            // Safe to ignore for local file decoding.
        }

        try
        {
            var version = ffmpeg.avformat_version();
            log?.Invoke($"FFmpeg avformat_version: {version}");
        }
        catch (Exception ex)
        {
            log?.Invoke($"FFmpeg avformat_version failed: {ex.Message}");
        }
        _initialized = true;
    }

    private static string? ResolveFFmpegPath()
    {
        var env = Environment.GetEnvironmentVariable("FFMPEG_PATH");
        if (!string.IsNullOrWhiteSpace(env) && Directory.Exists(env))
        {
            if (HasFfmpegLibraries(env))
            {
                return env;
            }
        }

        var candidates = new List<string>
        {
            // macOS Homebrew
            "/opt/homebrew/opt/ffmpeg/lib",
            "/usr/local/opt/ffmpeg/lib",
            // Linux standard paths
            "/usr/lib/x86_64-linux-gnu",
            "/usr/lib/aarch64-linux-gnu",
            "/usr/lib64",
            "/usr/lib",
            // Generic
            "/usr/local/lib",
            "/opt/local/lib"
        };

        // If ffmpeg is on PATH, check for libraries relative to the binary
        var ffmpegBin = FindOnPath("ffmpeg");
        if (ffmpegBin != null)
        {
            var binDir = Path.GetDirectoryName(ffmpegBin);
            if (binDir != null)
            {
                var parentDir = Path.GetDirectoryName(binDir);
                if (parentDir != null)
                {
                    candidates.Insert(0, Path.Combine(parentDir, "lib"));
                    if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
                    {
                        var arch = RuntimeInformation.OSArchitecture switch
                        {
                            Architecture.X64 => "x86_64-linux-gnu",
                            Architecture.Arm64 => "aarch64-linux-gnu",
                            _ => null
                        };
                        if (arch != null)
                            candidates.Insert(0, Path.Combine(parentDir, "lib", arch));
                    }
                }
            }
        }

        foreach (var candidate in candidates)
        {
            if (Directory.Exists(candidate) && HasFfmpegLibraries(candidate))
            {
                return candidate;
            }
        }

        return null;
    }

    private static void LoadNativeLibraries(string rootPath, Action<string>? log)
    {
        var libraries = new[]
        {
            "libavutil",
            "libswresample",
            "libswscale",
            "libavcodec",
            "libavformat"
        };

        foreach (var name in libraries)
        {
            var path = FindLibraryPath(rootPath, name);
            if (path == null)
            {
                log?.Invoke($"FFmpeg library not found: {name}");
                continue;
            }

            try
            {
                var handle = NativeLibrary.Load(path);
                _handles.Add(handle);
                log?.Invoke($"Loaded FFmpeg library: {path}");
            }
            catch (Exception ex)
            {
                log?.Invoke($"Failed to load FFmpeg library {path}: {ex.Message}");
            }
        }
    }

    private static void RegisterResolver(string rootPath, Action<string>? log)
    {
        NativeLibrary.SetDllImportResolver(typeof(ffmpeg).Assembly, (name, assembly, path) =>
        {
            if (name.Contains("libdl", StringComparison.OrdinalIgnoreCase))
            {
                try
                {
                    var handle = NativeLibrary.Load("/usr/lib/libSystem.B.dylib");
                    log?.Invoke("Resolver: mapped libdl -> /usr/lib/libSystem.B.dylib");
                    return handle;
                }
                catch (Exception ex)
                {
                    log?.Invoke($"Resolver: failed to map libdl: {ex.Message}");
                    return IntPtr.Zero;
                }
            }

            var baseName = NormalizeLibraryName(name);
            var resolved = FindLibraryPath(rootPath, baseName);
            if (resolved == null)
            {
                try
                {
                    var handle = NativeLibrary.Load(name);
                    log?.Invoke($"Resolver: fallback loaded {name}");
                    return handle;
                }
                catch
                {
                    log?.Invoke($"Resolver: no match for {name} in {rootPath}");
                    return IntPtr.Zero;
                }
            }

            try
            {
                var handle = NativeLibrary.Load(resolved);
                log?.Invoke($"Resolver: loaded {resolved} for {name}");
                return handle;
            }
            catch (Exception ex)
            {
                log?.Invoke($"Resolver: failed to load {resolved} for {name}: {ex.Message}");
                return IntPtr.Zero;
            }
        });
    }

    private static string NormalizeLibraryName(string name)
    {
        var trimmed = name;
        if (trimmed.StartsWith("lib", StringComparison.OrdinalIgnoreCase))
        {
            trimmed = trimmed[3..];
        }

        if (trimmed.EndsWith(".dylib", StringComparison.OrdinalIgnoreCase) ||
            trimmed.EndsWith(".so", StringComparison.OrdinalIgnoreCase) ||
            trimmed.EndsWith(".dll", StringComparison.OrdinalIgnoreCase))
        {
            trimmed = Path.GetFileNameWithoutExtension(trimmed);
        }

        return $"lib{trimmed}";
    }

    private static string? FindLibraryPath(string rootPath, string baseName)
    {
        var dylib = Directory.GetFiles(rootPath, $"{baseName}*.dylib");
        if (dylib.Length > 0)
        {
            return dylib[0];
        }

        var so = Directory.GetFiles(rootPath, $"{baseName}*.so");
        if (so.Length > 0)
        {
            return so[0];
        }

        var dll = Directory.GetFiles(rootPath, $"{baseName}*.dll");
        if (dll.Length > 0)
        {
            return dll[0];
        }

        return null;
    }

    private static bool HasFfmpegLibraries(string rootPath)
    {
        return FindLibraryPath(rootPath, "libavformat") != null;
    }

    private static string? FindOnPath(string executable)
    {
        var pathVar = Environment.GetEnvironmentVariable("PATH");
        if (string.IsNullOrWhiteSpace(pathVar))
            return null;

        var separator = RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? ';' : ':';
        foreach (var dir in pathVar.Split(separator))
        {
            var fullPath = Path.Combine(dir, executable);
            if (File.Exists(fullPath))
                return fullPath;
        }
        return null;
    }
}
