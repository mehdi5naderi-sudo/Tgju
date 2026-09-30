#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#include <string>
#include <vector>
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

// name key from API ("name" field), Persian label, display mode
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
    std::string t;
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
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

// Convert ASCII digits in time string to Persian digits for display
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
    if (decimals == 0)
        swprintf_s(buf, L"%.0f", v);
    else
        swprintf_s(buf, L"%.2f", v);

    // thousand separators for integer part
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

static std::string JsonStr(const std::string& obj, const char* key) {
    std::string pat = std::string("\"") + key + "\":\"";
    auto p = obj.find(pat);
    if (p == std::string::npos) return {};
    p += pat.size();
    auto e = obj.find('"', p);
    if (e == std::string::npos) return {};
    return obj.substr(p, e - p);
}

static double JsonNum(const std::string& obj, const char* key) {
    std::string pat = std::string("\"") + key + "\":";
    auto p = obj.find(pat);
    if (p == std::string::npos) return 0;
    p += pat.size();
    while (p < obj.size() && (obj[p] == ' ' || obj[p] == '\t')) p++;
    try { return std::stod(obj.substr(p)); } catch (...) { return 0; }
}

static bool HttpGet(const std::wstring& host, const std::wstring& path, std::string& out, std::string& err) {
    out.clear();
    HINTERNET ses = WinHttpOpen(L"TGJU-Native/1.1",
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
    size_t pos = 0;
    while (true) {
        auto n = body.find("\"name\":\"", pos);
        if (n == std::string::npos) break;
        auto start = body.rfind('{', n);
        auto end = body.find('}', n);
        if (start == std::string::npos || end == std::string::npos) { pos = n + 8; continue; }
        std::string obj = body.substr(start, end - start + 1);
        std::string name = JsonStr(obj, "name");
        std::string slug = JsonStr(obj, "slug");
        Quote q;
        q.p = JsonStr(obj, "p");
        q.dp = JsonNum(obj, "dp");
        q.t = JsonStr(obj, "t");
        if (!name.empty()) next[name] = q;
        if (!slug.empty()) next[slug] = q;
        pos = end + 1;
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
        wchar_t ts[32];
        swprintf_s(ts, L"به‌روزرسانی %02d:%02d", st.wHour, st.wMinute);
        EnterCriticalSection(&gCs);
        gStatus = ToPersianDigits(ts);
        gError.clear();
        gFetching = false;
        LeaveCriticalSection(&gCs);
        Log("Parsed keys=" + std::to_string(gData.size()));
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
    const int w = 460, h = 52 + kCount * 36 + 36;
    int x = pt.x - w + 20;
    int y = pt.y - h - 12;
    if (x < 8) x = 8;
    if (y < 8) y = pt.y + 28;

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

static void DrawTextRect(HDC hdc, const RECT& r, const wchar_t* text, UINT fmt, COLORREF color) {
    SetTextColor(hdc, color);
    RECT rr = r;
    DrawTextW(hdc, text, -1, &rr, fmt | DT_RTLREADING | DT_NOPREFIX | DT_SINGLELINE | DT_VCENTER);
}

static void PaintPopup(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc; GetClientRect(hwnd, &rc);

    // clean white background
    HBRUSH bg = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    // top accent bar
    RECT bar{ 0, 0, rc.right, 4 };
    HBRUSH accent = CreateSolidBrush(RGB(16, 122, 186));
    FillRect(hdc, &bar, accent);
    DeleteObject(accent);

    // header strip
    RECT head{ 0, 4, rc.right, 40 };
    HBRUSH headBg = CreateSolidBrush(RGB(247, 249, 252));
    FillRect(hdc, &head, headBg);
    DeleteObject(headBg);

    SetBkMode(hdc, TRANSPARENT);
    HFONT titleFont = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HFONT rowFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HFONT smallFont = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

    std::wstring status, error;
    std::map<std::string, Quote> snap;
    EnterCriticalSection(&gCs);
    status = gStatus;
    error = gError;
    snap = gData;
    LeaveCriticalSection(&gCs);

    HFONT old = (HFONT)SelectObject(hdc, titleFont);
    RECT titleR{ 12, 8, rc.right - 12, 36 };
    DrawTextRect(hdc, titleR, L"شاخص‌های بازار", DT_RIGHT, RGB(30, 40, 55));

    SelectObject(hdc, smallFont);
    RECT stR{ 12, 12, rc.right - 140, 34 };
    DrawTextRect(hdc, stR, status.c_str(), DT_LEFT, RGB(100, 110, 125));

    // column headers
    SelectObject(hdc, smallFont);
    int y0 = 44;
    RECT hName{ 12, y0, 130, y0 + 20 };
    RECT hPrice{ 130, y0, 280, y0 + 20 };
    RECT hChg{ 280, y0, 370, y0 + 20 };
    RECT hTime{ 370, y0, rc.right - 12, y0 + 20 };
    DrawTextRect(hdc, hName, L"شاخص", DT_RIGHT, RGB(120, 130, 145));
    DrawTextRect(hdc, hPrice, L"قیمت", DT_CENTER, RGB(120, 130, 145));
    DrawTextRect(hdc, hChg, L"تغییر", DT_CENTER, RGB(120, 130, 145));
    DrawTextRect(hdc, hTime, L"زمان", DT_CENTER, RGB(120, 130, 145));

    // separator under headers
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(230, 234, 240));
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, 12, y0 + 22, nullptr);
    LineTo(hdc, rc.right - 12, y0 + 22);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    SelectObject(hdc, rowFont);
    for (int i = 0; i < kCount; i++) {
        int y = 70 + i * 36;

        // zebra row
        if (i % 2 == 0) {
            RECT zr{ 8, y - 4, rc.right - 8, y + 30 };
            HBRUSH zb = CreateSolidBrush(RGB(250, 251, 253));
            FillRect(hdc, &zr, zb);
            DeleteObject(zb);
        }

        RECT rName{ 12, y, 130, y + 28 };
        DrawTextRect(hdc, rName, kItems[i].label, DT_RIGHT, RGB(25, 30, 40));

        auto it = snap.find(kItems[i].key);
        if (it == snap.end()) {
            RECT rDash{ 130, y, rc.right - 12, y + 28 };
            DrawTextRect(hdc, rDash, L"—", DT_CENTER, RGB(170, 175, 185));
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

        COLORREF c = q.dp > 0 ? RGB(0, 140, 70) : (q.dp < 0 ? RGB(190, 40, 40) : RGB(90, 95, 105));

        RECT rPrice{ 130, y, 280, y + 28 };
        DrawTextRect(hdc, rPrice, price.c_str(), DT_CENTER, c);

        wchar_t chg[32];
        swprintf_s(chg, L"%+.2f%%", q.dp);
        std::wstring chgFa = ToPersianDigits(chg);
        RECT rChg{ 280, y, 370, y + 28 };
        DrawTextRect(hdc, rChg, chgFa.c_str(), DT_CENTER, c);

        std::wstring t = q.t.empty() ? L"—" : ToPersianDigits(Wide(q.t));
        RECT rTime{ 370, y, rc.right - 12, y + 28 };
        DrawTextRect(hdc, rTime, t.c_str(), DT_CENTER, RGB(110, 120, 135));
    }

    // footer
    if (!error.empty()) {
        SelectObject(hdc, smallFont);
        RECT fr{ 12, rc.bottom - 28, rc.right - 12, rc.bottom - 6 };
        DrawTextRect(hdc, fr, error.c_str(), DT_RIGHT, RGB(180, 50, 50));
    }

    SelectObject(hdc, old);
    DeleteObject(titleFont);
    DeleteObject(rowFont);
    DeleteObject(smallFont);
    EndPaint(hwnd, &ps);
}

static void AddTray(HINSTANCE hi) {
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
    Log("Tray added");
}

static void RemoveTray() {
    Shell_NotifyIconW(NIM_DELETE, &gNid);
}

static LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TRAY) {
        UINT ev = LOWORD(lp);
        if (ev == NIN_SELECT || ev == NIN_KEYSELECT || ev == WM_LBUTTONUP || ev == WM_LBUTTONDBLCLK) {
            ShowPopup();
            StartFetch();
        } else if (ev == WM_MOUSEMOVE) {
            static DWORD last = 0;
            if (GetTickCount() - last > 900) {
                last = GetTickCount();
                ShowPopup();
                StartFetch();
            }
        } else if (ev == WM_RBUTTONUP || ev == WM_CONTEXTMENU) {
            POINT pt; GetCursorPos(&pt);
            HMENU m = CreatePopupMenu();
            AppendMenuW(m, MF_STRING, 1001, L"به‌روزرسانی");
            AppendMenuW(m, MF_STRING, 1002, L"خروج");
            SetForegroundWindow(hwnd);
            TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_RIGHTALIGN, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(m);
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
    if (msg == WM_TIMER && wp == kTimerId) {
        StartFetch();
        return 0;
    }
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
    {
        std::ofstream f(gLogPath, std::ios::trunc);
        f << "=== TGJU Native started ===\n";
    }

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

    int h = 52 + kCount * 36 + 36;
    gPopup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kClassPopup, L"TGJU",
        WS_POPUP | WS_BORDER, 100, 100, 460, h, nullptr, nullptr, hi, nullptr);

    AddTray(hi);
    SetTimer(gMain, kTimerId, 5 * 60 * 1000, nullptr);
    StartFetch();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DeleteCriticalSection(&gCs);
    Log("exit");
    return 0;
}
