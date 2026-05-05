using Avalonia;
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading.Tasks;

namespace Mayhem;

class Program
{
    // Initialization code. Don't use any Avalonia, third-party APIs or any
    // SynchronizationContext-reliant code before AppMain is called: things aren't initialized
    // yet and stuff might break.
    [STAThread]
    public static void Main(string[] args)
    {
        ConfigureWorkingDirectory();

        // DBus connection cleanup races with dispatcher shutdown on Linux,
        // throwing TaskCanceledException on a threadpool thread. Safe to ignore.
        AppDomain.CurrentDomain.UnhandledException += (_, e) =>
        {
            if (e.ExceptionObject is TaskCanceledException)
                Environment.Exit(0);
        };

        BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
    }

    // Avalonia configuration, don't remove; also used by visual designer.
    public static AppBuilder BuildAvaloniaApp()
        => AppBuilder.Configure<App>()
            .UsePlatformDetect()
            .WithInterFont()
            .LogToTrace();

    private static void ConfigureWorkingDirectory()
    {
        if (!RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
        {
            return;
        }

        var currentDirectory = Directory.GetCurrentDirectory();
        if (currentDirectory != Path.GetPathRoot(currentDirectory))
        {
            return;
        }

        var appData = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
        if (string.IsNullOrWhiteSpace(appData))
        {
            return;
        }

        var workspace = Path.Combine(appData, "Mayhem");
        Directory.CreateDirectory(workspace);
        SeedBundledScripts(workspace);
        Directory.SetCurrentDirectory(workspace);
    }

    private static void SeedBundledScripts(string workspace)
    {
        var resourcesScripts = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "Resources", "Scripts"));
        if (!Directory.Exists(resourcesScripts))
        {
            return;
        }

        var targetScripts = Path.Combine(workspace, "Scripts");
        Directory.CreateDirectory(targetScripts);

        foreach (var sourcePath in Directory.GetFiles(resourcesScripts))
        {
            var targetPath = Path.Combine(targetScripts, Path.GetFileName(sourcePath));
            if (!File.Exists(targetPath))
            {
                File.Copy(sourcePath, targetPath);
            }
        }
    }
}
