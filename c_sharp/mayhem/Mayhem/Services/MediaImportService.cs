using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Platform.Storage;

namespace Mayhem.Services;

public sealed class MediaImportService
{
    private static readonly FilePickerFileType MediaFileType = new("Media")
    {
        Patterns = new List<string>
        {
            "*.wav", "*.mp3", "*.ogg", "*.flac", "*.aac", "*.m4a",
            "*.mp4", "*.mov", "*.avi", "*.mkv", "*.webm"
        }
    };

    public async Task<string?> PickMediaAsync(Window owner)
    {
        var options = new FilePickerOpenOptions
        {
            AllowMultiple = false,
            FileTypeFilter = new List<FilePickerFileType> { MediaFileType }
        };

        var files = await owner.StorageProvider.OpenFilePickerAsync(options).ConfigureAwait(true);
        var file = files.FirstOrDefault();
        return file?.Path.LocalPath;
    }
}
