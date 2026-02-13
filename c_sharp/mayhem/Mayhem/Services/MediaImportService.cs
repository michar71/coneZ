using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Platform.Storage;

namespace Mayhem.Services;

public sealed class MediaImportService
{
    private static readonly FilePickerFileType AllMediaType = new("All Media (*.wav, *.mp3, *.ogg, *.flac, *.aac, *.m4a, *.mp4, *.mov, *.avi, *.mkv, *.webm)")
    {
        Patterns = new List<string>
        {
            "*.wav", "*.mp3", "*.ogg", "*.flac", "*.aac", "*.m4a",
            "*.mp4", "*.mov", "*.avi", "*.mkv", "*.webm"
        }
    };

    private static readonly FilePickerFileType AudioFileType = new("Audio (*.wav, *.mp3, *.ogg, *.flac, *.aac, *.m4a)")
    {
        Patterns = new List<string>
        {
            "*.wav", "*.mp3", "*.ogg", "*.flac", "*.aac", "*.m4a"
        }
    };

    private static readonly FilePickerFileType VideoFileType = new("Video (*.mp4, *.mov, *.avi, *.mkv, *.webm)")
    {
        Patterns = new List<string>
        {
            "*.mp4", "*.mov", "*.avi", "*.mkv", "*.webm"
        }
    };

    public async Task<string?> PickMediaAsync(Window owner)
    {
        var options = new FilePickerOpenOptions
        {
            AllowMultiple = false,
            FileTypeFilter = new List<FilePickerFileType> { AllMediaType, AudioFileType, VideoFileType }
        };

        var files = await owner.StorageProvider.OpenFilePickerAsync(options).ConfigureAwait(true);
        var file = files.FirstOrDefault();
        return file?.Path.LocalPath;
    }
}
