# TGJU Native (smallest)

Pure **Win32 C++** tray app — no .NET runtime.

Expected size: **~100–200 KB**.

## Features
- System tray icon
- Popup on **left click only** (refreshes data at the same time)
- No automatic background refresh
- Compact columns by default
- Fetches `api.tgju.org` via WinHTTP
- Log: `%TEMP%\\TGJU-native.log`

## Build (local, Developer Command Prompt for VS)

```bat
cd desktop-native
cl /O2 /EHsc /utf-8 /DUNICODE /D_UNICODE main.cpp /Fe:TGJU-native.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib shell32.lib winhttp.lib
```

`main.cpp` includes `tgju_p0.inc`, `tgju_p1.inc`, `tgju_p2.inc` (same folder).

Or download `TGJU-native.exe` from GitHub Actions / Releases.
