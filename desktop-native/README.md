# TGJU Native (smallest)

Pure **Win32 C++** tray app — no .NET runtime.

Expected size: **~100–200 KB**.

## Features
- System tray icon
- Popup on click/hover
- Fetches `api.tgju.org` via WinHTTP (TLS 1.2, cert errors ignored for filtered networks)
- Log: `%TEMP%\TGJU-native.log`

## Build (local, Developer Command Prompt for VS)

```bat
cd desktop-native
cl /O2 /EHsc /utf-8 /DUNICODE /D_UNICODE main.cpp /Fe:TGJU-native.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib shell32.lib winhttp.lib
```

Or download `TGJU-native.exe` from GitHub Actions / Releases.
