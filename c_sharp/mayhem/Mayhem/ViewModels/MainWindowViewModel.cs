using System;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Collections;
using Avalonia.Media;
using Effect = Mayhem.Models.Effect;
using Avalonia.Media.Imaging;
using Mayhem.Models;
using Mayhem.Services;

namespace Mayhem.ViewModels;

public sealed class MainWindowViewModel : ObservableObject
{
    private const double PixelsPerSecond = 100.0;
    private const double DefaultTimelineWidth = 3000.0;
    private const double WaveformHeight = 80.0;
    private const double ThumbnailHeight = 70.0;
    private const double TimelineTickHeight = 18.0;

    private Project _project = new(string.Empty);
    private double _zoom = 1.0;
    private double _timelineWidth = DefaultTimelineWidth;
    private string _mediaPath = string.Empty;
    private MediaKind _mediaKind = MediaKind.None;
    private long _mediaDurationMs;
    private List<double> _waveformSamples = new();
    private List<VideoFrameData> _videoFrames = new();
    private Bitmap? _currentVideoFrame;
    private long _currentTimeMs;
    private string _statusText = "Status: Ready";
    private int _waveformPointCount;
    private Geometry? _waveformGeometry;
    private readonly DebugLog _debugLog = new();
    private CueMarker? _selectedCue;
    private int _channelCount = 4;
    private double _channelLaneHeight = 40.0;
    private EffectInstanceViewModel? _selectedEffect;
    private int _editStartMs;
    private int _editDurationMs = 1000;
    private RgbColor _editStartRgb = RgbColor.Yellow;
    private RgbColor _editEndRgb = RgbColor.Yellow;
    private int _editOffset;
    private int _editWindow = 100;
    private Geometry? _colorCurveGeometry;
    private bool _suppressEffectApply;
    private bool _isColorEffectSelected;
    private bool _isScriptEffectSelected;
    private bool _isParamSetEffectSelected;
    private bool _isParamChangeEffectSelected;
    private string _selectedScriptLink = string.Empty;
    private string _editParamName = string.Empty;
    private decimal _editParamValue;
    private decimal _editParamStartValue;
    private decimal _editParamEndValue;
    private string? _effectClipboardJson;
    private ChannelEntry? _effectClipboardChannel;

    private readonly string _scriptsDirectory;

    public MainWindowViewModel()
    {
        _scriptsDirectory = System.IO.Path.Combine(System.IO.Directory.GetCurrentDirectory(), "Scripts");
        SetChannelCount(_channelCount);
        LoadScripts();
    }

    public Project Project
    {
        get => _project;
        set => SetField(ref _project, value);
    }

    public string MediaPath
    {
        get => _mediaPath;
        set => SetField(ref _mediaPath, value);
    }

    public MediaKind MediaKind
    {
        get => _mediaKind;
        set
        {
            if (SetField(ref _mediaKind, value))
            {
                OnPropertyChanged(nameof(IsAudio));
                OnPropertyChanged(nameof(IsVideo));
                OnPropertyChanged(nameof(IsNoMedia));
            }
        }
    }

    public bool IsAudio => MediaKind == MediaKind.Audio;
    public bool IsVideo => MediaKind == MediaKind.Video;
    public bool IsNoMedia => MediaKind == MediaKind.None;

    public double Zoom
    {
        get => _zoom;
        set
        {
            var clamped = Math.Clamp(value, 0.05, 4.0);
            if (SetField(ref _zoom, clamped))
            {
                UpdateTimelineWidth();
            }
        }
    }

    public double TimelineWidth
    {
        get => _timelineWidth;
        private set => SetField(ref _timelineWidth, value);
    }

    public long MediaDurationMs => _mediaDurationMs;

    public string StatusText
    {
        get => _statusText;
        set => SetField(ref _statusText, value);
    }

    public int WaveformPointCount
    {
        get => _waveformPointCount;
        private set => SetField(ref _waveformPointCount, value);
    }

    public Geometry? WaveformGeometry
    {
        get => _waveformGeometry;
        private set => SetField(ref _waveformGeometry, value);
    }

    public CueMarker? SelectedCue
    {
        get => _selectedCue;
        private set => SetField(ref _selectedCue, value);
    }

    public EffectInstanceViewModel? SelectedEffect
    {
        get => _selectedEffect;
        private set => SetField(ref _selectedEffect, value);
    }

    public bool HasSelectedEffect => SelectedEffect != null;

    public string SelectedEffectType => SelectedEffect?.Effect.Type.ToString() ?? string.Empty;
    public bool IsColorEffectSelected
    {
        get => _isColorEffectSelected;
        private set => SetField(ref _isColorEffectSelected, value);
    }

    public bool IsScriptEffectSelected
    {
        get => _isScriptEffectSelected;
        private set => SetField(ref _isScriptEffectSelected, value);
    }

    public bool IsParamSetEffectSelected
    {
        get => _isParamSetEffectSelected;
        private set => SetField(ref _isParamSetEffectSelected, value);
    }

    public bool IsParamChangeEffectSelected
    {
        get => _isParamChangeEffectSelected;
        private set => SetField(ref _isParamChangeEffectSelected, value);
    }

    public string SelectedScriptLink
    {
        get => _selectedScriptLink;
        private set => SetField(ref _selectedScriptLink, value);
    }

    public string EditParamName
    {
        get => _editParamName;
        set
        {
            var trimmed = value ?? string.Empty;
            if (trimmed.Length > 16)
            {
                trimmed = trimmed.Substring(0, 16);
            }
            if (SetField(ref _editParamName, trimmed))
            {
                ApplyEffectEdits();
            }
        }
    }

    public decimal EditParamValue
    {
        get => _editParamValue;
        set
        {
            if (SetField(ref _editParamValue, value))
            {
                ApplyEffectEdits();
            }
        }
    }

    public decimal EditParamStartValue
    {
        get => _editParamStartValue;
        set
        {
            if (SetField(ref _editParamStartValue, value))
            {
                ApplyEffectEdits();
            }
        }
    }

    public decimal EditParamEndValue
    {
        get => _editParamEndValue;
        set
        {
            if (SetField(ref _editParamEndValue, value))
            {
                ApplyEffectEdits();
            }
        }
    }

    public int EditStartMs
    {
        get => _editStartMs;
        set
        {
            var clamped = Math.Max(0, value);
            if (SetField(ref _editStartMs, clamped))
            {
                ApplyEffectEdits();
            }
        }
    }

    public int EditDurationMs
    {
        get => _editDurationMs;
        set
        {
            var clamped = Math.Max(1, value);
            if (SetField(ref _editDurationMs, clamped))
            {
                ApplyEffectEdits();
            }
        }
    }

    public RgbColor EditStartRgb
    {
        get => _editStartRgb;
        set
        {
            if (SetField(ref _editStartRgb, value))
            {
                _editStartBrushCache = null;
                OnPropertyChanged(nameof(EditStartBrush));
                RebuildColorCurveGeometry();
                ApplyEffectEdits();
            }
        }
    }

    public RgbColor EditEndRgb
    {
        get => _editEndRgb;
        set
        {
            if (SetField(ref _editEndRgb, value))
            {
                _editEndBrushCache = null;
                OnPropertyChanged(nameof(EditEndBrush));
                RebuildColorCurveGeometry();
                ApplyEffectEdits();
            }
        }
    }

    public int EditOffset
    {
        get => _editOffset;
        set
        {
            var clamped = Math.Clamp(value, 0, 100);
            if (SetField(ref _editOffset, clamped))
            {
                RebuildColorCurveGeometry();
                ApplyEffectEdits();
            }
        }
    }

    public int EditWindow
    {
        get => _editWindow;
        set
        {
            var clamped = Math.Clamp(value, 0, 100);
            if (SetField(ref _editWindow, clamped))
            {
                RebuildColorCurveGeometry();
                ApplyEffectEdits();
            }
        }
    }

    public Geometry? ColorCurveGeometry
    {
        get => _colorCurveGeometry;
        private set => SetField(ref _colorCurveGeometry, value);
    }

    private IBrush? _editStartBrushCache;
    private IBrush? _editEndBrushCache;

    public IBrush EditStartBrush
    {
        get
        {
            if (_editStartBrushCache != null) return _editStartBrushCache;
            _editStartBrushCache = new SolidColorBrush(Color.FromRgb(EditStartRgb.R, EditStartRgb.G, EditStartRgb.B));
            return _editStartBrushCache;
        }
    }

    public IBrush EditEndBrush
    {
        get
        {
            if (_editEndBrushCache != null) return _editEndBrushCache;
            _editEndBrushCache = new SolidColorBrush(Color.FromRgb(EditEndRgb.R, EditEndRgb.G, EditEndRgb.B));
            return _editEndBrushCache;
        }
    }

    public int ChannelCount
    {
        get => _channelCount;
        set
        {
            var clamped = Math.Clamp(value, 1, 255);
            if (SetField(ref _channelCount, clamped))
            {
                SetChannelCount(clamped);
            }
        }
    }

    public double ChannelLaneHeight
    {
        get => _channelLaneHeight;
        set => SetField(ref _channelLaneHeight, Math.Clamp(value, 20, 120));
    }

    public AvaloniaList<string> DebugLines => _debugLog.Lines;

    public AvaloniaList<Point> WaveformPoints { get; } = new();
    public AvaloniaList<VideoFrameThumbnail> VideoFrames { get; } = new();
    public AvaloniaList<TimelineTick> TimelineTicks { get; } = new();
    public AvaloniaList<CueMarker> CueMarkers { get; } = new();
    public AvaloniaList<ChannelEntry> Channels { get; } = new();
    public AvaloniaList<ScriptFileItem> ScriptFiles { get; } = new();

    public Bitmap? CurrentVideoFrame
    {
        get => _currentVideoFrame;
        set
        {
            var old = _currentVideoFrame;
            if (SetField(ref _currentVideoFrame, value))
            {
                old?.Dispose();
            }
        }
    }

    public long CurrentTimeMs
    {
        get => _currentTimeMs;
        set
        {
            if (SetField(ref _currentTimeMs, value))
            {
                UpdateCurrentVideoFrame();
                OnPropertyChanged(nameof(PlayheadX));
                OnPropertyChanged(nameof(Timecode));
            }
        }
    }

    public double PlayheadX => (_currentTimeMs / 1000.0) * PixelsPerSecond * Zoom;
    public string Timecode => TimeSpan.FromMilliseconds(_currentTimeMs).ToString(@"hh\:mm\:ss\:fff");

    public long TimeFromX(double x)
    {
        var seconds = x / Math.Max(1.0, PixelsPerSecond * Zoom);
        return (long)(seconds * 1000.0);
    }

    public void SetMedia(Project project, string mediaPath, MediaDecodeResult decode)
    {
        Project = project;
        MediaPath = mediaPath;
        MediaKind = decode.Kind;
        _mediaDurationMs = decode.DurationMs;
        StatusText = decode.Kind == MediaKind.None ? "Status: No media streams detected." : "Status: Media loaded.";

        _waveformSamples = decode.Audio?.WaveformSamples ?? new List<double>();
        _videoFrames = decode.Video?.Frames ?? new List<VideoFrameData>();

        ApplyWaveform();
        ApplyVideoFrames();
        RebuildCueMarkers();
        RebuildChannels();
        UpdateTimelineWidth();
        UpdateCurrentVideoFrame();
    }

    public void LoadProject(Project project)
    {
        Project = project;
        MediaPath = project.MediaLink ?? string.Empty;
        MediaKind = MediaKind.None;
        _mediaDurationMs = 0;
        _waveformSamples = new List<double>();
        _videoFrames = new List<VideoFrameData>();
        WaveformPoints.Clear();
        WaveformGeometry = null;
        DisposeVideoFrames();
        VideoFrames.Clear();
        CurrentVideoFrame = null;

        _channelCount = Math.Max(1, CountChannels(project));
        OnPropertyChanged(nameof(ChannelCount));

        RebuildCueMarkers();
        RebuildChannels();
        UpdateTimelineWidth();
        SetCurrentTime(0);
        SelectCue(null);
        SelectEffect(null);
        OnPropertyChanged(nameof(IsNoMedia));
    }

    private static int CountChannels(Project project)
    {
        var count = 0;
        var node = project.Channels.Root();
        while (node != null)
        {
            count++;
            node = node.Next;
        }

        return count;
    }

    public void SetCurrentTime(long timeMs)
    {
        CurrentTimeMs = Math.Clamp(timeMs, 0, _mediaDurationMs);
    }

    public void SetStatus(string text)
    {
        StatusText = text;
    }

    public void AddDebug(string message)
    {
        _debugLog.Add(message);
    }

    private void UpdateTimelineWidth()
    {
        if (_mediaDurationMs <= 0)
        {
            TimelineWidth = DefaultTimelineWidth;
        }
        else
        {
            var durationSeconds = Math.Max(0.1, _mediaDurationMs / 1000.0);
            var width = durationSeconds * PixelsPerSecond * Zoom;
            TimelineWidth = Math.Max(200.0, width);
        }
        ApplyWaveform();
        ApplyVideoFrames();
        UpdateCuePositions();
        UpdateEffectPositions();
        UpdateTimelineTicks();
        OnPropertyChanged(nameof(PlayheadX));
    }

    private void SetChannelCount(int count)
    {
        EnsureChannelList(count);
        RebuildChannels();
    }

    private void EnsureChannelList(int count)
    {
        var current = 0;
        var node = Project.Channels.Root();
        while (node != null)
        {
            current++;
            node = node.Next;
        }

        if (current < count)
        {
            for (var i = current; i < count; i++)
            {
                Project.Channels.Add(new Channel(i, $"Channel {i}"));
            }
        }
        else if (current > count)
        {
            for (var i = current - 1; i >= count; i--)
            {
                var last = Project.Channels.Root();
                while (last?.Next != null)
                {
                    last = last.Next;
                }

                if (last != null)
                {
                    Project.Channels.Delete(last);
                }
            }
        }
    }

    private void RebuildChannels()
    {
        Channels.Clear();
        var list = new List<ChannelEntry>();
        var node = Project.Channels.Root();
        while (node != null)
        {
            var entry = new ChannelEntry(node.Value);
            list.Add(entry);
            node = node.Next;
        }

        for (var i = 0; i < list.Count; i++)
        {
            Channels.Add(list[i]);
        }

        if (Channels.Count == 0)
        {
            Project.Channels.Add(new Channel(0, "Channel 0"));
            Channels.Add(new ChannelEntry(Project.Channels.Root()!.Value));
        }

        BuildEffectInstances();
    }

    private void BuildEffectInstances()
    {
        foreach (var entry in Channels)
        {
            entry.Effects.Clear();
            var node = entry.Channel.Effects.Root();
            while (node != null)
            {
                var vm = new EffectInstanceViewModel(node.Value, entry);
                vm.UpdateLayout(this);
                entry.AddEffect(vm);
                node = node.Next;
            }
        }
    }

    public EffectInstanceViewModel AddColorEffect(ChannelEntry channel, long startMs, int durationMs)
    {
        var effect = new ColorEffect((int)startMs, durationMs, new RgbColor(255, 255, 0), new RgbColor(255, 255, 0));
        return AddEffectInstance(channel, effect);
    }

    public EffectInstanceViewModel AddScriptEffect(ChannelEntry channel, long startMs, int durationMs, string scriptLink)
    {
        var effect = new ScriptEffect((int)startMs, durationMs, scriptLink);
        return AddEffectInstance(channel, effect);
    }

    public EffectInstanceViewModel AddParamSetEffect(ChannelEntry channel, long startMs, int durationMs)
    {
        var effect = new ParamSetEffect((int)startMs, durationMs, "param", 0f);
        return AddEffectInstance(channel, effect);
    }

    public EffectInstanceViewModel AddParamChangeEffect(ChannelEntry channel, long startMs, int durationMs)
    {
        var effect = new ParamChangeEffect((int)startMs, durationMs, "param", 0f, 1f);
        return AddEffectInstance(channel, effect);
    }

    public void MoveEffect(EffectInstanceViewModel effect, ChannelEntry newChannel, long startMs)
    {
        effect.Effect.StartMs = (int)startMs;
        if (!ReferenceEquals(effect.Channel, newChannel))
        {
            effect.Channel.RemoveEffect(effect);
            newChannel.AddEffect(effect);
            RemoveEffectFromList(effect.Channel.Channel, effect.Effect);
            newChannel.Channel.Effects.Add(effect.Effect);
            effect.MoveToChannel(newChannel);
        }
        effect.UpdateLayout(this);
    }

    public bool CopySelectedEffect()
    {
        if (SelectedEffect == null)
        {
            return false;
        }

        if (SelectedEffect.Effect is ColorEffect selectedColor)
        {
            SelectedEffect.Effect.SetColor(selectedColor.StartRgb);
            SelectedEffect.NotifyColorChanged();
        }

        _effectClipboardJson = SelectedEffect.Effect.ToJson();
        _effectClipboardChannel = SelectedEffect.Channel;
        return true;
    }

    public bool CutSelectedEffect()
    {
        if (SelectedEffect == null)
        {
            return false;
        }

        if (!CopySelectedEffect())
        {
            return false;
        }

        RemoveEffect(SelectedEffect);
        return true;
    }

    public EffectInstanceViewModel? PasteClipboardEffectAt(long startMs)
    {
        if (string.IsNullOrWhiteSpace(_effectClipboardJson))
        {
            return null;
        }

        if (_effectClipboardChannel == null || !Channels.Contains(_effectClipboardChannel))
        {
            return null;
        }

        Effect effect;
        try
        {
            effect = Effect.FromJson(_effectClipboardJson);
        }
        catch (Exception ex)
        {
            AddDebug($"Paste failed: {ex.Message}");
            return null;
        }

        if (effect is ColorEffect pastedColor)
        {
            effect.SetColor(pastedColor.StartRgb);
        }

        effect.StartMs = (int)Math.Max(0, startMs);
        var vm = AddEffectInstance(_effectClipboardChannel, effect);
        SelectEffect(vm);
        return vm;
    }

    public void RemoveEffect(EffectInstanceViewModel effect)
    {
        if (ReferenceEquals(SelectedEffect, effect))
        {
            SelectEffect(null);
        }

        effect.Channel.RemoveEffect(effect);
        RemoveEffectFromList(effect.Channel.Channel, effect.Effect);
    }

    public void SelectEffect(EffectInstanceViewModel? effect)
    {
        if (SelectedEffect != null)
        {
            SelectedEffect.IsSelected = false;
        }

        SelectedEffect = effect;
        if (SelectedEffect != null)
        {
            SelectedEffect.IsSelected = true;
        }

        LoadSelectedEffect();
        OnPropertyChanged(nameof(HasSelectedEffect));
        OnPropertyChanged(nameof(SelectedEffectType));
    }

    public void CommitSelectedEffectEdits()
    {
        ApplyEffectEdits(force: true);
    }

    private void LoadSelectedEffect()
    {
        if (SelectedEffect == null)
        {
            IsColorEffectSelected = false;
            IsScriptEffectSelected = false;
            IsParamSetEffectSelected = false;
            IsParamChangeEffectSelected = false;
            SelectedScriptLink = string.Empty;
            EditParamName = string.Empty;
            EditParamValue = 0;
            EditParamStartValue = 0;
            EditParamEndValue = 0;
            ColorCurveGeometry = null;
            return;
        }

        _suppressEffectApply = true;
        try
        {
            EditStartMs = SelectedEffect.Effect.StartMs;
            EditDurationMs = SelectedEffect.Effect.DurationMs;
            if (SelectedEffect.Effect is ColorEffect color)
            {
                SelectedEffect.Effect.SetColor(color.StartRgb);
                SelectedEffect.NotifyColorChanged();
                EditStartRgb = color.StartRgb;
                EditEndRgb = color.EndRgb;
                EditOffset = color.Offset;
                EditWindow = color.Window;
            }
            else if (SelectedEffect.Effect is ParamSetEffect paramSet)
            {
                EditStartRgb = RgbColor.Yellow;
                EditEndRgb = RgbColor.Yellow;
                EditOffset = 0;
                EditWindow = 100;
                EditParamName = paramSet.ParamName;
                EditParamValue = (decimal)paramSet.Value;
                EditParamStartValue = 0;
                EditParamEndValue = 0;
            }
            else if (SelectedEffect.Effect is ParamChangeEffect paramChange)
            {
                EditStartRgb = RgbColor.Yellow;
                EditEndRgb = RgbColor.Yellow;
                EditOffset = paramChange.Offset;
                EditWindow = paramChange.Window;
                EditParamName = paramChange.ParamName;
                EditParamValue = 0;
                EditParamStartValue = (decimal)paramChange.StartValue;
                EditParamEndValue = (decimal)paramChange.EndValue;
            }
            else
            {
                EditStartRgb = RgbColor.Yellow;
                EditEndRgb = RgbColor.Yellow;
                EditOffset = 0;
                EditWindow = 100;
                EditParamName = string.Empty;
                EditParamValue = 0;
                EditParamStartValue = 0;
                EditParamEndValue = 0;
            }
            IsColorEffectSelected = SelectedEffect.Effect is ColorEffect;
            IsScriptEffectSelected = SelectedEffect.Effect is ScriptEffect;
            IsParamSetEffectSelected = SelectedEffect.Effect is ParamSetEffect;
            IsParamChangeEffectSelected = SelectedEffect.Effect is ParamChangeEffect;
            if (SelectedEffect.Effect is ScriptEffect script)
            {
                SelectedScriptLink = script.ScriptLink;
            }
            else
            {
                SelectedScriptLink = string.Empty;
            }
        }
        finally
        {
            _suppressEffectApply = false;
        }
        RebuildColorCurveGeometry();
    }

    public void LoadScripts()
    {
        ScriptFiles.Clear();
        if (!System.IO.Directory.Exists(_scriptsDirectory))
        {
            System.IO.Directory.CreateDirectory(_scriptsDirectory);
        }

        foreach (var file in System.IO.Directory.GetFiles(_scriptsDirectory))
        {
            var name = System.IO.Path.GetFileNameWithoutExtension(file);
            var relative = System.IO.Path.GetRelativePath(System.IO.Directory.GetCurrentDirectory(), file);
            ScriptFiles.Add(new ScriptFileItem(name, relative, file));
        }
    }

    private void ApplyEffectEdits(bool force = false)
    {
        if (_suppressEffectApply && !force)
        {
            return;
        }

        if (SelectedEffect == null)
        {
            return;
        }

        SelectedEffect.Effect.StartMs = EditStartMs;
        SelectedEffect.Effect.DurationMs = Math.Max(1, EditDurationMs);
        if (SelectedEffect.Effect is ColorEffect color)
        {
            color.StartRgb = EditStartRgb;
            color.EndRgb = EditEndRgb;
            color.Offset = EditOffset;
            color.Window = EditWindow;
            SelectedEffect.Effect.SetColor(EditStartRgb);
            SelectedEffect.NotifyColorChanged();
            SelectedEffect.NotifyLabelChanged();
        }
        else if (SelectedEffect.Effect is ParamSetEffect paramSet)
        {
            paramSet.ParamName = LimitParamName(EditParamName);
            paramSet.Value = (float)EditParamValue;
            SelectedEffect.NotifyLabelChanged();
        }
        else if (SelectedEffect.Effect is ParamChangeEffect paramChange)
        {
            paramChange.ParamName = LimitParamName(EditParamName);
            paramChange.StartValue = (float)EditParamStartValue;
            paramChange.EndValue = (float)EditParamEndValue;
            paramChange.Offset = EditOffset;
            paramChange.Window = EditWindow;
            SelectedEffect.NotifyLabelChanged();
        }

        SelectedEffect.UpdateLayout(this);
        OnPropertyChanged(nameof(PlayheadX));
    }

    private void RebuildColorCurveGeometry()
    {
        if (!IsColorEffectSelected && !IsParamChangeEffectSelected)
        {
            ColorCurveGeometry = null;
            return;
        }

        const int width = 220;
        const int height = 90;
        const int min = 0;
        const int max = 255;
        const int stride = 20;
        const int xMin = 0;
        const int xMax = 1000;

        var geometry = new StreamGeometry();
        using var ctx = geometry.Open();
        var first = true;
        for (var x = 0; x <= width; x++)
        {
            var t = x / (double)width;
            var sampleX = (int)Math.Round(xMin + ((xMax - xMin) * t));
            var value = ParamMath.Larp(sampleX, xMin, xMax, min, max, EditOffset, EditWindow, stride);
            var y = height - ((value / 255.0) * height);
            var point = new Point(x, y);
            if (first)
            {
                ctx.BeginFigure(point, false);
                first = false;
            }
            else
            {
                ctx.LineTo(point);
            }
        }

        ColorCurveGeometry = geometry;
    }

    private static string LimitParamName(string value)
    {
        var normalized = value ?? string.Empty;
        if (normalized.Length > 16)
        {
            normalized = normalized.Substring(0, 16);
        }
        return normalized;
    }

    public void UpdateEffectDuration(EffectInstanceViewModel effect, int durationMs)
    {
        if (durationMs < 100)
        {
            durationMs = 100;
        }

        effect.Effect.DurationMs = durationMs;
        effect.UpdateLayout(this);
    }

    private void UpdateEffectPositions()
    {
        foreach (var entry in Channels)
        {
            foreach (var effect in entry.Effects)
            {
                effect.UpdateLayout(this);
            }
        }
    }

    private EffectInstanceViewModel AddEffectInstance(ChannelEntry channel, Effect effect)
    {
        channel.Channel.Effects.Add(effect);
        var vm = new EffectInstanceViewModel(effect, channel);
        vm.UpdateLayout(this);
        channel.AddEffect(vm);
        return vm;
    }

    private void RemoveEffectFromList(Channel channel, Effect effect)
    {
        var node = channel.Effects.Root();
        while (node != null)
        {
            if (ReferenceEquals(node.Value, effect))
            {
                channel.Effects.Delete(node);
                return;
            }
            node = node.Next;
        }
    }

    private void ApplyWaveform()
    {
        WaveformPoints.Clear();
        if (_waveformSamples.Count == 0)
        {
            WaveformPointCount = 0;
            WaveformGeometry = null;
            return;
        }

        var points = NormalizeWaveform(_waveformSamples, TimelineWidth);
        foreach (var point in points)
        {
            WaveformPoints.Add(point);
        }
        WaveformPointCount = points.Count;
        WaveformGeometry = BuildWaveformGeometry(points);
    }

    private static Geometry BuildWaveformGeometry(IReadOnlyList<Point> points)
    {
        var geometry = new StreamGeometry();
        if (points.Count == 0)
        {
            return geometry;
        }

        using var ctx = geometry.Open();
        ctx.BeginFigure(points[0], false);
        for (var i = 1; i < points.Count; i++)
        {
            ctx.LineTo(points[i]);
        }

        return geometry;
    }

    public void DisposeAllMedia()
    {
        DisposeVideoFrames();
        VideoFrames.Clear();
        CurrentVideoFrame = null;
    }

    private void DisposeVideoFrames()
    {
        foreach (var thumbnail in VideoFrames)
        {
            thumbnail.Dispose();
        }
    }

    private void ApplyVideoFrames()
    {
        DisposeVideoFrames();
        VideoFrames.Clear();
        if (_videoFrames.Count == 0)
        {
            return;
        }

        foreach (var frame in _videoFrames)
        {
            var bitmap = ToBitmap(frame, ThumbnailHeight);
            if (bitmap == null)
            {
                continue;
            }

            var x = (frame.TimestampMs / 1000.0) * PixelsPerSecond * Zoom;
            VideoFrames.Add(new VideoFrameThumbnail(frame.TimestampMs, bitmap, x));
        }
    }

    private void UpdateTimelineTicks()
    {
        TimelineTicks.Clear();
        if (_mediaDurationMs <= 0)
        {
            return;
        }

        var totalSeconds = (int)Math.Ceiling(_mediaDurationMs / 1000.0);
        for (var i = 0; i <= totalSeconds; i++)
        {
            var x = i * PixelsPerSecond * Zoom;
            TimelineTicks.Add(new TimelineTick(x, i.ToString()));
        }
    }

    public CueMarker AddCueAt(long timeMs)
    {
        var cue = Cue.CreateUserCue((int)timeMs);
        Project.Cues.Add(cue);
        var marker = new CueMarker(cue, TimeToX(timeMs));
        CueMarkers.Add(marker);
        SelectCue(marker);
        return marker;
    }

    public void RemoveSelectedCue()
    {
        if (SelectedCue == null)
        {
            return;
        }

        var toRemove = SelectedCue;
        SelectedCue = null;
        toRemove.IsSelected = false;
        CueMarkers.Remove(toRemove);
        RemoveCueFromList(toRemove.Cue);
    }

    public void SelectCue(CueMarker? marker)
    {
        if (SelectedCue != null)
        {
            SelectedCue.IsSelected = false;
        }

        SelectedCue = marker;
        if (SelectedCue != null)
        {
            SelectedCue.IsSelected = true;
        }
    }

    public void MoveCue(CueMarker marker, long timeMs)
    {
        marker.Cue.TimeMs = (int)timeMs;
        marker.X = TimeToX(timeMs);
    }

    public double TimeToX(long timeMs)
    {
        return (timeMs / 1000.0) * PixelsPerSecond * Zoom;
    }

    private void RebuildCueMarkers()
    {
        CueMarkers.Clear();
        var node = Project.Cues.Root();
        while (node != null)
        {
            var marker = new CueMarker(node.Value, TimeToX(node.Value.TimeMs));
            CueMarkers.Add(marker);
            node = node.Next;
        }
    }

    private void UpdateCuePositions()
    {
        foreach (var marker in CueMarkers)
        {
            marker.X = TimeToX(marker.Cue.TimeMs);
        }
    }

    private void RemoveCueFromList(Cue cue)
    {
        var node = Project.Cues.Root();
        while (node != null)
        {
            if (ReferenceEquals(node.Value, cue))
            {
                Project.Cues.Delete(node);
                return;
            }
            node = node.Next;
        }
    }

    private void UpdateCurrentVideoFrame()
    {
        if (_videoFrames.Count == 0)
        {
            CurrentVideoFrame = null;
            return;
        }

        VideoFrameData? selected = null;
        for (var i = _videoFrames.Count - 1; i >= 0; i--)
        {
            if (_videoFrames[i].TimestampMs <= _currentTimeMs)
            {
                selected = _videoFrames[i];
                break;
            }
        }

        if (selected == null)
        {
            selected = _videoFrames[0];
        }

        CurrentVideoFrame = ToBitmap(selected, 360);
    }

    private static Bitmap? ToBitmap(VideoFrameData frame, double targetHeight)
    {
        if (frame.Width <= 0 || frame.Height <= 0)
        {
            return null;
        }

        var scaledWidth = (int)(frame.Width * (targetHeight / frame.Height));
        var scaledHeight = (int)targetHeight;
        if (scaledWidth <= 0 || scaledHeight <= 0)
        {
            return null;
        }

        return CreateScaledBitmap(frame, scaledWidth, scaledHeight);
    }

    private static Bitmap? CreateScaledBitmap(VideoFrameData frame, int scaledWidth, int scaledHeight)
    {
        if (frame.Width <= 0 || frame.Height <= 0)
        {
            return null;
        }

        var source = new WriteableBitmap(new PixelSize(frame.Width, frame.Height),
            new Vector(96, 96), Avalonia.Platform.PixelFormat.Bgra8888, Avalonia.Platform.AlphaFormat.Unpremul);

        var target = new WriteableBitmap(new PixelSize(scaledWidth, scaledHeight),
            new Vector(96, 96), Avalonia.Platform.PixelFormat.Bgra8888, Avalonia.Platform.AlphaFormat.Unpremul);

        try
        {
            using (var locked = source.Lock())
            {
                unsafe
                {
                    fixed (byte* src = frame.Bgra)
                    {
                        Buffer.MemoryCopy(src, (void*)locked.Address, locked.RowBytes * frame.Height, frame.Bgra.Length);
                    }
                }
            }

            using (var srcLock = source.Lock())
            using (var dstLock = target.Lock())
            {
                unsafe
                {
                    var srcPtr = (byte*)srcLock.Address;
                    var dstPtr = (byte*)dstLock.Address;
                    for (var y = 0; y < scaledHeight; y++)
                    {
                        var srcY = y * frame.Height / scaledHeight;
                        var srcRow = srcPtr + srcY * srcLock.RowBytes;
                        var dstRow = dstPtr + y * dstLock.RowBytes;
                        for (var x = 0; x < scaledWidth; x++)
                        {
                            var srcX = x * frame.Width / scaledWidth;
                            var srcPixel = srcRow + srcX * 4;
                            var dstPixel = dstRow + x * 4;
                            dstPixel[0] = srcPixel[0];
                            dstPixel[1] = srcPixel[1];
                            dstPixel[2] = srcPixel[2];
                            dstPixel[3] = srcPixel[3];
                        }
                    }
                }
            }

            return target;
        }
        catch
        {
            target.Dispose();
            return null;
        }
        finally
        {
            source.Dispose();
        }
    }

    public static List<Point> NormalizeWaveform(IReadOnlyList<double> samples, double width)
    {
        if (samples.Count == 0)
        {
            return new List<Point>();
        }

        var points = new List<Point>(samples.Count);
        var halfHeight = WaveformHeight / 2.0;
        var count = samples.Count;
        for (var i = 0; i < count; i++)
        {
            var normalized = ApplyLogScale(samples[i]);
            var x = width * i / Math.Max(1, count - 1);
            var y = halfHeight - (normalized * halfHeight);
            points.Add(new Point(x, y));
        }
        return points;
    }

    private static double ApplyLogScale(double value)
    {
        var clamped = Math.Clamp(value, 0, 1);
        const double k = 9.0;
        return Math.Log10(1 + k * clamped) / Math.Log10(1 + k);
    }
}
