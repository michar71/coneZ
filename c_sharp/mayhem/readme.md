Mayhem is the software used rot assemble cue-lists and accociated effects ina timeline-like UI.
It then allows to bundle those cue-lists and effect-scripts into a "Piece" that czn be deployed.

Mayham is developed in C#.

- Install the Microsoft .Net package for your system (Win/Linux/OS C)
- For Development also cloen  the Avalonia UI git into the "Avalonia" directory.
https://github.com/AvaloniaUI/Avalonia
(Might also require other steps like precompiling libraries on mac OS)
- Install ffMpeg as a command-line tool

- Start app like this:
export FFMPEG_PATH=/opt/homebrew/opt/ffmpeg/lib
dotnet run --project Mayhem/Mayhem.csproj
