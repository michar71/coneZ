using System;
using System.Diagnostics;
using System.Threading;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Threading;
using Avalonia;
using Avalonia.Media;
using Avalonia.VisualTree;
using Avalonia.Interactivity;
using Avalonia.Layout;
using System.Collections.ObjectModel;
using Avalonia.Controls.Presenters;
using Mayhem.Services;
using Mayhem.ViewModels;
using Mayhem.Models;

namespace Mayhem;

public partial class MainWindow : Window
{
    private readonly MainWindowViewModel _viewModel;
    private readonly MediaImportService _mediaImportService = new();
    private readonly MediaDecodeService _mediaDecodeService = new();
    private readonly AudioPlaybackService _audioPlayback = new();
    private readonly ProjectFileService _projectFileService = new();
    private readonly DispatcherTimer _playbackTimer;
    private readonly Stopwatch _fallbackClock = new();
    private long _fallbackOffsetMs;

    private DecodedAudio? _decodedAudio;
    private bool _hasVideo;
    private bool _isScrubbing;
    private ScrollViewer? _timelineScroll;
    private ScrollViewer? _channelLabelsScroll;
    private ScrollViewer? _channelLanesScroll;
    private Control? _propertiesPanel;
    private Grid? _rootGrid;
    private Grid? _mainContentGrid;
    private Grid? _rightPanelGrid;
    private Border? _leftPanel;
    private Border? _debugPanel;
    private string? _currentProjectPath;
    private double _pendingSeekSeconds;
    private long _playbackBaseMs;
    private readonly DispatcherTimer _edgeScrollTimer;
    private double _edgeScrollVelocity;
    private CueMarker? _draggingCue;
    private EffectInstanceViewModel? _draggingEffect;
    private double _draggingEffectOffsetX;
    private bool _isResizingEffect;
    private double _resizeStartX;
    private double _resizeStartWidth;
    private CancellationTokenSource? _decodeCts;

    public MainWindow()
    {
        InitializeComponent();
        _viewModel = new MainWindowViewModel();
        DataContext = _viewModel;

        _timelineScroll = this.FindControl<ScrollViewer>("TimelineScroll");
        _channelLabelsScroll = this.FindControl<ScrollViewer>("ChannelLabelsScroll");
        _channelLanesScroll = this.FindControl<ScrollViewer>("ChannelLanesScroll");
        _propertiesPanel = this.FindControl<Control>("PropertiesPanel");
        _rootGrid = this.FindControl<Grid>("RootGrid");
        _mainContentGrid = this.FindControl<Grid>("MainContentGrid");
        _rightPanelGrid = this.FindControl<Grid>("RightPanelGrid");
        _leftPanel = this.FindControl<Border>("LeftPanel");
        _debugPanel = this.FindControl<Border>("DebugPanel");
        _playbackTimer = new DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(33)
        };
        _playbackTimer.Tick += PlaybackTimerOnTick;

        _edgeScrollTimer = new DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(16)
        };
        _edgeScrollTimer.Tick += EdgeScrollTimerOnTick;
    }

    private async void ImportMedia_OnClick(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        var path = await _mediaImportService.PickMediaAsync(this);
        if (string.IsNullOrWhiteSpace(path))
        {
            _viewModel.AddDebug("Import canceled.");
            return;
        }

        _viewModel.AddDebug($"Importing media: {path}");
        _decodeCts?.Cancel();
        _decodeCts?.Dispose();
        _decodeCts = new CancellationTokenSource();
        var token = _decodeCts.Token;
        void ThreadSafeLog(string msg) => Dispatcher.UIThread.Post(() => _viewModel.AddDebug(msg));
        MediaDecodeResult decode;
        try
        {
            decode = await _mediaDecodeService.DecodeAsync(path, token, ThreadSafeLog);
        }
        catch (OperationCanceledException)
        {
            _viewModel.AddDebug("Media import canceled.");
            return;
        }
        catch (Exception ex)
        {
            _viewModel.SetStatus($"Status: FFmpeg error: {ex.Message}");
            _viewModel.AddDebug($"FFmpeg error: {ex}");
            return;
        }
        if (token.IsCancellationRequested) return;
        var project = _viewModel.Project;
        project.MediaLink = path;
        _viewModel.SetMedia(project, path, decode);
        _viewModel.AddDebug($"Decoded: kind={decode.Kind}, durationMs={decode.DurationMs}, audio={(decode.Audio != null ? "yes" : "no")}, video={(decode.Video != null ? "yes" : "no")}");

        _decodedAudio = decode.Audio;
        _hasVideo = decode.Video != null && decode.Video.Frames.Count > 0;
        if (_decodedAudio != null && _decodedAudio.Samples.Length > 0)
        {
            _viewModel.AddDebug($"Audio loaded: samples={_decodedAudio.Samples.Length}, rate={_decodedAudio.SampleRate}, channels={_decodedAudio.Channels}");
            _audioPlayback.LoadPcm(_decodedAudio.Samples, _decodedAudio.SampleRate, _decodedAudio.Channels);
            _pendingSeekSeconds = 0;
            _playbackBaseMs = 0;
            _viewModel.SetCurrentTime(0);
        }
        else
        {
            _viewModel.AddDebug("No audio samples available.");
            _fallbackClock.Reset();
            _fallbackOffsetMs = 0;
            _pendingSeekSeconds = 0;
            _playbackBaseMs = 0;
        }
    }

    private void ResetPlayback()
    {
        _playbackTimer.Stop();
        if (_audioPlayback.IsPlaying)
        {
            _audioPlayback.Stop();
        }

        _decodedAudio = null;
        _hasVideo = false;
        _pendingSeekSeconds = 0;
        _playbackBaseMs = 0;
        _fallbackClock.Reset();
        _fallbackOffsetMs = 0;
        _viewModel.SetCurrentTime(0);
    }

    private void NewProject_OnClick(object? sender, RoutedEventArgs e)
    {
        _currentProjectPath = null;
        ResetPlayback();
        var project = new Project(string.Empty);
        _viewModel.LoadProject(project);
        _viewModel.AddDebug("New project created.");
    }

    private async void OpenProject_OnClick(object? sender, RoutedEventArgs e)
    {
        var path = await _projectFileService.PickOpenAsync(this);
        if (string.IsNullOrWhiteSpace(path))
        {
            return;
        }

        ProjectFile file;
        try
        {
            file = ProjectFile.Load(path);
        }
        catch (Exception ex)
        {
            _viewModel.AddDebug($"Failed to open project: {ex.Message}");
            return;
        }

        _currentProjectPath = path;
        ApplyLayout(file.Layout);
        var project = file.ToProject();
        ResetPlayback();

        var mediaPath = project.MediaLink;
        if (!string.IsNullOrWhiteSpace(mediaPath) && !System.IO.Path.IsPathRooted(mediaPath))
        {
            var baseDir = System.IO.Path.GetDirectoryName(path) ?? System.IO.Directory.GetCurrentDirectory();
            mediaPath = System.IO.Path.Combine(baseDir, mediaPath);
        }

        if (!string.IsNullOrWhiteSpace(mediaPath) && System.IO.File.Exists(mediaPath))
        {
            _decodeCts?.Cancel();
            _decodeCts?.Dispose();
            _decodeCts = new CancellationTokenSource();
            var token = _decodeCts.Token;
            void ThreadSafeLog(string msg) => Dispatcher.UIThread.Post(() => _viewModel.AddDebug(msg));
            try
            {
                var decode = await _mediaDecodeService.DecodeAsync(mediaPath, token, ThreadSafeLog);
                if (token.IsCancellationRequested) return;
                project.MediaLink = mediaPath;
                _viewModel.SetMedia(project, mediaPath, decode);
                _decodedAudio = decode.Audio;
                _hasVideo = decode.Video != null && decode.Video.Frames.Count > 0;
                if (_decodedAudio != null && _decodedAudio.Samples.Length > 0)
                {
                    _audioPlayback.LoadPcm(_decodedAudio.Samples, _decodedAudio.SampleRate, _decodedAudio.Channels);
                }
            }
            catch (OperationCanceledException)
            {
                _viewModel.AddDebug("Media decode canceled.");
            }
            catch (Exception ex)
            {
                _viewModel.AddDebug($"FFmpeg error: {ex.Message}");
                _viewModel.LoadProject(project);
            }
        }
        else
        {
            _viewModel.LoadProject(project);
        }
    }

    private async void SaveProject_OnClick(object? sender, RoutedEventArgs e)
    {
        if (string.IsNullOrWhiteSpace(_currentProjectPath))
        {
            await SaveProjectAsAsync();
            return;
        }

        SaveProjectToPath(_currentProjectPath);
    }

    private async void SaveProjectAs_OnClick(object? sender, RoutedEventArgs e)
    {
        await SaveProjectAsAsync();
    }

    private async System.Threading.Tasks.Task SaveProjectAsAsync()
    {
        var path = await _projectFileService.PickSaveAsync(this, "project.clf");
        if (string.IsNullOrWhiteSpace(path))
        {
            return;
        }

        _currentProjectPath = path;
        SaveProjectToPath(path);
    }

    private void SaveProjectToPath(string path)
    {
        var layout = CaptureLayout();
        ProjectFile.Save(path, _viewModel.Project, layout);
        _viewModel.AddDebug($"Project saved: {path}");
    }

    private LayoutSettings CaptureLayout()
    {
        static double Sanitize(double value, double fallback)
        {
            if (double.IsNaN(value) || double.IsInfinity(value) || value <= 0)
            {
                return fallback;
            }

            return value;
        }

        var layout = new LayoutSettings
        {
            WindowWidth = Sanitize(Bounds.Width, 1200),
            WindowHeight = Sanitize(Bounds.Height, 800),
            LeftPanelWidth = Sanitize(_leftPanel?.Bounds.Width ?? 260, 260),
            RightPanelWidth = Sanitize(_propertiesPanel?.Bounds.Width ?? 260, 260),
            DebugPanelHeight = Sanitize(_debugPanel?.Bounds.Height ?? 200, 200),
            VideoPanelHeight = Sanitize(_rightPanelGrid?.RowDefinitions[2].ActualHeight ?? 240, 240),
            ChannelLaneHeight = Sanitize(_viewModel.ChannelLaneHeight, 40),
            Zoom = Sanitize(_viewModel.Zoom, 1.0)
        };

        return layout;
    }

    private void ApplyLayout(LayoutSettings? layout)
    {
        if (layout == null)
        {
            return;
        }

        if (layout.WindowWidth > 0)
        {
            Width = layout.WindowWidth;
        }
        if (layout.WindowHeight > 0)
        {
            Height = layout.WindowHeight;
        }

        if (_mainContentGrid != null)
        {
            if (layout.LeftPanelWidth > 0)
            {
                _mainContentGrid.ColumnDefinitions[0].Width = new GridLength(layout.LeftPanelWidth);
            }

            if (layout.RightPanelWidth > 0)
            {
                _mainContentGrid.ColumnDefinitions[4].Width = new GridLength(layout.RightPanelWidth);
            }
        }

        if (_rootGrid != null && layout.DebugPanelHeight > 0)
        {
            _rootGrid.RowDefinitions[2].Height = new GridLength(layout.DebugPanelHeight);
        }

        if (_rightPanelGrid != null && layout.VideoPanelHeight > 0)
        {
            _rightPanelGrid.RowDefinitions[2].Height = new GridLength(layout.VideoPanelHeight);
        }

        if (layout.ChannelLaneHeight > 0)
        {
            _viewModel.ChannelLaneHeight = layout.ChannelLaneHeight;
        }

        if (layout.Zoom > 0)
        {
            _viewModel.Zoom = layout.Zoom;
        }
    }

    private void PlayPause_OnClick(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (_decodedAudio == null && !_hasVideo)
        {
            _viewModel.AddDebug("Play/Pause ignored: no media loaded.");
            return;
        }

        Focus();
        if (_decodedAudio != null)
        {
            if (_audioPlayback.IsPlaying)
            {
                var seconds = _audioPlayback.GetPositionSeconds();
                var absoluteMs = _playbackBaseMs + (long)(seconds * 1000.0);
                _pendingSeekSeconds = absoluteMs / 1000.0;
                _viewModel.SetCurrentTime(absoluteMs);
                _audioPlayback.Pause();
                _playbackTimer.Stop();
                _viewModel.AddDebug("Audio paused.");
                _playbackBaseMs = absoluteMs;
            }
            else
            {
                _audioPlayback.Stop();
                _audioPlayback.SeekSeconds(_pendingSeekSeconds);
                _playbackBaseMs = (long)(_pendingSeekSeconds * 1000.0);
                _viewModel.SetCurrentTime(_playbackBaseMs);
                _audioPlayback.Play();
                _playbackTimer.Start();
                _viewModel.AddDebug("Audio playing.");
            }
        }
        else
        {
            if (_fallbackClock.IsRunning)
            {
                _fallbackOffsetMs += _fallbackClock.ElapsedMilliseconds;
                _fallbackClock.Reset();
                _playbackTimer.Stop();
                _viewModel.AddDebug("Video clock paused.");
            }
            else
            {
                _fallbackClock.Start();
                _playbackTimer.Start();
                _viewModel.AddDebug("Video clock running.");
            }
        }

        AutoScrollTimelineIfNeeded();
    }

    private void PlaybackTimerOnTick(object? sender, EventArgs e)
    {
        if (_decodedAudio == null && !_hasVideo)
        {
            return;
        }

        if (_isScrubbing)
        {
            return;
        }

        if (_decodedAudio != null)
        {
            if (_audioPlayback.IsPlaying)
            {
                var seconds = _audioPlayback.GetPositionSeconds();
                var timeMs = _playbackBaseMs + (long)(seconds * 1000.0);
                _viewModel.SetCurrentTime(timeMs);
                _pendingSeekSeconds = timeMs / 1000.0;
                AutoScrollTimelineIfNeeded();
            }
        }
        else if (_fallbackClock.IsRunning)
        {
            var timeMs = _fallbackOffsetMs + _fallbackClock.ElapsedMilliseconds;
            _viewModel.SetCurrentTime(timeMs);
            AutoScrollTimelineIfNeeded();
        }
    }

    private void Window_OnKeyDown(object? sender, KeyEventArgs e)
    {
        var isCommandShortcut = e.KeyModifiers.HasFlag(KeyModifiers.Meta) || e.KeyModifiers.HasFlag(KeyModifiers.Control);
        if (isCommandShortcut && e.Key == Key.C)
        {
            if (_viewModel.CopySelectedEffect())
            {
                _viewModel.AddDebug("Effect copied.");
                e.Handled = true;
            }
            return;
        }

        if (isCommandShortcut && e.Key == Key.X)
        {
            if (_viewModel.CutSelectedEffect())
            {
                _viewModel.AddDebug("Effect cut.");
                e.Handled = true;
            }
            return;
        }

        if (isCommandShortcut && e.Key == Key.V)
        {
            var pasted = _viewModel.PasteClipboardEffectAt(_viewModel.CurrentTimeMs);
            if (pasted != null)
            {
                var pasteEndMs = pasted.Effect.StartMs + pasted.Effect.DurationMs;
                _viewModel.SetCurrentTime(pasteEndMs);
                AutoScrollTimelineIfNeeded();
                _viewModel.AddDebug($"Effect pasted at {pasted.Effect.StartMs} ms on {pasted.Channel.Name}.");
                e.Handled = true;
            }
            return;
        }

        if (e.Key == Key.Space)
        {
            PlayPause_OnClick(sender, e);
            e.Handled = true;
            return;
        }

        if (e.Key == Key.C)
        {
            AddCueAtCurrentTime();
            e.Handled = true;
            return;
        }

        if (e.Key == Key.Delete || e.Key == Key.Back)
        {
            if (_viewModel.SelectedEffect != null)
            {
                _viewModel.RemoveEffect(_viewModel.SelectedEffect);
                _viewModel.AddDebug("Effect deleted.");
            }
            else
            {
                _viewModel.RemoveSelectedCue();
            }
            e.Handled = true;
        }
    }

    private void AddCue_OnClick(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        AddCueAtCurrentTime();
    }

    private void ChannelLabel_OnPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (sender is not Control control)
        {
            return;
        }

        if (control.DataContext is not ChannelEntry entry)
        {
            return;
        }

        if (e.ClickCount == 2)
        {
            entry.IsEditing = true;
        }
    }

    private void ChannelName_OnLostFocus(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (sender is not Control control)
        {
            return;
        }

        if (control.DataContext is ChannelEntry entry)
        {
            entry.IsEditing = false;
        }
    }

    private void ChannelName_OnKeyDown(object? sender, KeyEventArgs e)
    {
        if (e.Key != Key.Enter && e.Key != Key.Escape)
        {
            return;
        }

        if (sender is not Control control)
        {
            return;
        }

        if (control.DataContext is ChannelEntry entry)
        {
            entry.IsEditing = false;
            e.Handled = true;
        }
    }

    private void ChannelScroll_OnScrollChanged(object? sender, ScrollChangedEventArgs e)
    {
        if (_channelLabelsScroll == null || _channelLanesScroll == null)
        {
            return;
        }

        if (ReferenceEquals(sender, _channelLabelsScroll))
        {
            _channelLanesScroll.Offset = new Avalonia.Vector(_channelLanesScroll.Offset.X, _channelLabelsScroll.Offset.Y);
        }
        else if (ReferenceEquals(sender, _channelLanesScroll))
        {
            _channelLabelsScroll.Offset = new Avalonia.Vector(_channelLabelsScroll.Offset.X, _channelLanesScroll.Offset.Y);
        }
    }

    private async void EffectTreeItem_OnPointerPressed(object? sender, PointerPressedEventArgs e)
    {
#pragma warning disable CS0618
        var data = new DataObject();
        data.Set(DataFormats.Text, "effect:color");
        _viewModel.AddDebug("Effect drag started.");
        await DragDrop.DoDragDrop(e, data, DragDropEffects.Copy);
#pragma warning restore CS0618
    }

    private void ChannelLanes_OnDragOver(object? sender, DragEventArgs e)
    {
#pragma warning disable CS0618
        if (e.Data.Contains(DataFormats.Text))
        {
            e.DragEffects = DragDropEffects.Copy;
        }
        else
        {
            e.DragEffects = DragDropEffects.None;
        }
#pragma warning restore CS0618
        e.Handled = true;
    }

    private void ChannelLanes_OnDrop(object? sender, DragEventArgs e)
    {
        _viewModel.AddDebug("Drop received.");
        if (_channelLanesScroll == null || _timelineScroll == null)
        {
            return;
        }

#pragma warning disable CS0618
        if (!e.Data.Contains(DataFormats.Text))
        {
            _viewModel.AddDebug("Drop ignored: no text.");
            return;
        }

        var text = e.Data.GetText();
#pragma warning restore CS0618
        if (string.IsNullOrWhiteSpace(text))
        {
            _viewModel.AddDebug("Drop ignored: empty payload.");
            return;
        }

        var position = e.GetPosition(_channelLanesScroll);
        var channel = ChannelFromPosition(position.Y + _channelLanesScroll.Offset.Y);
        if (channel == null)
        {
            _viewModel.AddDebug("Drop ignored: no channel resolved.");
            return;
        }

        var x = GetTimelineContentX(e);
        var timeMs = _viewModel.TimeFromX(Math.Max(0, x));
        timeMs = SnapToCue(timeMs);
        if (text.StartsWith("effect:", StringComparison.OrdinalIgnoreCase))
        {
            var effect = _viewModel.AddColorEffect(channel, timeMs, 1000);
            _viewModel.AddDebug($"Effect added at {timeMs} ms on {channel.Name}");
            _viewModel.AddDebug($"Effect VM: x={effect.X}, width={effect.Width}");
        }
        else if (text.StartsWith("script:", StringComparison.OrdinalIgnoreCase))
        {
            var scriptLink = text.Substring("script:".Length);
            var effect = _viewModel.AddScriptEffect(channel, timeMs, 1000, scriptLink);
            _viewModel.AddDebug($"Script effect added at {timeMs} ms on {channel.Name}");
            _viewModel.AddDebug($"Script link: {scriptLink}");
            _viewModel.AddDebug($"Effect VM: x={effect.X}, width={effect.Width}");
        }
        else
        {
            _viewModel.AddDebug("Drop ignored: unrecognized payload.");
            return;
        }
        e.Handled = true;
    }

    private void Effect_OnPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (sender is not Control control)
        {
            return;
        }

        if (control.DataContext is not EffectInstanceViewModel effect)
        {
            return;
        }

        var wasSelected = effect.IsSelected;
        _viewModel.SelectEffect(effect);
        if (!wasSelected)
        {
            e.Handled = true;
            return;
        }

        _draggingEffect = effect;
        var position = e.GetPosition(control);
        _draggingEffectOffsetX = position.X;

        if (effect.Width - position.X <= 8)
        {
            _isResizingEffect = true;
            _resizeStartX = effect.X;
            _resizeStartWidth = effect.Width;
        }
        else
        {
            _isResizingEffect = false;
        }

        e.Pointer.Capture(control);
        e.Handled = true;
    }

    private async void ScriptFile_OnPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (sender is not Control control)
        {
            return;
        }

        if (control.DataContext is not ScriptFileItem script)
        {
            return;
        }

#pragma warning disable CS0618
        var data = new DataObject();
        data.Set(DataFormats.Text, $"script:{script.RelativePath}");
        await DragDrop.DoDragDrop(e, data, DragDropEffects.Copy);
#pragma warning restore CS0618
    }

    private void NewScript_OnClick(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        _ = CreateAndEditScriptAsync();
    }

    private async System.Threading.Tasks.Task CreateAndEditScriptAsync()
    {
        var name = await ShowNewScriptDialogAsync();
        if (string.IsNullOrWhiteSpace(name))
        {
            return;
        }

        if (!name.EndsWith(".bas", StringComparison.OrdinalIgnoreCase))
        {
            name += ".bas";
        }

        var scriptsDir = System.IO.Path.Combine(System.IO.Directory.GetCurrentDirectory(), "Scripts");
        if (!System.IO.Directory.Exists(scriptsDir))
        {
            System.IO.Directory.CreateDirectory(scriptsDir);
        }

        var fullPath = System.IO.Path.Combine(scriptsDir, name);
        if (System.IO.File.Exists(fullPath))
        {
            _viewModel.AddDebug($"Script already exists: {name}");
        }
        else
        {
            System.IO.File.WriteAllText(fullPath, "' New script");
        }

        _viewModel.LoadScripts();
        _viewModel.AddDebug($"Created script: {System.IO.Path.GetRelativePath(System.IO.Directory.GetCurrentDirectory(), fullPath)}");
        await ShowScriptEditorAsync(fullPath);
    }

    private async System.Threading.Tasks.Task<string?> ShowNewScriptDialogAsync()
    {
        var input = new TextBox
        {
            Width = 240
        };

        var okButton = new Button
        {
            Content = "OK",
            Width = 80,
            HorizontalAlignment = HorizontalAlignment.Right
        };

        var cancelButton = new Button
        {
            Content = "Cancel",
            Width = 80,
            HorizontalAlignment = HorizontalAlignment.Right
        };

        var buttonPanel = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            Spacing = 8,
            HorizontalAlignment = HorizontalAlignment.Right
        };
        buttonPanel.Children.Add(okButton);
        buttonPanel.Children.Add(cancelButton);

        var stack = new StackPanel
        {
            Margin = new Thickness(12),
            Spacing = 8
        };
        stack.Children.Add(new TextBlock { Text = "Script name" });
        stack.Children.Add(input);
        stack.Children.Add(buttonPanel);

        var dialog = new Window
        {
            Width = 320,
            Height = 160,
            Title = "New Script",
            Content = stack,
            WindowStartupLocation = WindowStartupLocation.CenterOwner
        };

        var tcs = new System.Threading.Tasks.TaskCompletionSource<string?>();
        okButton.Click += (_, _) =>
        {
            tcs.TrySetResult(input.Text);
            dialog.Close();
        };
        cancelButton.Click += (_, _) =>
        {
            tcs.TrySetResult(null);
            dialog.Close();
        };
        dialog.Closed += (_, _) => tcs.TrySetResult(null);

        await dialog.ShowDialog(this);
        return await tcs.Task;
    }

    private async void ScriptFile_OnDoubleTapped(object? sender, RoutedEventArgs e)
    {
        if (sender is not Control control)
        {
            return;
        }

        if (control.DataContext is not ScriptFileItem script)
        {
            return;
        }

        await ShowScriptEditorAsync(script.FullPath);
    }

    private async void Effect_OnDoubleTapped(object? sender, RoutedEventArgs e)
    {
        if (sender is not Control control)
        {
            return;
        }

        if (control.DataContext is not EffectInstanceViewModel effect)
        {
            return;
        }

        if (effect.Effect is not Mayhem.Models.ScriptEffect script)
        {
            return;
        }

        var fullPath = System.IO.Path.Combine(System.IO.Directory.GetCurrentDirectory(), script.ScriptLink);
        await ShowScriptEditorAsync(fullPath);
    }

    private async System.Threading.Tasks.Task ShowScriptEditorAsync(string fullPath)
    {
        if (!System.IO.File.Exists(fullPath))
        {
            _viewModel.SetStatus($"Status: Script not found: {System.IO.Path.GetFileName(fullPath)}");
            _viewModel.AddDebug($"Script not found: {fullPath}");
            return;
        }

        var text = await System.IO.File.ReadAllTextAsync(fullPath);

        var lineNumbers = new ObservableCollection<string>();
        void UpdateLineNumbers(string content)
        {
            var count = Math.Max(1, content.Split('\n').Length);
            lineNumbers.Clear();
            for (var i = 1; i <= count; i++)
            {
                lineNumbers.Add(i.ToString());
            }
        }

        const double lineHeight = 16;

        var lineItems = new StackPanel
        {
            Width = 40
        };

        void RebuildLineItems()
        {
            lineItems.Children.Clear();
            foreach (var value in lineNumbers)
            {
                lineItems.Children.Add(new TextBlock
                {
                    Text = value,
                    Height = lineHeight,
                    LineHeight = lineHeight,
                    FontFamily = new FontFamily("Cascadia Mono, JetBrains Mono, Consolas, Menlo, monospace"),
                    FontSize = 12,
                    VerticalAlignment = VerticalAlignment.Center
                });
            }
        }

        UpdateLineNumbers(text);
        RebuildLineItems();

        var lineScroll = new ScrollViewer
        {
            Content = new Border
            {
                Background = new SolidColorBrush(Color.Parse("#1A1A1A")),
                Child = lineItems
            }
        };

        var editor = new TextBox
        {
            Text = text,
            AcceptsReturn = true,
            TextWrapping = TextWrapping.NoWrap,
            LineHeight = lineHeight,
            FontFamily = new FontFamily("Cascadia Mono, JetBrains Mono, Consolas, Menlo, monospace"),
            FontSize = 12
        };

        EventHandler<EventArgs>? textChangedHandler = null;
        textChangedHandler = (_, _) =>
        {
            UpdateLineNumbers(editor.Text ?? string.Empty);
            RebuildLineItems();
        };
        editor.TextChanged += textChangedHandler;

        ScrollViewer? editorScroll = null;
        EventHandler<ScrollChangedEventArgs>? scrollHandler = null;
        editor.AttachedToVisualTree += (_, _) =>
        {
            if (editorScroll != null) return;
            editorScroll = editor.FindDescendantOfType<ScrollViewer>();
            if (editorScroll != null)
            {
                scrollHandler = (_, _) =>
                {
                    lineScroll.Offset = new Vector(0, editorScroll.Offset.Y);
                };
                editorScroll.ScrollChanged += scrollHandler;
            }
        };

        var grid = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions("Auto,*"),
            RowDefinitions = new RowDefinitions("*,Auto"),
            Margin = new Thickness(12)
        };

        grid.Children.Add(lineScroll);
        Grid.SetRow(lineScroll, 0);
        Grid.SetColumn(lineScroll, 0);

        grid.Children.Add(editor);
        Grid.SetRow(editor, 0);
        Grid.SetColumn(editor, 1);

        var saveButton = new Button
        {
            Content = "Save",
            Width = 80,
            HorizontalAlignment = HorizontalAlignment.Right
        };

        var closeButton = new Button
        {
            Content = "Close",
            Width = 80,
            HorizontalAlignment = HorizontalAlignment.Right
        };

        var buttonPanel = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            Spacing = 8,
            HorizontalAlignment = HorizontalAlignment.Right
        };
        buttonPanel.Children.Add(saveButton);
        buttonPanel.Children.Add(closeButton);

        grid.Children.Add(buttonPanel);
        Grid.SetRow(buttonPanel, 1);
        Grid.SetColumn(buttonPanel, 1);

        var dialog = new Window
        {
            Width = 720,
            Height = 520,
            Title = System.IO.Path.GetFileName(fullPath),
            Content = grid,
            WindowStartupLocation = WindowStartupLocation.CenterOwner
        };

        saveButton.Click += async (_, _) =>
        {
            await System.IO.File.WriteAllTextAsync(fullPath, editor.Text ?? string.Empty);
            _viewModel.AddDebug($"Saved script: {fullPath}");
        };
        closeButton.Click += (_, _) => dialog.Close();
        dialog.Closed += (_, _) =>
        {
            editor.TextChanged -= textChangedHandler;
            if (editorScroll != null && scrollHandler != null)
            {
                editorScroll.ScrollChanged -= scrollHandler;
            }
        };

        await dialog.ShowDialog(this);
    }

    private void Effect_OnPointerMoved(object? sender, PointerEventArgs e)
    {
        if (_draggingEffect == null || _channelLanesScroll == null || _timelineScroll == null)
        {
            return;
        }

        if (sender is not Control control)
        {
            return;
        }

        var point = e.GetCurrentPoint(control);
        if (!point.Properties.IsLeftButtonPressed)
        {
            _draggingEffect = null;
            _isResizingEffect = false;
            return;
        }

        if (!_draggingEffect.IsSelected)
        {
            return;
        }

        var pos = e.GetPosition(_channelLanesScroll);
        var x = GetTimelineContentX(e);

        if (_isResizingEffect)
        {
            var newWidth = Math.Max(20, x - _resizeStartX);
            var durationMs = _viewModel.TimeFromX(_resizeStartX + newWidth) - _viewModel.TimeFromX(_resizeStartX);
            if (durationMs < 100)
            {
                durationMs = 100;
            }
            _viewModel.UpdateEffectDuration(_draggingEffect, (int)durationMs);
            return;
        }

        var startX = x - _draggingEffectOffsetX;
        var timeMs = _viewModel.TimeFromX(Math.Max(0, startX));
        timeMs = SnapToCue(timeMs);

        var channel = ChannelFromPosition(pos.Y + _channelLanesScroll.Offset.Y);
        if (channel == null)
        {
            return;
        }

        _viewModel.MoveEffect(_draggingEffect, channel, timeMs);
    }

    private double GetTimelineContentX(PointerEventArgs e)
    {
        if (_timelineScroll == null)
        {
            return 0;
        }

        var pos = e.GetPosition(_timelineScroll);
        return pos.X + _timelineScroll.Offset.X;
    }

    private double GetTimelineContentX(DragEventArgs e)
    {
        if (_timelineScroll == null)
        {
            return 0;
        }

        var pos = e.GetPosition(_timelineScroll);
        return pos.X + _timelineScroll.Offset.X;
    }

    private void Effect_OnPointerReleased(object? sender, PointerReleasedEventArgs e)
    {
        if (_draggingEffect == null)
        {
            return;
        }

        if (sender is Control control)
        {
            e.Pointer.Capture(null);
        }

        _draggingEffect = null;
        _isResizingEffect = false;
    }

    private void Effect_OnPointerCaptureLost(object? sender, PointerCaptureLostEventArgs e)
    {
        _draggingEffect = null;
        _isResizingEffect = false;
    }

    private ChannelEntry? ChannelFromPosition(double y)
    {
        if (_viewModel.Channels.Count == 0)
        {
            return null;
        }

        var index = (int)(y / _viewModel.ChannelLaneHeight);
        if (index < 0)
        {
            index = 0;
        }
        if (index >= _viewModel.Channels.Count)
        {
            index = _viewModel.Channels.Count - 1;
        }

        return _viewModel.Channels[index];
    }

    private long SnapToCue(long timeMs)
    {
        const double thresholdPx = 6.0;
        var targetX = _viewModel.TimeToX(timeMs);
        CueMarker? nearest = null;
        var min = double.MaxValue;

        foreach (var cue in _viewModel.CueMarkers)
        {
            var dx = Math.Abs(cue.X - targetX);
            if (dx < thresholdPx && dx < min)
            {
                min = dx;
                nearest = cue;
            }
        }

        return nearest != null ? nearest.Cue.TimeMs : timeMs;
    }

    private void Exit_OnClick(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        Close();
    }

    private void Root_OnPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (_propertiesPanel == null)
        {
            return;
        }

        if (e.Source is Control source)
        {
            var current = source;
            while (current != null)
            {
                if (ReferenceEquals(current, _propertiesPanel))
                {
                    return;
                }
                current = current.Parent as Control;
            }
        }

        _viewModel.CommitSelectedEffectEdits();
    }


    private async void CopyDebug_OnClick(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        var lines = _viewModel.DebugLines;
        if (lines.Count == 0)
        {
            return;
        }

        var text = string.Join(Environment.NewLine, lines);
        var clipboard = TopLevel.GetTopLevel(this)?.Clipboard;
        if (clipboard != null)
        {
            await clipboard.SetTextAsync(text);
            _viewModel.AddDebug("Debug log copied to clipboard.");
        }
    }

    private void Timeline_OnPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (sender is not Control control)
        {
            return;
        }
        _isScrubbing = true;
        var position = e.GetPosition(control);
        _draggingCue = FindCueNearX(position.X);
        if (_draggingCue != null)
        {
            _viewModel.SelectCue(_draggingCue);
            MoveCueFromPosition(position.X);
        }
        else
        {
            _viewModel.SelectCue(null);
            SeekFromPointer(control, e);
        }
        e.Pointer.Capture((IInputElement?)sender);
        UpdateEdgeScroll(control, e);
    }

    private void Timeline_OnPointerMoved(object? sender, PointerEventArgs e)
    {
        if (!_isScrubbing)
        {
            return;
        }

        if (sender is not Control control)
        {
            return;
        }
        var position = e.GetPosition(control);
        if (_draggingCue != null)
        {
            MoveCueFromPosition(position.X);
        }
        else
        {
            SeekFromPointer(control, e);
        }
        UpdateEdgeScroll(control, e);
    }

    private void Timeline_OnPointerReleased(object? sender, PointerReleasedEventArgs e)
    {
        if (!_isScrubbing)
        {
            return;
        }

        _isScrubbing = false;
        _draggingCue = null;
        e.Pointer.Capture(null);
        StopEdgeScroll();
    }

    private void SeekFromPointer(Control control, PointerEventArgs e)
    {
        if (_decodedAudio == null && !_hasVideo)
        {
            return;
        }

        var position = e.GetPosition(control);
        var timeMs = _viewModel.TimeFromX(position.X);
        _viewModel.SetCurrentTime(timeMs);
        _pendingSeekSeconds = timeMs / 1000.0;
        _playbackBaseMs = timeMs;

        if (_decodedAudio != null)
        {
            _audioPlayback.SeekSeconds(timeMs / 1000.0);
        }
        else
        {
            var wasRunning = _fallbackClock.IsRunning;
            _fallbackOffsetMs = timeMs;
            _fallbackClock.Reset();
            if (wasRunning)
            {
                _fallbackClock.Start();
            }
        }

        AutoScrollTimelineIfNeeded();
    }

    private void AddCueAtCurrentTime()
    {
        var marker = _viewModel.AddCueAt(_viewModel.CurrentTimeMs);
        _viewModel.AddDebug($"Cue added at {marker.Cue.TimeMs} ms.");
    }

    private CueMarker? FindCueNearX(double x)
    {
        const double threshold = 6.0;
        CueMarker? closest = null;
        var min = double.MaxValue;

        foreach (var cue in _viewModel.CueMarkers)
        {
            var distance = Math.Abs(cue.X - x);
            if (distance < threshold && distance < min)
            {
                min = distance;
                closest = cue;
            }
        }

        return closest;
    }

    private void MoveCueFromPosition(double x)
    {
        if (_draggingCue == null)
        {
            return;
        }

        var timeMs = _viewModel.TimeFromX(Math.Max(0, x));
        _viewModel.MoveCue(_draggingCue, timeMs);
    }

    private void AutoScrollTimelineIfNeeded()
    {
        if (_timelineScroll == null)
        {
            return;
        }

        var viewport = _timelineScroll.Viewport.Width;
        if (viewport <= 0)
        {
            return;
        }

        var currentOffset = _timelineScroll.Offset.X;
        var rightEdge = currentOffset + viewport;
        var playheadX = _viewModel.PlayheadX;

        if (playheadX > rightEdge)
        {
            var targetOffset = Math.Max(0, playheadX - viewport + 20);
            var newOffset = currentOffset + (targetOffset - currentOffset) * 0.25;
            _timelineScroll.Offset = new Avalonia.Vector(newOffset, _timelineScroll.Offset.Y);
        }
        else if (playheadX < currentOffset)
        {
            var targetOffset = Math.Max(0, playheadX - 20);
            var newOffset = currentOffset + (targetOffset - currentOffset) * 0.25;
            _timelineScroll.Offset = new Avalonia.Vector(newOffset, _timelineScroll.Offset.Y);
        }
    }

    private void UpdateEdgeScroll(Control control, PointerEventArgs e)
    {
        if (_timelineScroll == null)
        {
            return;
        }

        var viewport = _timelineScroll.Viewport.Width;
        if (viewport <= 0)
        {
            return;
        }

        var position = _timelineScroll != null ? e.GetPosition(_timelineScroll) : e.GetPosition(control);
        const double margin = 48.0;
        double velocity = 0;

        if (position.X < margin)
        {
            velocity = -1 * (margin - position.X) / margin;
        }
        else if (position.X > viewport - margin)
        {
            velocity = (position.X - (viewport - margin)) / margin;
        }

        if (Math.Abs(velocity) < 0.001)
        {
            StopEdgeScroll();
            return;
        }

        _edgeScrollVelocity = velocity;
        if (!_edgeScrollTimer.IsEnabled)
        {
            _edgeScrollTimer.Start();
        }
    }

    private void EdgeScrollTimerOnTick(object? sender, EventArgs e)
    {
        if (_timelineScroll == null)
        {
            return;
        }

        var viewport = _timelineScroll.Viewport.Width;
        if (viewport <= 0)
        {
            return;
        }

        const double speed = 2.5;
        var delta = Math.Sign(_edgeScrollVelocity) * speed * Math.Pow(Math.Abs(_edgeScrollVelocity), 1.2);
        if (Math.Abs(delta) < 0.01)
        {
            return;
        }

        var maxOffset = Math.Max(0, _viewModel.TimelineWidth - viewport);
        var limitedDelta = Math.Clamp(delta, -3.0, 3.0);
        var newOffset = Math.Clamp(_timelineScroll.Offset.X + limitedDelta, 0, maxOffset);
        _timelineScroll.Offset = new Avalonia.Vector(newOffset, _timelineScroll.Offset.Y);
    }

    private void StopEdgeScroll()
    {
        _edgeScrollVelocity = 0;
        if (_edgeScrollTimer.IsEnabled)
        {
            _edgeScrollTimer.Stop();
        }
    }

    private void MenuCut_OnClick(object? sender, RoutedEventArgs e)
    {
        if (_viewModel.CutSelectedEffect())
        {
            _viewModel.AddDebug("Effect cut.");
        }
    }

    private void MenuCopy_OnClick(object? sender, RoutedEventArgs e)
    {
        if (_viewModel.CopySelectedEffect())
        {
            _viewModel.AddDebug("Effect copied.");
        }
    }

    private void MenuPaste_OnClick(object? sender, RoutedEventArgs e)
    {
        var pasted = _viewModel.PasteClipboardEffectAt(_viewModel.CurrentTimeMs);
        if (pasted != null)
        {
            var pasteEndMs = pasted.Effect.StartMs + pasted.Effect.DurationMs;
            _viewModel.SetCurrentTime(pasteEndMs);
            AutoScrollTimelineIfNeeded();
            _viewModel.AddDebug($"Effect pasted at {pasted.Effect.StartMs} ms on {pasted.Channel.Name}.");
        }
    }

    private void MenuDelete_OnClick(object? sender, RoutedEventArgs e)
    {
        if (_viewModel.SelectedEffect != null)
        {
            _viewModel.RemoveEffect(_viewModel.SelectedEffect);
            _viewModel.AddDebug("Effect deleted.");
        }
        else
        {
            _viewModel.RemoveSelectedCue();
        }
    }

    private void MenuZoomIn_OnClick(object? sender, RoutedEventArgs e)
    {
        _viewModel.Zoom = Math.Min(4.0, _viewModel.Zoom * 1.25);
    }

    private void MenuZoomOut_OnClick(object? sender, RoutedEventArgs e)
    {
        _viewModel.Zoom = Math.Max(0.05, _viewModel.Zoom / 1.25);
    }

    private void MenuZoomReset_OnClick(object? sender, RoutedEventArgs e)
    {
        _viewModel.Zoom = 1.0;
    }

    protected override void OnOpened(EventArgs e)
    {
        base.OnOpened(e);
        Focus();
    }

    protected override void OnClosed(EventArgs e)
    {
        base.OnClosed(e);
        _playbackTimer.Stop();
        _edgeScrollTimer.Stop();
        _decodeCts?.Cancel();
        _decodeCts?.Dispose();
        _audioPlayback.Dispose();
        _viewModel.DisposeAllMedia();
    }
}
