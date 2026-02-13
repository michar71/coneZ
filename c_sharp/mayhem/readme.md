Mayhem is the software used to assemble cue-lists and associated effects in a timeline-like UI.
It then allows to bundle those cue-lists and effect-scripts into a "Piece" that can be deployed.

Mayhem is developed in C# using the Avalonia UI framework.


Requirements
------------

- .NET 10.0 SDK (or later)
- FFmpeg libraries (for audio/video decoding)


Linux (Debian/Ubuntu)
---------------------

  sudo apt install dotnet-sdk-10.0 ffmpeg libavcodec-dev libavformat-dev \
      libswscale-dev libswresample-dev libavutil-dev

  cd c_sharp/mayhem
  export FFMPEG_PATH=/usr/lib/x86_64-linux-gnu
  dotnet run --project Mayhem/Mayhem.csproj


macOS (Homebrew)
----------------

  brew install dotnet ffmpeg

  cd c_sharp/mayhem
  export FFMPEG_PATH=/opt/homebrew/opt/ffmpeg/lib
  dotnet run --project Mayhem/Mayhem.csproj
