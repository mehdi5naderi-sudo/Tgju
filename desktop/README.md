# TGJU Desktop

Portable Windows tray app for TGJU market indicators.

- System tray icon + popup (no main window)
- Self-contained single EXE (win-x64)
- Auto refresh every 5 minutes
- Keeps last good values if a refresh fails

## Size

Release build uses **trimmed** self-contained publish (typically ~15–25 MB instead of ~60–80 MB).

### Local publish (small portable EXE)

```bat
cd desktop
dotnet publish -c Release -r win-x64 --self-contained true ^
  -p:PublishSingleFile=true ^
  -p:EnableCompressionInSingleFile=true ^
  -p:PublishTrimmed=true ^
  -p:TrimMode=partial ^
  -p:DebugType=None ^
  -o publish
```

Output: `desktop/publish/TGJU.exe`

### Even smaller (needs .NET 8 runtime on the PC)

```bat
dotnet publish -c Release -r win-x64 --self-contained false -p:PublishSingleFile=true -o publish-fdd
```

This is only a few MB, but the machine must have [.NET 8 Desktop Runtime](https://dotnet.microsoft.com/download/dotnet/8.0) installed.

## Run from source

```bat
cd desktop
dotnet run -c Release
```
