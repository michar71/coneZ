using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Platform.Storage;

namespace Mayhem.Services;

public sealed class ProjectFileService
{
    private static readonly FilePickerFileType ProjectFileType = new("Cue List File")
    {
        Patterns = new List<string> { "*.clf" }
    };

    public async Task<string?> PickOpenAsync(Window owner)
    {
        var options = new FilePickerOpenOptions
        {
            AllowMultiple = false,
            FileTypeFilter = new List<FilePickerFileType> { ProjectFileType }
        };

        var files = await owner.StorageProvider.OpenFilePickerAsync(options).ConfigureAwait(true);
        var file = files.FirstOrDefault();
        return file?.Path.LocalPath;
    }

    public async Task<string?> PickSaveAsync(Window owner, string? suggestedName = null)
    {
        if (!string.IsNullOrWhiteSpace(suggestedName) &&
            suggestedName.EndsWith(".clf", System.StringComparison.OrdinalIgnoreCase))
        {
            suggestedName = System.IO.Path.GetFileNameWithoutExtension(suggestedName);
        }

        var options = new FilePickerSaveOptions
        {
            SuggestedFileName = suggestedName,
            FileTypeChoices = new List<FilePickerFileType> { ProjectFileType },
            DefaultExtension = "clf"
        };

        var file = await owner.StorageProvider.SaveFilePickerAsync(options).ConfigureAwait(true);
        return file?.Path.LocalPath;
    }
}
