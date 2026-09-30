#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <fstream>
#include <ctime>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

static const wchar_t* kClassMain = L"TGJUNativeMain";
static const wchar_t* kClassPopup = L"TGJUNativePopup";
static const UINT WM_TRAY = WM_APP + 1;
static const UINT WM_DATA = WM_APP + 2;
static const UINT_PTR kTimerId = 1;

static const char* kSlugs[] = {
    "crypto-tether-irr", "price_dollar_rl", "geram18",
    "ime_fund_kahroba", "ime_fund_ayar", "ons", "oil_brent"
};
static const wchar_t* kNames[] = {
    L"USDT", L"USD", L"Gold18", L"Kahroba", L"Ayar", L"ONS", L"Brent"
};
static const int kCount = 7;

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
static std::wstring gStatus = L"loading...";
static std::wstring gError;
static std::string gLogPath;

static void Log(const std::string& line) {
    try {
        std::ofstream f(gLogPath, std::ios::app);
        SYSTEMTIME st; GetLocalTime(&st);
        char buf[64];
        sprintf_s(buf, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
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

// Very small JSON helpers for this fixed API shape
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
    HINTERNET ses = WinHttpOpen(L"TGJU-Native/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) { err = "WinHttpOpen failed"; return false; }

    // Prefer TLS1.2
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(ses, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    HINTERNET con = WinHttpConnect(ses, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!con) { err = "WinHttpConnect failed"; WinHttpCloseHandle(ses); return false; }

    HINTERNET req = WinHttpOpenRequest(con, L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!req) { err = "OpenRequest failed"; WinHttpCloseHandle(con); WinHttpCloseHandle(ses); return false; }

    // Ignore cert errors (same situation as .NET bypass on filtered networks)
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
    // Split roughly by objects containing "name"
    size_t pos = 0;
    while (true) {
        auto n = body.find("\"name\":\"", pos);
        if (n == std::string::npos) break;
        // find object bounds: nearest { before and } after
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

static DWORD WINAPI FetchThread(LPVOID) {
    Log("Fetch start");
    std::wstring path =
        L"/v1/widget/tmp?keys=crypto-tether-irr,price_dollar_rl,geram18,ime_fund_kahroba,ime_fund_ayar,ons,oil_brent";
    std::string body, err;
    if (!HttpGet(L"api.tgju.org", path, body, err)) {
        EnterCriticalSection(&gCs);
        gStatus = L"error";
        gError = Wide(err);
        LeaveCriticalSection(&gCs);
        Log("Fetch fail: " + err);
    } else {
        Log("HTTP ok len=" + std::to_string(body.size()));
        ParseAndStore(body);
        EnterCriticalSection(&gCs);
        gStatus = L"ok";
        gError.clear();
        LeaveCriticalSection(&gCs);
        Log("Parsed keys=" + std::to_string(gData.size()));
    }
    PostMessageW(gMain, WM_DATA, 0, 0);
    return 0;
}

static void StartFetch() {
    HANDLE h = CreateThread(nullptr, 0, FetchThread, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
}

static void ShowPopup() {
    POINT pt; GetCursorPos(&pt);
    const int w = 420, h = 360;
    int x = pt.x - w / 2;
    int y = pt.y - h - 16;
    if (x < 8) x = 8;
    if (y < 8) y = pt.y + 24;

    SetWindowPos(gPopup, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW);
    ShowWindow(gPopup, SW_SHOWNOACTIVATE);
    gPopupVisible = true;
    gPopupShownTick = GetTickCount();
    InvalidateRect(gPopup, nullptr, TRUE);
    UpdateWindow(gPopup);
    Log("ShowPopup");
}

static void HidePopup() {
    if (!gPopupVisible) return;
    if (GetTickCount() - gPopupShownTick < 2500) return;
    ShowWindow(gPopup, SW_HIDE);
    gPopupVisible = false;
    Log("HidePopup");
}

static void PaintPopup(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc; GetClientRect(hwnd, &rc);

    HBRUSH bg = CreateSolidBrush(RGB(255, 252, 220));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    SetBkMode(hdc, TRANSPARENT);
    HFONT font = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HFONT mono = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
    HFONT old = (HFONT)SelectObject(hdc, font);

    std::wstring status, error;
    std::map<std::string, Quote> snap;
    EnterCriticalSection(&gCs);
    status = gStatus;
    error = gError;
    snap = gData;
    LeaveCriticalSection(&gCs);

    SetTextColor(hdc, RGB(0, 0, 0));
    TextOutW(hdc, 12, 8, L"TGJU Prices", 11);
    TextOutW(hdc, 200, 8, status.c_str(), (int)status.size());
    if (!error.empty()) {
        SetTextColor(hdc, RGB(180, 0, 0));
        TextOutW(hdc, 12, 30, error.c_str(), (int)error.size());
        SetTextColor(hdc, RGB(0, 0, 0));
    }

    SelectObject(hdc, mono);
    TextOutW(hdc, 12, 55, L"Name", 4);
    TextOutW(hdc, 120, 55, L"Price", 5);
    TextOutW(hdc, 260, 55, L"Change", 6);

    int drawn = 0;
    for (int i = 0; i < kCount; i++) {
        int y = 80 + i * 32;
        TextOutW(hdc, 12, y, kNames[i], (int)wcslen(kNames[i]));
        auto it = snap.find(kSlugs[i]);
        if (it == snap.end()) {
            TextOutW(hdc, 120, y, L"---", 3);
            continue;
        }
        drawn++;
        const Quote& q = it->second;
        double p = 0;
        try { p = std::stod(q.p); } catch (...) {}
        wchar_t price[64];
        if (strcmp(kSlugs[i], "ons") == 0 || strcmp(kSlugs[i], "oil_brent") == 0)
            swprintf_s(price, L"%.2f", p);
        else
            swprintf_s(price, L"%.0f", p / 10.0);

        COLORREF c = q.dp > 0 ? RGB(0, 128, 0) : (q.dp < 0 ? RGB(180, 0, 0) : RGB(80, 80, 80));
        SetTextColor(hdc, c);
        TextOutW(hdc, 120, y, price, (int)wcslen(price));
        wchar_t chg[64];
        swprintf_s(chg, L"%+.2f%%", q.dp);
        TextOutW(hdc, 260, y, chg, (int)wcslen(chg));
        SetTextColor(hdc, RGB(0, 0, 0));
    }

    wchar_t foot[64];
    swprintf_s(foot, L"drawn=%d", drawn);
    SetTextColor(hdc, RGB(90, 90, 90));
    TextOutW(hdc, 12, rc.bottom - 24, foot, (int)wcslen(foot));

    SelectObject(hdc, old);
    DeleteObject(font);
    DeleteObject(mono);
    EndPaint(hwnd, &ps);
    Log("Paint drawn=" + std::to_string(drawn));
}

static void AddTray(HINSTANCE hi) {
    memset(&gNid, 0, sizeof(gNid));
    gNid.cbSize = sizeof(gNid);
    gNid.hWnd = gMain;
    gNid.uID = 1;
    gNid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    gNid.uCallbackMessage = WM_TRAY;
    gNid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(gNid.szTip, L"TGJU");
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
            if (GetTickCount() - last > 800) {
                last = GetTickCount();
                ShowPopup();
                StartFetch();
            }
        } else if (ev == WM_RBUTTONUP || ev == WM_CONTEXTMENU) {
            POINT pt; GetCursorPos(&pt);
            HMENU m = CreatePopupMenu();
            AppendMenuW(m, MF_STRING, 1001, L"Refresh");
            AppendMenuW(m, MF_STRING, 1002, L"Exit");
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
    gPopup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kClassPopup, L"TGJU",
        WS_POPUP | WS_BORDER, 100, 100, 420, 360, nullptr, nullptr, hi, nullptr);

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
