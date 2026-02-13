# Mayhem Bug Fixes

## Cue deserialization failure
- **File:** `Models/Cue.cs`
- **Severity:** Critical
- **Problem:** `System.Text.Json` could not deserialize `Cue` objects because it didn't know to use the 3-parameter constructor. Round-tripping a cue through JSON would throw.
- **Fix:** Added `[JsonConstructor]` attribute to the constructor.

## ColorEffect round-trip corruption
- **File:** `Models/Effect.cs`
- **Severity:** Critical
- **Problem:** `FromDto()` set `effect.Color` correctly from `dto.Color`, then immediately overwrote it with `colorEffect.StartRgb`. Any `Color` value that differed from `StartRgb` was silently lost on save/load.
- **Fix:** Removed the `if (effect is ColorEffect)` block that overwrote `Color`.

## Linked list double-delete count corruption
- **Files:** `Models/ChannelList.cs`, `CueList.cs`, `EffectList.cs`, `ParamList.cs`
- **Severity:** High
- **Problem:** Calling `Delete()` on an already-deleted or foreign node would blindly decrement `_count` and corrupt head/tail pointers, since a detached node has `Next == null && Previous == null`.
- **Fix:** Added early return when both `Next` and `Previous` are null and the node is not `_head`.

## BGRA field named Rgba
- **Files:** `Services/MediaDecodeResult.cs`, `ViewModels/MainWindowViewModel.cs`
- **Severity:** Medium
- **Problem:** FFmpeg's `sws_scale` outputs `AV_PIX_FMT_BGRA` but the field was named `Rgba`, making the byte order misleading. Avalonia's `Bgra8888` pixel format was correctly used for rendering, so no visual bug — just a naming lie that would cause bugs if anyone trusted the name.
- **Fix:** Renamed property and constructor parameter from `Rgba` to `Bgra`.

## Missing decoder flush
- **File:** `Services/MediaDecodeService.cs`
- **Severity:** High
- **Problem:** After `av_read_frame` returned EOF, neither `DecodeAudio` nor `DecodeVideo` sent a null packet to flush the codec. Codecs that buffer frames internally (B-frames, AAC priming) would silently drop the last few frames/samples.
- **Fix:** After the read loop, send a null packet via `avcodec_send_packet` and drain remaining frames. Extracted shared logic into `DrainAudioFrames()` and `DrainVideoFrames()` helpers.

## FfmpegLoader thread safety
- **File:** `Services/FfmpegLoader.cs`
- **Severity:** High
- **Problem:** `Initialize()` checked `_initialized` without synchronization. Two threads calling it concurrently could both enter the init path, causing double library loads or races on `ffmpeg.RootPath`.
- **Fix:** Added `lock (_lock)` with double-checked locking around the entire init body.

## FFmpeg null pointer crashes
- **File:** `Services/MediaDecodeService.cs`
- **Severity:** High
- **Problem:** Return values from `sws_getContext`, `av_malloc`, `av_packet_alloc`, and `av_frame_alloc` were used without null checks. Any allocation failure (OOM, unsupported pixel format) would crash with a null dereference.
- **Fix:** Added null checks after each allocation; on failure, clean up already-allocated resources and return an empty result.

## libdl resolver crashes on Linux
- **File:** `Services/FfmpegLoader.cs`
- **Severity:** High
- **Problem:** The DLL import resolver unconditionally tried to load `/usr/lib/libSystem.B.dylib` for any `libdl` request. This is a macOS-only path and fails on Linux.
- **Fix:** Guarded the `libSystem.B.dylib` load with `RuntimeInformation.IsOSPlatform(OSPlatform.OSX)`. On other platforms, the resolver falls through to the generic path.

## Bitmap memory leaks
- **Files:** `ViewModels/VideoFrameThumbnail.cs`, `ViewModels/MainWindowViewModel.cs`
- **Severity:** High
- **Problem:** Video frame thumbnails created Avalonia `Bitmap` objects that were never disposed. `ApplyVideoFrames()` and `LoadProject()` cleared the list without disposing, and `CurrentVideoFrame` was replaced without disposing the old bitmap. Over a session with multiple media loads, this leaked significant native memory.
- **Fix:** `VideoFrameThumbnail` now implements `IDisposable`. Added `DisposeVideoFrames()` helper called before clearing the list. `CurrentVideoFrame` setter disposes the old bitmap on change.

## CreateScaledBitmap double-dispose
- **File:** `ViewModels/MainWindowViewModel.cs`
- **Severity:** Low
- **Problem:** In the error path, `catch` disposed `source`, then `finally` disposed it again. Harmless (Dispose is idempotent) but sloppy — and if a future bitmap type isn't idempotent, it would crash.
- **Fix:** Removed `source.Dispose()` from the `catch` block since `finally` always handles it. `catch` now only disposes `target`.

## Video-only scrub unpauses playback
- **File:** `MainWindow.axaml.cs`
- **Severity:** Medium
- **Problem:** `SeekFromPointer` unconditionally called `_fallbackClock.Start()` when seeking in video-only mode. Scrubbing while paused would resume playback, since the clock restart acted as an unpause.
- **Fix:** Capture `_fallbackClock.IsRunning` before reset, only restart the clock if it was already running.

## Effect block color never updates visually
- **Files:** `ViewModels/EffectInstanceViewModel.cs`, `ViewModels/MainWindowViewModel.cs`
- **Severity:** Medium
- **Problem:** `EffectInstanceViewModel.Fill` is a computed property that reads `Effect.Color`, but nothing called `OnPropertyChanged(nameof(Fill))` after color edits. The effect block in the timeline stayed its original color regardless of user changes.
- **Fix:** Added `NotifyColorChanged()` method on `EffectInstanceViewModel`. Called from `ApplyEffectEdits()` after `SetColor()`.

## Video frame bitmaps not disposed on window close
- **Files:** `MainWindow.axaml.cs`, `ViewModels/MainWindowViewModel.cs`
- **Severity:** Medium
- **Problem:** `OnClosed` disposed the audio playback service but never disposed video frame thumbnail bitmaps or the current video frame bitmap, leaking native memory on exit.
- **Fix:** Added `DisposeAllMedia()` on the view model (disposes thumbnails, clears list, nulls current frame). Called from `OnClosed`.

## DebugLog unbounded growth
- **File:** `ViewModels/DebugLog.cs`
- **Severity:** Low
- **Problem:** Debug log lines accumulated indefinitely with no cap. Over a long editing session, the list would grow without bound, consuming memory and slowing UI rendering.
- **Fix:** Added `MaxLines = 500` cap. Oldest entries are removed when the limit is exceeded.

## ColorEffect constructor hardcodes Yellow base Color
- **File:** `Models/ColorEffect.cs`
- **Severity:** Low
- **Problem:** Constructor passed `RgbColor.Yellow` to the base `Effect` class regardless of the `startRgb` parameter. The color was corrected later by `ApplyEffectEdits` and `FromDto`, but the initial value was misleading and could cause the effect block to flash Yellow briefly before the first edit.
- **Fix:** Pass `startRgb` to the base constructor instead of `RgbColor.Yellow`.

## Concurrent decode operations on re-import
- **File:** `MainWindow.axaml.cs`
- **Severity:** Medium
- **Problem:** `DecodeAsync` was called with `default` CancellationToken. Importing a new file or opening a project while a previous decode was still in-flight ran both concurrently, wasting CPU and risking a stale result overwriting a newer one.
- **Fix:** Added `CancellationTokenSource` field. Previous decode is cancelled before starting a new one. Post-await cancellation check prevents applying stale results. CTS disposed on window close.

## QueueBuffer copies entire audio tail on every seek
- **File:** `Services/AudioPlaybackService.cs`
- **Severity:** Low
- **Problem:** `QueueBuffer` allocated a new `byte[]` and `BlockCopy`'d all remaining samples on every seek. For a 3-minute stereo 44.1kHz track, that's ~31MB per seek call — significant GC pressure during scrubbing.
- **Fix:** Pin the original `short[]` array and pass an offset pointer directly to `AL.BufferData`, eliminating the copy and allocation entirely.

## Playback timer runs continuously with no media
- **File:** `MainWindow.axaml.cs`
- **Severity:** Low
- **Problem:** `_playbackTimer` was started unconditionally in the constructor and ticked every 33ms for the lifetime of the window, even with no media loaded. Each tick performed checks and returned early — trivial overhead but unnecessary.
- **Fix:** Timer is no longer started in the constructor. Started when playback begins (play click), stopped when playback pauses/stops or media is reset/unloaded.

## AddDebug called from background thread
- **File:** `MainWindow.axaml.cs`
- **Severity:** High
- **Problem:** `_viewModel.AddDebug` is passed as the `log` callback to `MediaDecodeService.DecodeAsync()`, which runs on `Task.Run()` (ThreadPool). `DebugLog.Add()` mutates an `AvaloniaList<string>` — a UI-bound collection that must only be modified on the UI thread. On some platforms this silently corrupts state; on others it throws.
- **Fix:** Wrap the callback passed to `DecodeAsync` so it marshals to the UI thread via `Dispatcher.UIThread.Post()`.

## OpenAL context/buffer creation not validated
- **File:** `Services/AudioPlaybackService.cs`
- **Severity:** High
- **Problem:** `ALC.CreateContext()` return value was never null-checked before `ALC.MakeContextCurrent()`. `AL.GenBuffer()` and `AL.GenSource()` results were used without verifying valid handles. If no audio device is available or context creation fails, the application crashes with an opaque error.
- **Fix:** Add null/validity checks after `CreateContext`, `GenBuffer`, and `GenSource`. On failure, stay in `!_initialized` state and log the error rather than crashing.

## Script editor event handler leaks
- **File:** `MainWindow.axaml.cs`
- **Severity:** High
- **Problem:** Each call to `ShowScriptEditorAsync` subscribes `TextChanged` and `AttachedToVisualTree` (which subscribes `ScrollChanged`) handlers on fresh controls. These are never unsubscribed. Over many script open/close cycles, handlers accumulate, holding references to closed dialog controls.
- **Fix:** Unsubscribe `TextChanged` handler when the dialog closes. Use a flag to only subscribe `ScrollChanged` once in `AttachedToVisualTree`.

## Unimplemented menu items
- **File:** `MainWindow.axaml`
- **Severity:** Low
- **Problem:** Edit menu (Undo, Redo, Cut, Copy, Paste, Delete), View menu (Zoom In, Zoom Out, Reset Zoom), and Help > About all render as menu items but have no click handlers — clicking them does nothing.
- **Fix:** Wire Edit menu items to existing keyboard shortcut logic (Cut/Copy/Paste/Delete). Wire View zoom items to Zoom property changes. Undo/Redo kept but disabled until undo system is implemented. Removed empty Help menu.

## ChannelCount bound to TextBox without validation
- **File:** `MainWindow.axaml`
- **Severity:** Medium
- **Problem:** `ChannelCount` (an int) is bound two-way to a plain `TextBox`. Typing non-numeric text causes a silent binding failure. All other numeric properties use `NumericUpDown`.
- **Fix:** Replace the `TextBox` with `NumericUpDown` with `Minimum="1"` and `Maximum="255"`.

## Edge scroll timer not stopped on window close
- **File:** `MainWindow.axaml.cs`
- **Severity:** Low
- **Problem:** `OnClosed` stops `_playbackTimer` but not `_edgeScrollTimer`. If the user closes the window while scrubbing, the edge scroll timer keeps ticking until GC.
- **Fix:** Stop `_edgeScrollTimer` in `OnClosed` alongside the other cleanup.

## ParamMath integer precision loss
- **File:** `Models/ParamMath.cs`
- **Severity:** Medium
- **Problem:** `(range / 2) * offset / 100` truncated odd ranges before multiplying by offset. For `range = 999, offset = 50`, result was `499 * 50 / 100 = 249` instead of the correct `999 * 50 / 200 = 249`. The two happen to agree here, but for `range = 1, offset = 99` the old code gave `0` while the correct answer is `0` (still rounds down). More importantly, the division ordering was mathematically wrong for asymmetric cases.
- **Fix:** Changed to `range * offset / 200` to defer truncation.

## Dead CreateNewScript method
- **File:** `ViewModels/MainWindowViewModel.cs`
- **Severity:** Low
- **Problem:** `CreateNewScript()` was a public method that performed synchronous file I/O, but no code called it — the code-behind in `MainWindow.axaml.cs` handles script creation directly with its own logic.
- **Fix:** Removed the dead method.

## Silent deserialization fallbacks for Script/Media effects
- **File:** `Models/Effect.cs`
- **Severity:** Medium
- **Problem:** `FromDto()` defaulted `ScriptLink ?? string.Empty` and `MediaLink ?? string.Empty` when deserializing. A corrupt or hand-edited project file with a missing link would silently produce a broken effect with an empty path instead of reporting an error.
- **Fix:** Changed to `?? throw new JsonException(...)` so missing required fields fail loudly.

## Script-not-found gives no user-visible feedback
- **File:** `MainWindow.axaml.cs`
- **Severity:** Low
- **Problem:** When double-clicking a script effect whose file was deleted, `ShowScriptEditorAsync` only logged to the debug panel. Users had to open the debug panel to discover why the editor didn't appear.
- **Fix:** Also set the status bar text so the error is visible without opening debug.

## VideoFrameThumbnail not safe against double dispose
- **File:** `ViewModels/VideoFrameThumbnail.cs`
- **Severity:** Low
- **Problem:** `Dispose()` called `Bitmap.Dispose()` unconditionally. If disposed twice (e.g. from both `DisposeVideoFrames()` and a stale reference), it would throw `ObjectDisposedException`.
- **Fix:** Added `_disposed` guard flag.

## PasteClipboardEffectAt swallows exceptions silently
- **File:** `ViewModels/MainWindowViewModel.cs`
- **Severity:** Low
- **Problem:** Bare `catch` block in `PasteClipboardEffectAt` discarded all exception information. Paste failures from corrupt clipboard data were invisible — no log, no status message.
- **Fix:** Changed to `catch (Exception ex)` with `AddDebug()` call so the error appears in the debug panel.

## EffectInstanceViewModel.Fill allocates brush on every access
- **File:** `ViewModels/EffectInstanceViewModel.cs`
- **Severity:** Low
- **Problem:** The `Fill` property getter created a new `SolidColorBrush` every time it was read. During timeline rendering with many effects, this produced significant short-lived garbage.
- **Fix:** Cache the brush in `_fillCache`. Invalidate in `NotifyColorChanged()`.

## CancellationTokenSource leaked on re-import
- **File:** `MainWindow.axaml.cs`
- **Severity:** Medium
- **Problem:** `ImportMedia_OnClick` and `OpenProject_OnClick` created a new `CancellationTokenSource` each time but only cancelled the previous one — never disposed it. Over repeated imports, CTS objects (which hold OS handles) accumulated until GC.
- **Fix:** Added `_decodeCts?.Dispose()` before creating a new CTS in both methods.

## _suppressEffectApply not reset on exception
- **File:** `ViewModels/MainWindowViewModel.cs`
- **Severity:** Medium
- **Problem:** `LoadSelectedEffect()` set `_suppressEffectApply = true`, then assigned multiple properties, then set it back to `false`. If any property setter threw, the flag stayed `true` forever, silently disabling all future effect edits for the session.
- **Fix:** Wrapped the property assignments in `try/finally` so the flag is always reset.

## NormalizeWaveform crashes on empty input
- **File:** `ViewModels/MainWindowViewModel.cs`
- **Severity:** Medium
- **Problem:** `NormalizeWaveform` used `Math.Max(1, samples.Count)` for the loop bound but indexed into `samples[i]`. When `samples.Count == 0`, the loop ran once and threw `IndexOutOfRangeException`.
- **Fix:** Added early return for empty input. Simplified `count` to just `samples.Count`.

## Border brush allocates on every access
- **File:** `ViewModels/EffectInstanceViewModel.cs`
- **Severity:** Low
- **Problem:** The `Border` property created a new `SolidColorBrush` via `Color.Parse()` on every read. With many effects, this produced garbage during timeline rendering — same issue as `Fill` before caching.
- **Fix:** Replaced with `static readonly` brush fields. Since the two border colors are constants, no per-instance allocation is needed at all.

## Zoom property has no bounds
- **File:** `ViewModels/MainWindowViewModel.cs`
- **Severity:** Low
- **Problem:** The `Zoom` setter accepted any `double` value without clamping. Negative, zero, NaN, or extremely large values would break timeline layout math (division by zero, infinite widths, negative pixel positions).
- **Fix:** Added `Math.Clamp(value, 0.05, 4.0)` before storing the value.

## RebuildChannels reversed order
- **File:** `ViewModels/MainWindowViewModel.cs`
- **Severity:** High
- **Problem:** `RebuildChannels()` traversed the linked list forward [Ch0, Ch1, Ch2] but added entries to the UI collection in reverse (`for i = list.Count - 1 downto 0`). Channels appeared bottom-to-top in the timeline — the first channel showed at the bottom instead of the top.
- **Fix:** Changed the reverse loop to forward iteration.

## _isScrubbing set before sender validation
- **File:** `MainWindow.axaml.cs`
- **Severity:** High
- **Problem:** `Timeline_OnPointerPressed` set `_isScrubbing = true` before checking `if (sender is not Control)`. If sender wasn't a Control, the method returned early with the flag stuck true — pointer release never fires because the pointer was never captured, leaving the UI permanently in scrubbing mode.
- **Fix:** Moved `_isScrubbing = true` to after the sender validation check.

## Missing swr_alloc null check and swr_init error check
- **File:** `Services/MediaDecodeService.cs`
- **Severity:** High
- **Problem:** `swr_alloc()` can return null on OOM, but lines 149-155 immediately dereferenced it via `av_opt_set_*` calls. Additionally, `swr_init()` returns a negative error code on invalid configuration, but the return value was ignored — proceeding with a broken resampler that silently produces corrupt audio.
- **Fix:** Added null check after `swr_alloc()` and error check on `swr_init()` return value. On failure, clean up and return empty audio.

## av_image_get_buffer_size negative return cast to ulong
- **File:** `Services/MediaDecodeService.cs`
- **Severity:** High
- **Problem:** `av_image_get_buffer_size()` returns negative error codes on invalid parameters. The return value was cast directly to `ulong` without validation — a negative int wraps to ~2^64, causing `av_malloc` to attempt a massive allocation.
- **Fix:** Added `bufferSize <= 0` check before the cast; on failure, clean up and return empty video.

## Menlo font hardcoded in script editor
- **File:** `MainWindow.axaml.cs`
- **Severity:** Medium
- **Problem:** The script editor's line number and text controls used `FontFamily("Menlo")`, which only exists on macOS. On Windows/Linux, Avalonia falls back to a non-monospace font, breaking line number alignment with the editor text.
- **Fix:** Changed to a cross-platform monospace font family fallback chain: `"Cascadia Mono, JetBrains Mono, Consolas, Menlo, monospace"`.

## Missing video dimension validation
- **File:** `Services/MediaDecodeService.cs`
- **Severity:** Medium
- **Problem:** `codecContext->width` and `height` were passed directly to `sws_getContext` without checking for zero or negative values. Malformed video files could have zero dimensions, wasting resources on FFmpeg calls that would fail.
- **Fix:** Added early return when width or height is <= 0.

## BoolToColorConverter allocates brush per call
- **File:** `Converters/BoolToColorConverter.cs`
- **Severity:** Low
- **Problem:** The converter created a `new SolidColorBrush` on every binding evaluation. For cue markers in the timeline, this produced significant short-lived garbage — same pattern as the Fill/Border brush issues fixed earlier.
- **Fix:** Replaced with static readonly cached brush fields.

## EditStartBrush/EditEndBrush allocate brush per access
- **File:** `ViewModels/MainWindowViewModel.cs`
- **Severity:** Low
- **Problem:** `EditStartBrush` and `EditEndBrush` were expression-bodied properties that created a `new SolidColorBrush` on every read. Same pattern as the Fill/Border/BoolToColorConverter issues fixed earlier — unnecessary GC pressure during color editing.
- **Fix:** Added `_editStartBrushCache` / `_editEndBrushCache` fields. Cache invalidated when the corresponding `EditStartRgb` / `EditEndRgb` setters fire.

## Reviewed and determined not bugs

- **RemoveEffectFromList silent no-op:** Private method; callers always pass valid data. No caller checks a return value.
- **RgbColor.FromJson returns default (0,0,0):** Struct deserialization — can't return null. Missing fields defaulting to zero is normal behavior.
- **FfmpegLoader native handles never freed:** Process-lifetime handles. FFmpeg stays loaded for the entire session; OS reclaims on exit.
- **LayoutSettings no validation:** `ApplyLayout()` already guards every value with `> 0` before applying. NaN, negative, and zero are handled at the consumer.
