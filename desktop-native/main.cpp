#define WIN32_LEAN_AND_MEAN
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
    std::string t; // API field "t": clock (۱۱:۲۰:۰۵) OR date label (۷ مهر)
};

static HWND gMain = nullptr;
static HWND gPopup = nullptr;
static NOTIFYICONDATAW gNid{};
static bool gPopupVisible = false;
static DWORD gPopupShownTick = 0;
static std::map<std::string, Quote> gData;
static CRITICAL_SECTION gCs;
static std::wstring gStatus = L"در حال دریافت…";
static std::wstring gError;
static std::string gLogPath;
static bool gFetching = false;

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

        next[kItems[i].key] = q;

        char line[320];
        sprintf_s(line, "key=%s p=%s dp=%.2f t='%s'",
            kItems[i].key, q.p.c_str(), q.dp, q.t.c_str());
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
    const int w = 700, h = 72 + kCount * 52 + 28;
    int x = pt.x - w + 24;
    int y = pt.y - h - 12;
    if (x < 8) x = 8;
    if (y < 8) y = pt.y + 36;

    SetWindowPos(gPopup, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW);
    ShowWindow(gPopup, SW_SHOWNOACTIVATE);
    gPopupVisible = true;
    gPopupShownTick = GetTickCount();
    InvalidateRect(gPopup, nullptr, TRUE);
    UpdateWindow(gPopup);
}

static void HidePopup() {
    if (!gPopupVisible) return;
    if (GetTickCount() - gPopupShownTick < 2000) return;
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
    const int m = 18;

    HBRUSH bg = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    RECT bar{ 0, 0, W, 3 };
    HBRUSH accent = CreateSolidBrush(RGB(16, 122, 186));
    FillRect(hdc, &bar, accent);
    DeleteObject(accent);

    RECT head{ 0, 3, W, 56 };
    HBRUSH headBg = CreateSolidBrush(RGB(247, 249, 252));
    FillRect(hdc, &head, headBg);
    DeleteObject(headBg);

    SetBkMode(hdc, TRANSPARENT);
    HFONT titleFont = CreateFontW(26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HFONT rowFont = CreateFontW(22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HFONT smallFont = CreateFontW(17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

    std::wstring status, error;
    std::map<std::string, Quote> snap;
    EnterCriticalSection(&gCs);
    status = gStatus;
    error = gError;
    snap = gData;
    LeaveCriticalSection(&gCs);

    HFONT old = (HFONT)SelectObject(hdc, titleFont);
    DrawTextRect(hdc, RECT{ m, 8, W - m, 48 }, L"شاخص‌های بازار", DT_RIGHT, RGB(30, 40, 55));
    SelectObject(hdc, smallFont);
    DrawTextRect(hdc, RECT{ m, 16, W / 2 - 65, 46 }, status.c_str(), DT_LEFT, RGB(100, 110, 125));
    DrawTextRect(hdc, RECT{ W / 2 - 55, 16, W - m, 46 }, kAppVersion, DT_RIGHT, RGB(16, 122, 186));

    const int nameW = 165;
    const int priceW = 195;
    const int chgW = 115;

    auto colName  = [&](int y1, int y2) { return RECT{ W - m - nameW, y1, W - m, y2 }; };
    auto colPrice = [&](int y1, int y2) { return RECT{ W - m - nameW - priceW, y1, W - m - nameW, y2 }; };
    auto colChg   = [&](int y1, int y2) { return RECT{ W - m - nameW - priceW - chgW, y1, W - m - nameW - priceW, y2 }; };
    auto colTime  = [&](int y1, int y2) { return RECT{ m, y1, W - m - nameW - priceW - chgW - 4, y2 }; };

    int y0 = 62;
    SelectObject(hdc, smallFont);
    DrawTextRect(hdc, colName(y0, y0 + 18),  L"شاخص", DT_RIGHT, RGB(120, 130, 145));
    DrawTextRect(hdc, colPrice(y0, y0 + 18), L"قیمت", DT_CENTER, RGB(120, 130, 145));
    DrawTextRect(hdc, colChg(y0, y0 + 18),   L"تغییر", DT_CENTER, RGB(120, 130, 145));
    DrawTextRect(hdc, colTime(y0, y0 + 18),  L"زمان", DT_CENTER, RGB(120, 130, 145));

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(230, 234, 240));
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, m, y0 + 20, nullptr);
    LineTo(hdc, W - m, y0 + 20);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    SelectObject(hdc, rowFont);
    for (int i = 0; i < kCount; i++) {
        int y = 92 + i * 52;

        if (i % 2 == 0) {
            RECT zr{ m - 4, y - 4, W - m + 4, y + 46 };
            HBRUSH zb = CreateSolidBrush(RGB(250, 251, 253));
            FillRect(hdc, &zr, zb);
            DeleteObject(zb);
        }

        DrawTextRect(hdc, colName(y, y + 44), kItems[i].label, DT_RIGHT, RGB(25, 30, 40));

        auto it = snap.find(kItems[i].key);
        if (it == snap.end() || it->second.p.empty()) {
            DrawTextRect(hdc, colPrice(y, y + 28), L"—", DT_CENTER, RGB(170, 175, 185));
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

        const bool isNegative = q.dp < -0.000001;
        const bool isPositive = q.dp > 0.000001;
        COLORREF c = isNegative ? RGB(210, 25, 35) : (isPositive ? RGB(0, 140, 70) : RGB(90, 95, 105));

        DrawTextRect(hdc, colPrice(y, y + 28), price.c_str(), DT_CENTER, c);

        wchar_t chg[32];
        swprintf_s(chg, L"%+.2f%%", q.dp);
        DrawTextRect(hdc, colChg(y, y + 28), ToPersianDigits(chg).c_str(), DT_CENTER, c);

        // Decode TGJU "t" correctly (clock or date label) before drawing.
        // This avoids mojibake when the API sends Persian text in UTF-8/CP1256
        // or as JSON unicode escapes.
        std::wstring tShow = q.t.empty() ? L"—" : ToPersianDigits(Wide(q.t));
        DrawTextRect(hdc, colTime(y, y + 28), tShow.c_str(), DT_CENTER, RGB(110, 120, 135));
    }

    if (!error.empty()) {
        SelectObject(hdc, smallFont);
        DrawTextRect(hdc, RECT{ m, rc.bottom - 24, W - m, rc.bottom - 5 },
            error.c_str(), DT_RIGHT, RGB(180, 50, 50));
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
            if (GetTickCount() - last > 900) {
                last = GetTickCount();
                ShowPopup(); StartFetch();
            }
        } else if (ev == WM_RBUTTONUP || ev == WM_CONTEXTMENU) {
            POINT pt; GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1001, L"به‌روزرسانی");
            AppendMenuW(menu, MF_STRING, 1002, L"خروج");
            SetForegroundWindow(hwnd);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_RIGHTALIGN, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
        }
        return 0;
    }
    if (msg == WM_COMMAND) {
        if (LOWORD(wp) == 1001) { ShowPopup(); StartFetch(); }
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
    if (msg == WM_TIMER && wp == kTimerId) { StartFetch(); return 0; }
    if (msg == WM_DESTROY) {
        KillTimer(hwnd, kTimerId);
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

    int h = 72 + kCount * 52 + 28;
    gPopup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kClassPopup, L"TGJU",
        WS_POPUP | WS_BORDER, 100, 100, 700, h, nullptr, nullptr, hi, nullptr);

    AddTray(hi);
    SetTimer(gMain, kTimerId, 5 * 60 * 1000, nullptr);
    StartFetch();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DeleteCriticalSection(&gCs);
    return 0;
}
