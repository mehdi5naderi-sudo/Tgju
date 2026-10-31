# TGJU Native

برنامه‌ی سبک ویندوز با ++C و Win32؛ بدون نیاز به نصب .NET.

## امکانات
- آیکون در System Tray
- باز کردن پنجره و دریافت داده با کلیک چپ
- نمایش فشرده‌ی شاخص‌ها
- دریافت اطلاعات از `api.tgju.org` با WinHTTP
- ثبت گزارش در `%TEMP%\\TGJU-native.log`

## ساخت محلی

در Developer Command Prompt ویژوال استودیو:

```bat
cd desktop-native
cl /nologo /O1 /GL /Gy /EHsc /utf-8 /DUNICODE /D_UNICODE main.cpp /Fe:TGJU-native.exe /link /LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO /DEBUG:NONE /SUBSYSTEM:WINDOWS user32.lib gdi32.lib shell32.lib winhttp.lib
```

یا فایل آماده را از [GitHub Releases](https://github.com/mehdi5naderi-sudo/Tgju/releases/latest) دریافت کنید. ساخت خودکار نیز از طریق گردش‌کار `build-tgju-native.yml` در GitHub Actions انجام می‌شود.
