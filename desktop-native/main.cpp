#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#include <string>
#include <map>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <algorithm>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

static const wchar_t* kClassMain = L"TGJUNativeMain";
static const wchar_t* kClassPopup = L"TGJUNativePopup";
static const UINT WM_TRAY = WM_APP + 1;
static const UINT WM_DATA = WM_APP + 2;
static const UINT_PTR kTimerId = 1;
static const wchar_t* kAppVersion = L"نسخه ۱.۱";

enum class Mode { TomanDiv10, AsIs2, Index0 };

struct ItemDef {
    const char* key;
    const wchar_t* label;
    Mode mode;
};

static const ItemDef kItems[] = {
    { "price_dollar_rl",   L"دلار",           Mode::TomanDiv10 },
    { "crypto-tether-irr", L"تتر",            Mode::TomanDiv10 },
    { "sekee",             L"سکه امامی",      Mode::TomanDiv10 },
    { "geram18",           L"طلای ۱۸ عیار",   Mode::TomanDiv10 },
    { "bourse",            L"شاخص بورس",      Mode::Index0 },
    { "ime_fund_kahroba",  L"کهربا",          Mode::TomanDiv10 },
    { "ime_fund_ayar",     L"عیار",           Mode::TomanDiv10 },
    { "ons",               L"انس طلا",        Mode::AsIs2 },
    { "oil_brent",         L"نفت برنت",       Mode::AsIs2 },
};
static const int kCount = (int)(sizeof(kItems) / sizeof(kItems[0]));

struct Quote {
    std::string p;
    double dp = 0;
    std::string t; // API field "t": clock/date label
    std::string dt; // API field "dt": "high" or "low"
};

static HWND gMain = nullptr;
static HWND gPopup = nullptr;
static NOTIFYICONDATAW gNid{};
static bool gPopupVisible = false;
static DWORD gPopupShownTick = 0;
static DWORD gLastTrayMoveTick = 0;
static std::map<std::string, Quote> gData;
static CRITICAL_SECTION gCs;
static std::wstring gStatus = L"در حال دریافت…";
static std::wstring gError;
static std::string gLogPath;
static bool gFetching = false;

struct UiSettings {
    int fontSize=22, smallFontSize=17, rowGap=34;
    int nameW=145, priceW=175, chgW=105, timeW=130, margin=18;
    int fontWeight=700, fontBrightness=100, closeDelay=5, refreshMinutes=5;
};
static UiSettings gSettings;

static int ClampInt(int v,int lo,int hi){return std::max(lo,std::min(hi,v));}
static int BrightValue(int v){return ClampInt(v*gSettings.fontBrightness/100,0,255);}
static COLORREF BrightColor(COLORREF c){return RGB(BrightValue(GetRValue(c)),BrightValue(GetGValue(c)),BrightValue(GetBValue(c)));}

static std::wstring ConfigPath(){
    wchar_t a[MAX_PATH]{};
    DWORD n=GetEnvironmentVariableW(L"APPDATA",a,MAX_PATH);
    return (!n||n>=MAX_PATH)?L"TGJU-native.ini":std::wstring(a)+L"\\TGJU-native.ini";
}
static void LoadSettings(){
    std::wstring p=ConfigPath();
    gSettings.fontSize=GetPrivateProfileIntW(L"Display",L"FontSize",22,p.c_str());
    gSettings.smallFontSize=GetPrivateProfileIntW(L"Display",L"SmallFontSize",17,p.c_str());
    gSettings.rowGap=GetPrivateProfileIntW(L"Layout",L"RowGap",34,p.c_str());
    gSettings.nameW=GetPrivateProfileIntW(L"Columns",L"Name",145,p.c_str());
    gSettings.priceW=GetPrivateProfileIntW(L"Columns",L"Price",175,p.c_str());
    gSettings.chgW=GetPrivateProfileIntW(L"Columns",L"Change",105,p.c_str());
    gSettings.timeW=GetPrivateProfileIntW(L"Columns",L"Time",130,p.c_str());
    gSettings.margin=GetPrivateProfileIntW(L"Layout",L"Margin",18,p.c_str());
    gSettings.fontWeight=GetPrivateProfileIntW(L"Display",L"FontWeight",700,p.c_str());
    gSettings.fontBrightness=GetPrivateProfileIntW(L"Display",L"FontBrightness",100,p.c_str());
    gSettings.closeDelay=GetPrivateProfileIntW(L"Behavior",L"CloseDelay",5,p.c_str());
    gSettings.refreshMinutes=GetPrivateProfileIntW(L"Behavior",L"RefreshMinutes",5,p.c_str());
    gSettings.fontSize=ClampInt(gSettings.fontSize,16,36);
    gSettings.smallFontSize=ClampInt(gSettings.smallFontSize,12,24);
    gSettings.rowGap=ClampInt(gSettings.rowGap,26,60);
    gSettings.nameW=ClampInt(gSettings.nameW,90,240);
    gSettings.priceW=ClampInt(gSettings.priceW,100,260);
    gSettings.chgW=ClampInt(gSettings.chgW,80,180);
    gSettings.timeW=ClampInt(gSettings.timeW,70,220);
    gSettings.margin=ClampInt(gSettings.margin,8,35);
    gSettings.fontWeight=ClampInt(gSettings.fontWeight,400,900);
    gSettings.fontBrightness=ClampInt(gSettings.fontBrightness,60,140);
    gSettings.closeDelay=ClampInt(gSettings.closeDelay,1,30);
    gSettings.refreshMinutes=ClampInt(gSettings.refreshMinutes,1,60);
}
static void SaveSettings(){
    std::wstring p=ConfigPath();wchar_t b[32]{};
#define WCFG(sec,key,val) do{swprintf_s(b,L"%d",(val));WritePrivateProfileStringW(sec,key,b,p.c_str());}while(0)
    WCFG(L"Display",L"FontSize",gSettings.fontSize);WCFG(L"Display",L"SmallFontSize",gSettings.smallFontSize);
    WCFG(L"Display",L"FontWeight",gSettings.fontWeight);WCFG(L"Display",L"FontBrightness",gSettings.fontBrightness);
    WCFG(L"Layout",L"RowGap",gSettings.rowGap);WCFG(L"Layout",L"Margin",gSettings.margin);
    WCFG(L"Columns",L"Name",gSettings.nameW);WCFG(L"Columns",L"Price",gSettings.priceW);
    WCFG(L"Columns",L"Change",gSettings.chgW);WCFG(L"Columns",L"Time",gSettings.timeW);
    WCFG(L"Behavior",L"CloseDelay",gSettings.closeDelay);WCFG(L"Behavior",L"RefreshMinutes",gSettings.refreshMinutes);
#undef WCFG
}
static void ResizePopup(){
    if(!gPopup)return;
    int W=std::max(520,gSettings.margin*2+gSettings.nameW+gSettings.priceW+gSettings.chgW+gSettings.timeW);
    int H=54+kCount*gSettings.rowGap+18;
    SetWindowPos(gPopup,nullptr,0,0,W,H,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    InvalidateRect(gPopup,nullptr,TRUE);
}
static void ApplySettings(){
    SaveSettings();
    if(gMain){KillTimer(gMain,kTimerId+1);SetTimer(gMain,kTimerId+1,(UINT)gSettings.refreshMinutes*60U*1000U,nullptr);}
    ResizePopup();
}
static void PresetCompact(){
    gSettings=UiSettings{};
    gSettings.fontSize=22;gSettings.smallFontSize=16;gSettings.rowGap=30;
    gSettings.nameW=135;gSettings.priceW=165;gSettings.chgW=98;gSettings.timeW=112;gSettings.margin=14;
    ApplySettings();
}
static void PresetReadable(){
    gSettings=UiSettings{};
    gSettings.fontSize=24;gSettings.smallFontSize=18;gSettings.rowGap=38;
    gSettings.nameW=155;gSettings.priceW=185;gSettings.chgW=110;gSettings.timeW=135;gSettings.margin=18;
    gSettings.fontBrightness=112;gSettings.closeDelay=7;
    ApplySettings();
}

enum {
    CMD_FONT_SMALL=1201,CMD_FONT_MED=1202,CMD_FONT_LARGE=1203,
    CMD_BRIGHT_LOW=1211,CMD_BRIGHT_MED=1212,CMD_BRIGHT_HIGH=1213,
    CMD_WEIGHT_NORMAL=1221,CMD_WEIGHT_SEMI=1222,CMD_WEIGHT_BOLD=1223,
    CMD_ROW_TIGHT=1231,CMD_ROW_MED=1232,CMD_ROW_LOOSE=1233,
    CMD_NAME_NARROW=1241,CMD_NAME_MED=1242,CMD_NAME_WIDE=1243,
    CMD_PRICE_NARROW=1251,CMD_PRICE_MED=1252,CMD_PRICE_WIDE=1253,
    CMD_CHG_NARROW=1261,CMD_CHG_MED=1262,CMD_CHG_WIDE=1263,
    CMD_TIME_NARROW=1271,CMD_TIME_MED=1272,CMD_TIME_WIDE=1273,
    CMD_CLOSE_3=1281,CMD_CLOSE_5=1282,CMD_CLOSE_10=1283,
    CMD_REFRESH_1=1291,CMD_REFRESH_5=1292,CMD_REFRESH_10=1293,
    CMD_MARGIN_SMALL=1301,CMD_MARGIN_MED=1302,CMD_MARGIN_LARGE=1303,
    CMD_PRESET_COMPACT=1311,CMD_PRESET_READABLE=1312,CMD_RESET=1313
};
static void AddSubItem(HMENU sub,UINT id,const wchar_t* text,bool checked=false){
    AppendMenuW(sub,MF_STRING|(checked?MF_CHECKED:0),id,text);
}
static void AddSettingsMenu(HMENU menu){
    HMENU settings=CreatePopupMenu();
    HMENU font=CreatePopupMenu();AddSubItem(font,CMD_FONT_SMALL,L"کوچک");AddSubItem(font,CMD_FONT_MED,L"متوسط",true);AddSubItem(font,CMD_FONT_LARGE,L"بزرگ");
    HMENU bright=CreatePopupMenu();AddSubItem(bright,CMD_BRIGHT_LOW,L"کم");AddSubItem(bright,CMD_BRIGHT_MED,L"متوسط",true);AddSubItem(bright,CMD_BRIGHT_HIGH,L"روشن");
    HMENU weight=CreatePopupMenu();AddSubItem(weight,CMD_WEIGHT_NORMAL,L"معمولی");AddSubItem(weight,CMD_WEIGHT_SEMI,L"نیمه‌پررنگ");AddSubItem(weight,CMD_WEIGHT_BOLD,L"پررنگ",true);
    HMENU rows=CreatePopupMenu();AddSubItem(rows,CMD_ROW_TIGHT,L"فشرده");AddSubItem(rows,CMD_ROW_MED,L"متوسط",true);AddSubItem(rows,CMD_ROW_LOOSE,L"باز");
    HMENU name=CreatePopupMenu();AddSubItem(name,CMD_NAME_NARROW,L"کم");AddSubItem(name,CMD_NAME_MED,L"متوسط",true);AddSubItem(name,CMD_NAME_WIDE,L"زیاد");
    HMENU price=CreatePopupMenu();AddSubItem(price,CMD_PRICE_NARROW,L"کم");AddSubItem(price,CMD_PRICE_MED,L"متوسط",true);AddSubItem(price,CMD_PRICE_WIDE,L"زیاد");
    HMENU chg=CreatePopupMenu();AddSubItem(chg,CMD_CHG_NARROW,L"کم");AddSubItem(chg,CMD_CHG_MED,L"متوسط",true);AddSubItem(chg,CMD_CHG_WIDE,L"زیاد");
    HMENU time=CreatePopupMenu();AddSubItem(time,CMD_TIME_NARROW,L"کم");AddSubItem(time,CMD_TIME_MED,L"متوسط",true);AddSubItem(time,CMD_TIME_WIDE,L"زیاد");
    HMENU close=CreatePopupMenu();AddSubItem(close,CMD_CLOSE_3,L"۳ ثانیه");AddSubItem(close,CMD_CLOSE_5,L"۵ ثانیه",true);AddSubItem(close,CMD_CLOSE_10,L"۱۰ ثانیه");
    HMENU refresh=CreatePopupMenu();AddSubItem(refresh,CMD_REFRESH_1,L"۱ دقیقه");AddSubItem(refresh,CMD_REFRESH_5,L"۵ دقیقه",true);AddSubItem(refresh,CMD_REFRESH_10,L"۱۰ دقیقه");
    HMENU margin=CreatePopupMenu();AddSubItem(margin,CMD_MARGIN_SMALL,L"کم");AddSubItem(margin,CMD_MARGIN_MED,L"متوسط",true);AddSubItem(margin,CMD_MARGIN_LARGE,L"زیاد");
    AppendMenuW(settings,MF_POPUP,(UINT_PTR)font,L"اندازه فونت");
    AppendMenuW(settings,MF_POPUP,(UINT_PTR)bright,L"روشنایی فونت");
    AppendMenuW(settings,MF_POPUP,(UINT_PTR)weight,L"ضخامت فونت");
    AppendMenuW(settings,MF_POPUP,(UINT_PTR)rows,L"فاصله عمودی شاخص‌ها");
    AppendMenuW(settings,MF_SEPARATOR,0,nullptr);
    AppendMenuW(settings,MF_POPUP,(UINT_PTR)name,L"عرض ستون شاخص");
    AppendMenuW(settings,MF_POPUP,(UINT_PTR)price,L"عرض ستون قیمت");
    AppendMenuW(settings,MF_POPUP,(UINT_PTR)chg,L"عرض ستون تغییر");
    AppendMenuW(settings,MF_POPUP,(UINT_PTR)time,L"عرض ستون زمان");
    AppendMenuW(settings,MF_POPUP,(UINT_PTR)margin,L"حاشیه داخلی");
    AppendMenuW(settings,MF_SEPARATOR,0,nullptr);
    AppendMenuW(settings,MF_POPUP,(UINT_PTR)close,L"زمان بسته‌شدن");
    AppendMenuW(settings,MF_POPUP,(UINT_PTR)refresh,L"دوره به‌روزرسانی");
    AppendMenuW(settings,MF_SEPARATOR,0,nullptr);
    AppendMenuW(settings,MF_STRING,CMD_PRESET_COMPACT,L"پروفایل فشرده");
    AppendMenuW(settings,MF_STRING,CMD_PRESET_READABLE,L"پروفایل خوانا");
    AppendMenuW(settings,MF_STRING,CMD_RESET,L"بازنشانی به پیش‌فرض");
    AppendMenuW(menu,MF_POPUP,(UINT_PTR)settings,L"تنظیمات نمایش");
}

static void Log(const std::string& line) {
    try {
        std::ofstream f(gLogPath, std::ios::app);
        SYSTEMTIME st; GetLocalTime(&st);
        char buf[64];
        sprintf_s(buf, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
        f << buf << line << "\n";
    } catch (...) {}
}

static std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};

    // API text is normally UTF-8, but some TGJU fields can contain
    // JSON unicode escapes or legacy Windows-1256 text. Decode robustly.
    std::wstring out;
    out.reserve(s.size());

    // First handle JSON unicode escapes such as \\u06f1.
    for (size_t i = 0; i < s.size(); ) {
        if (i + 5 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
            unsigned int v = 0;
            bool ok = true;
            for (int j = 0; j < 4; ++j) {
                char ch = s[i + 2 + j];
                unsigned int d = 0;
                if (ch >= '0' && ch <= '9') d = ch - '0';
                else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
                else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
                else { ok = false; break; }
                v = (v << 4) | d;
            }
            if (ok) {
                out.push_back((wchar_t)v);
                i += 6;
                continue;
            }
        }
        out.push_back((unsigned char)s[i]);
        ++i;
    }

    // If no unicode escapes were present, decode the original bytes.
    if (out.size() == s.size()) {
        int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            s.c_str(), (int)s.size(), nullptr, 0);
        if (n > 0) {
            std::wstring w(n, 0);
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                s.c_str(), (int)s.size(), &w[0], n);
            return w;
        }

        n = MultiByteToWideChar(1256, 0, s.c_str(), (int)s.size(), nullptr, 0);
        if (n > 0) {
            std::wstring w(n, 0);
            MultiByteToWideChar(1256, 0, s.c_str(), (int)s.size(), &w[0], n);
            return w;
        }
    }

    // The escaped path above may have produced UTF-16 code units directly.
    // For normal ASCII/Persian text this is already the desired result.
    bool hasHigh = false;
    for (wchar_t ch : out) if (ch > 0x7F) { hasHigh = true; break; }
    if (hasHigh) return out;

    // ASCII-only text is safe as-is.
    return out;
}

static std::wstring ToPersianDigits(const std::wstring& in) {
    static const wchar_t* fa = L"۰۱۲۳۴۵۶۷۸۹";
    std::wstring out;
    out.reserve(in.size());
    for (wchar_t c : in) {
        if (c >= L'0' && c <= L'9') out += fa[c - L'0'];
        else out += c;
    }
    return out;
}

static std::wstring FormatNumber(double v, int decimals) {
    wchar_t buf[64];
    if (decimals == 0) swprintf_s(buf, L"%.0f", v);
    else swprintf_s(buf, L"%.2f", v);

    std::wstring s = buf;
    auto dot = s.find(L'.');
    std::wstring ip = (dot == std::wstring::npos) ? s : s.substr(0, dot);
    std::wstring fp = (dot == std::wstring::npos) ? L"" : s.substr(dot);

    bool neg = false;
    if (!ip.empty() && ip[0] == L'-') { neg = true; ip.erase(0, 1); }

    std::wstring grouped;
    int cnt = 0;
    for (int i = (int)ip.size() - 1; i >= 0; --i) {
        if (cnt && cnt % 3 == 0) grouped.insert(grouped.begin(), L',');
        grouped.insert(grouped.begin(), ip[i]);
        cnt++;
    }
    if (neg) grouped.insert(grouped.begin(), L'-');
    return ToPersianDigits(grouped + fp);
}

static std::string ExtractStrNear(const std::string& body, size_t from, const char* key, size_t window = 1500) {
    std::string pat = std::string("\"") + key + "\":\"";
    size_t start = (from > 500) ? from - 500 : 0;
    size_t endSearch = (from + window < body.size()) ? from + window : body.size();

    size_t best = std::string::npos;
    size_t p = body.find(pat, start);
    while (p != std::string::npos && p < endSearch) {
        if (p >= from) { best = p; break; }
        best = p;
        p = body.find(pat, p + pat.size());
    }
    if (best == std::string::npos) return {};
    size_t v = best + pat.size();
    auto e = body.find('"', v);
    if (e == std::string::npos) return {};
    return body.substr(v, e - v);
}

static double ExtractNumNear(const std::string& body, size_t from, const char* key, size_t window = 1500) {
    std::string pat = std::string("\"") + key + "\":";
    size_t start = (from > 500) ? from - 500 : 0;
    size_t endSearch = (from + window < body.size()) ? from + window : body.size();
    size_t p = body.find(pat, start);
    size_t best = std::string::npos;
    while (p != std::string::npos && p < endSearch) {
        if (p >= from) { best = p; break; }
        best = p;
        p = body.find(pat, p + pat.size());
    }
    if (best == std::string::npos) return 0;
    size_t v = best + pat.size();
    while (v < body.size() && (body[v] == ' ' || body[v] == '\t')) v++;
    try { return std::stod(body.substr(v)); } catch (...) { return 0; }
}

static bool HttpGet(const std::wstring& host, const std::wstring& path, std::string& out, std::string& err) {
    out.clear();
    HINTERNET ses = WinHttpOpen(L"TGJU-Native/1.5",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) { err = "WinHttpOpen failed"; return false; }

    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(ses, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    HINTERNET con = WinHttpConnect(ses, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!con) { err = "WinHttpConnect failed"; WinHttpCloseHandle(ses); return false; }

    HINTERNET req = WinHttpOpenRequest(con, L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!req) { err = "OpenRequest failed"; WinHttpCloseHandle(con); WinHttpCloseHandle(ses); return false; }

    DWORD flags =
        SECURITY_FLAG_IGNORE_UNKNOWN_CA |
        SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
        SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
        SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    WinHttpSetOption(req, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof(flags));

    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req, nullptr)) {
        err = "Send/Receive failed err=" + std::to_string(GetLastError());
        WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
        return false;
    }

    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
        std::string chunk(avail, 0);
        DWORD read = 0;
        if (!WinHttpReadData(req, &chunk[0], avail, &read) || read == 0) break;
        chunk.resize(read);
        out += chunk;
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    if (out.empty()) { err = "empty body"; return false; }
    return true;
}

static void ParseAndStore(const std::string& body) {
    std::map<std::string, Quote> next;

    for (int i = 0; i < kCount; i++) {
        std::string needle = std::string("\"name\":\"") + kItems[i].key + "\"";
        auto pos = body.find(needle);
        if (pos == std::string::npos) {
            Log(std::string("missing ") + kItems[i].key);
            continue;
        }

        // Restrict extraction to this indicator's JSON object.
        size_t objectEnd = body.find('}', pos);
        if (objectEnd == std::string::npos) objectEnd = body.size();
        std::string obj = body.substr(pos, objectEnd - pos + 1);
        size_t localName = obj.find(needle);
        Quote q;
        q.p = ExtractStrNear(obj, localName, "p", obj.size());
        q.dp = ExtractNumNear(obj, localName, "dp", obj.size());
        q.t = ExtractStrNear(obj, localName, "t", obj.size());
        q.dt = ExtractStrNear(obj, localName, "dt", obj.size());

        next[kItems[i].key] = q;

        char line[320];
        sprintf_s(line, "key=%s p=%s dp=%.2f dt=%s t='%s'",
            kItems[i].key, q.p.c_str(), q.dp, q.dt.c_str(), q.t.c_str());
        Log(line);
    }

    EnterCriticalSection(&gCs);
    gData.swap(next);
    LeaveCriticalSection(&gCs);
}

static std::wstring BuildKeysPath() {
    std::string keys;
    for (int i = 0; i < kCount; i++) {
        if (i) keys += ',';
        keys += kItems[i].key;
    }
    return L"/v1/widget/tmp?keys=" + Wide(keys);
}

static DWORD WINAPI FetchThread(LPVOID) {
    Log("Fetch start");
    std::string body, err;
    if (!HttpGet(L"api.tgju.org", BuildKeysPath(), body, err)) {
        EnterCriticalSection(&gCs);
        gStatus = L"خطا در دریافت";
        gError = Wide(err);
        gFetching = false;
        LeaveCriticalSection(&gCs);
        Log("Fetch fail: " + err);
    } else {
        Log("HTTP ok len=" + std::to_string(body.size()));
        ParseAndStore(body);
        SYSTEMTIME st; GetLocalTime(&st);
        wchar_t ts[48];
        swprintf_s(ts, L"خواندن %02d:%02d", st.wHour, st.wMinute);
        EnterCriticalSection(&gCs);
        gStatus = ToPersianDigits(ts);
        gError.clear();
        gFetching = false;
        LeaveCriticalSection(&gCs);
    }
    PostMessageW(gMain, WM_DATA, 0, 0);
    return 0;
}

static void StartFetch() {
    EnterCriticalSection(&gCs);
    if (gFetching) { LeaveCriticalSection(&gCs); return; }
    gFetching = true;
    gStatus = L"در حال دریافت…";
    LeaveCriticalSection(&gCs);
    HANDLE h = CreateThread(nullptr, 0, FetchThread, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
    else {
        EnterCriticalSection(&gCs);
        gFetching = false;
        LeaveCriticalSection(&gCs);
    }
}

static void ShowPopup() {
    POINT pt; GetCursorPos(&pt);
    const int w = std::max(520,gSettings.margin*2+gSettings.nameW+gSettings.priceW+gSettings.chgW+gSettings.timeW);
    const int h = 54+kCount*gSettings.rowGap+18;
    int x = pt.x - w + 24;
    int y = pt.y - h - 12;
    if (x < 8) x = 8;
    if (y < 8) y = pt.y + 36;

    SetWindowPos(gPopup, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW);
    ShowWindow(gPopup, SW_SHOWNOACTIVATE);
    gPopupVisible = true;
    gPopupShownTick = GetTickCount();
    gLastTrayMoveTick = GetTickCount();
    InvalidateRect(gPopup, nullptr, TRUE);
    UpdateWindow(gPopup);
}

static void HidePopup() {
    if (!gPopupVisible) return;
    ShowWindow(gPopup, SW_HIDE);
    gPopupVisible = false;
}

static void DrawTextRect(HDC hdc, RECT r, const wchar_t* text, UINT align, COLORREF color) {
    SetTextColor(hdc, color);
    DrawTextW(hdc, text, -1, &r,
        align | DT_RTLREADING | DT_NOPREFIX | DT_SINGLELINE | DT_VCENTER);
}

static void PaintPopup(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc; GetClientRect(hwnd, &rc);
    const int W = rc.right;
    const int m = gSettings.margin;

    HBRUSH bg = CreateSolidBrush(RGB(28, 32, 38));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    RECT bar{ 0, 0, W, 3 };
    HBRUSH accent = CreateSolidBrush(RGB(40, 145, 205));
    FillRect(hdc, &bar, accent);
    DeleteObject(accent);

    RECT head{ 0, 3, W, 44 };
    HBRUSH headBg = CreateSolidBrush(RGB(36, 41, 48));
    FillRect(hdc, &head, headBg);
    DeleteObject(headBg);

    SetBkMode(hdc, TRANSPARENT);
    HFONT titleFont = CreateFontW(26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HFONT rowFont = CreateFontW(gSettings.fontSize, 0, 0, 0, gSettings.fontWeight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HFONT smallFont = CreateFontW(gSettings.smallFontSize, 0, 0, 0, gSettings.fontWeight>=700 ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

    std::wstring status, error;
    std::map<std::string, Quote> snap;
    EnterCriticalSection(&gCs);
    status = gStatus;
    error = gError;
    snap = gData;
    LeaveCriticalSection(&gCs);

    HFONT old = (HFONT)SelectObject(hdc, titleFont);
    DrawTextRect(hdc, RECT{ m, 3, W - m, 40 }, L"شاخص‌های بازار", DT_RIGHT, BrightColor(RGB(245, 247, 250)));
    SelectObject(hdc, smallFont);
    DrawTextRect(hdc, RECT{ m, 6, W / 2 - 65, 38 }, status.c_str(), DT_LEFT, BrightColor(RGB(180, 190, 202)));
    DrawTextRect(hdc, RECT{ W / 2 - 55, 6, W - m, 38 }, kAppVersion, DT_RIGHT, BrightColor(RGB(70, 165, 220)));

    const int nameW = 145;
    const int priceW = 175;
    const int chgW = 105;

    auto colName  = [&](int y1, int y2) { return RECT{ W - m - nameW, y1, W - m, y2 }; };
    auto colPrice = [&](int y1, int y2) { return RECT{ W - m - nameW - priceW, y1, W - m - nameW, y2 }; };
    auto colChg   = [&](int y1, int y2) { return RECT{ W - m - nameW - priceW - chgW, y1, W - m - nameW - priceW, y2 }; };
    const int timeW = 110;
    auto colTime  = [&](int y1,int y2){return RECT{m,y1,m+timeW,y2};};

    int y0 = 46;
    SelectObject(hdc, smallFont);
    DrawTextRect(hdc, colName(y0, y0 + 16),  L"شاخص", DT_RIGHT, BrightColor(RGB(165, 175, 188)));
    DrawTextRect(hdc, colPrice(y0, y0 + 16), L"قیمت", DT_CENTER, RGB(165, 175, 188));
    DrawTextRect(hdc, colChg(y0, y0 + 16),   L"تغییر", DT_CENTER, RGB(165, 175, 188));
    DrawTextRect(hdc, colTime(y0, y0 + 16),  L"زمان", DT_CENTER, RGB(165, 175, 188));

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(58, 64, 73));
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, m, y0 + 18, nullptr);
    LineTo(hdc, W - m, y0 + 18);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    SelectObject(hdc, rowFont);
    for (int i = 0; i < kCount; i++) {
        int y = 68 + i * gSettings.rowGap;

        if (i % 2 == 0) {
            RECT zr{ m - 2, y - 1, W - m + 2, y + 31 };
            HBRUSH zb = CreateSolidBrush(RGB(38, 43, 50));
            FillRect(hdc, &zr, zb);
            DeleteObject(zb);
        }

        DrawTextRect(hdc, colName(y, y + 32), kItems[i].label, DT_RIGHT, BrightColor(RGB(242, 244, 247)));

        auto it = snap.find(kItems[i].key);
        if (it == snap.end() || it->second.p.empty()) {
            DrawTextRect(hdc, colPrice(y, y + 30), L"—", DT_CENTER, BrightColor(RGB(145, 152, 162)));
            continue;
        }

        const Quote& q = it->second;
        double p = 0;
        try { p = std::stod(q.p); } catch (...) {}

        std::wstring price;
        switch (kItems[i].mode) {
        case Mode::TomanDiv10: price = FormatNumber(p / 10.0, 0); break;
        case Mode::AsIs2:      price = FormatNumber(p, 2); break;
        case Mode::Index0:     price = FormatNumber(p, 0); break;
        }

        const bool isNegative = q.dt == "low";
        const bool isPositive = q.dt == "high";
        COLORREF c = BrightColor(isNegative ? RGB(210,25,35) : (isPositive ? RGB(0,140,70) : RGB(90,95,105)));

        DrawTextRect(hdc, colPrice(y, y + 28), price.c_str(), DT_CENTER, c);

        wchar_t chg[32];
        swprintf_s(chg, L"%+.2f%%", q.dp);
        DrawTextRect(hdc, colChg(y, y + 30), ToPersianDigits(chg).c_str(), DT_CENTER, c);

        // Decode TGJU "t" correctly (clock or date label) before drawing.
        // This avoids mojibake when the API sends Persian text in UTF-8/CP1256
        // or as JSON unicode escapes.
        std::wstring tShow = q.t.empty() ? L"—" : ToPersianDigits(Wide(q.t));
        DrawTextRect(hdc, colTime(y, y + 30), tShow.c_str(), DT_CENTER, c);
    }

    if (!error.empty()) {
        SelectObject(hdc, smallFont);
        DrawTextRect(hdc, RECT{ m, rc.bottom - 24, W - m, rc.bottom - 5 },
            error.c_str(), DT_RIGHT, BrightColor(RGB(180, 50, 50)));
    }

    SelectObject(hdc, old);
    DeleteObject(titleFont);
    DeleteObject(rowFont);
    DeleteObject(smallFont);
    EndPaint(hwnd, &ps);
}

static void AddTray(HINSTANCE) {
    memset(&gNid, 0, sizeof(gNid));
    gNid.cbSize = sizeof(gNid);
    gNid.hWnd = gMain;
    gNid.uID = 1;
    gNid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    gNid.uCallbackMessage = WM_TRAY;
    gNid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(gNid.szTip, L"شاخص‌های TGJU");
    Shell_NotifyIconW(NIM_ADD, &gNid);
    gNid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &gNid);
}

static void RemoveTray() {
    Shell_NotifyIconW(NIM_DELETE, &gNid);
}

static LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TRAY) {
        UINT ev = LOWORD(lp);
        if (ev == NIN_SELECT || ev == NIN_KEYSELECT || ev == WM_LBUTTONUP || ev == WM_LBUTTONDBLCLK) {
            ShowPopup(); StartFetch();
        } else if (ev == WM_MOUSEMOVE) {
            static DWORD last = 0;
            gLastTrayMoveTick = GetTickCount();
            if (GetTickCount() - last > 900) {
                last = GetTickCount();
                ShowPopup(); StartFetch();
            }
        } else if (ev == WM_RBUTTONUP || ev == WM_CONTEXTMENU) {
            POINT pt; GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1001, L"به‌روزرسانی");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AddSettingsMenu(menu);
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, 1002, L"خروج");
            SetForegroundWindow(hwnd);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_RIGHTALIGN, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
        }
        return 0;
    }
    if (msg == WM_COMMAND) {
        if (LOWORD(wp) == 1001) { ShowPopup(); StartFetch(); }
        switch(LOWORD(wp)){
        case CMD_FONT_SMALL:gSettings.fontSize=20;break;case CMD_FONT_MED:gSettings.fontSize=22;break;case CMD_FONT_LARGE:gSettings.fontSize=26;break;
        case CMD_BRIGHT_LOW:gSettings.fontBrightness=85;break;case CMD_BRIGHT_MED:gSettings.fontBrightness=100;break;case CMD_BRIGHT_HIGH:gSettings.fontBrightness=120;break;
        case CMD_WEIGHT_NORMAL:gSettings.fontWeight=400;break;case CMD_WEIGHT_SEMI:gSettings.fontWeight=600;break;case CMD_WEIGHT_BOLD:gSettings.fontWeight=700;break;
        case CMD_ROW_TIGHT:gSettings.rowGap=29;break;case CMD_ROW_MED:gSettings.rowGap=34;break;case CMD_ROW_LOOSE:gSettings.rowGap=40;break;
        case CMD_NAME_NARROW:gSettings.nameW=125;break;case CMD_NAME_MED:gSettings.nameW=145;break;case CMD_NAME_WIDE:gSettings.nameW=170;break;
        case CMD_PRICE_NARROW:gSettings.priceW=150;break;case CMD_PRICE_MED:gSettings.priceW=175;break;case CMD_PRICE_WIDE:gSettings.priceW=205;break;
        case CMD_CHG_NARROW:gSettings.chgW=90;break;case CMD_CHG_MED:gSettings.chgW=105;break;case CMD_CHG_WIDE:gSettings.chgW=125;break;
        case CMD_TIME_NARROW:gSettings.timeW=105;break;case CMD_TIME_MED:gSettings.timeW=130;break;case CMD_TIME_WIDE:gSettings.timeW=160;break;
        case CMD_MARGIN_SMALL:gSettings.margin=10;break;case CMD_MARGIN_MED:gSettings.margin=18;break;case CMD_MARGIN_LARGE:gSettings.margin=26;break;
        case CMD_CLOSE_3:gSettings.closeDelay=3;break;case CMD_CLOSE_5:gSettings.closeDelay=5;break;case CMD_CLOSE_10:gSettings.closeDelay=10;break;
        case CMD_REFRESH_1:gSettings.refreshMinutes=1;break;case CMD_REFRESH_5:gSettings.refreshMinutes=5;break;case CMD_REFRESH_10:gSettings.refreshMinutes=10;break;
        case CMD_PRESET_COMPACT:PresetCompact();break;case CMD_PRESET_READABLE:PresetReadable();break;case CMD_RESET:gSettings=UiSettings{};ApplySettings();break;
        default:break;}
        if(LOWORD(wp)>=CMD_FONT_SMALL && LOWORD(wp)<=CMD_RESET){if(LOWORD(wp)!=CMD_PRESET_COMPACT&&LOWORD(wp)!=CMD_PRESET_READABLE&&LOWORD(wp)!=CMD_RESET)ApplySettings();if(gPopupVisible)UpdateWindow(gPopup);}
        if (LOWORD(wp) == 1002) DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_DATA) {
        if (gPopupVisible) {
            InvalidateRect(gPopup, nullptr, TRUE);
            UpdateWindow(gPopup);
        }
        return 0;
    }
    if (msg == WM_TIMER && wp == kTimerId) {
        if (gPopupVisible && GetTickCount() - gLastTrayMoveTick >= (DWORD)gSettings.closeDelay*1000U) HidePopup();
        return 0;
    }
    if (msg == WM_TIMER && wp == kTimerId + 1) { StartFetch(); return 0; }
    if (msg == WM_DESTROY) {
        KillTimer(hwnd, kTimerId);
        KillTimer(hwnd, kTimerId+1);
        RemoveTray();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK PopupProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) { PaintPopup(hwnd); return 0; }
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_MOUSEMOVE) {
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }
    if (msg == WM_MOUSELEAVE) { HidePopup(); return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int) {
    InitializeCriticalSection(&gCs);

    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    gLogPath = Narrow(tmp) + "TGJU-native.log";
    { std::ofstream f(gLogPath, std::ios::trunc); f << "=== TGJU Native started ===\n"; }

    WNDCLASSW wc{};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hi;
    wc.lpszClassName = kClassMain;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    WNDCLASSW pc{};
    pc.lpfnWndProc = PopupProc;
    pc.hInstance = hi;
    pc.lpszClassName = kClassPopup;
    pc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&pc);

    gMain = CreateWindowExW(WS_EX_TOOLWINDOW, kClassMain, L"TGJU", 0,
        0, 0, 0, 0, nullptr, nullptr, hi, nullptr);

    LoadSettings();
    int popupW=std::max(520,gSettings.margin*2+gSettings.nameW+gSettings.priceW+gSettings.chgW+gSettings.timeW);
    int h=54+kCount*gSettings.rowGap+18;
    gPopup=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,kClassPopup,L"TGJU",WS_POPUP|WS_BORDER,100,100,popupW,h,nullptr,nullptr,hi,nullptr);

    AddTray(hi);
    SetTimer(gMain, kTimerId, 1000, nullptr);
    SetTimer(gMain,kTimerId+1,(UINT)gSettings.refreshMinutes*60U*1000U,nullptr);
    StartFetch();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DeleteCriticalSection(&gCs);
    return 0;
}
