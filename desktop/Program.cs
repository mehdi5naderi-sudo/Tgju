using System.Net;
using System.Runtime.InteropServices;
using System.Security.Authentication;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace TgjuDesktop;

internal static class Program
{
    [STAThread]
    static void Main()
    {
        using var app = new TrayApp();
        app.Run();
    }
}

internal sealed class TrayApp : IDisposable
{
    const uint WM_APP = 0x8000;
    const uint WM_TRAY = WM_APP + 1;
    const uint WM_DATA_READY = WM_APP + 3;
    const uint WM_PAINT = 0x000F;
    const uint WM_DESTROY = 0x0002;
    const uint WM_COMMAND = 0x0111;
    const uint WM_CONTEXTMENU = 0x007B;
    const uint WM_MOUSEMOVE = 0x0200;
    const uint WM_MOUSELEAVE = 0x02A3;
    const uint WM_LBUTTONUP = 0x0202;
    const uint WM_RBUTTONUP = 0x0205;
    const uint WM_LBUTTONDBLCLK = 0x0203;
    const uint WM_USER = 0x0400;
    const uint NIN_SELECT = WM_USER + 0;
    const uint NIN_KEYSELECT = WM_USER + 1;

    const int WS_EX_TOOLWINDOW = 0x00000080;
    const int WS_EX_TOPMOST = 0x00000008;
    const int WS_EX_NOACTIVATE = 0x08000000;
    const int WS_POPUP = unchecked((int)0x80000000);
    const int WS_BORDER = 0x00800000;
    const uint SW_HIDE = 0;
    const uint SW_SHOWNOACTIVATE = 4;
    const uint HWND_TOPMOST = unchecked(0xFFFFFFFF);
    const uint SWP_NOACTIVATE = 0x0010;
    const uint SWP_SHOWWINDOW = 0x0040;
    const uint NIM_ADD = 0;
    const uint NIM_DELETE = 2;
    const uint NIM_SETVERSION = 4;
    const uint NOTIFYICON_VERSION_4 = 4;
    const uint NIF_MESSAGE = 1;
    const uint NIF_ICON = 2;
    const uint NIF_TIP = 4;
    const uint NIF_SHOWTIP = 0x00000080;
    const uint TRANSPARENT = 1;
    const uint DT_CENTER = 0x00000001;
    const uint DT_VCENTER = 0x00000004;
    const uint DT_SINGLELINE = 0x00000020;
    const uint DT_RTLREADING = 0x00020000;
    const uint DT_NOPREFIX = 0x00000800;

    const int IDM_REFRESH = 1001;
    const int IDM_EXIT = 1002;
    const int TPM_RIGHTBUTTON = 0x0002;
    const int TPM_BOTTOMALIGN = 0x0020;
    const int TPM_RIGHTALIGN = 0x0008;

    readonly string[] slugs =
    {
        "crypto-tether-irr", "price_dollar_rl", "geram18",
        "ime_fund_kahroba", "ime_fund_ayar", "ons", "oil_brent"
    };
    readonly string[] names = { "تتر", "دلار", "گرم ۱۸", "کهربا", "عیار", "انس", "نفت برنت" };

    readonly HttpClient http;
    readonly object dataLock = new();
    readonly Dictionary<string, Quote> lastGood = new();
    readonly WndProcDelegate wndProc;
    readonly WndProcDelegate popupProc;
    readonly string className = "TGJUTrayNative";
    readonly string popupClass = "TGJUPopupNative";
    readonly string logPath;

    IntPtr hwnd;
    IntPtr popup;
    IntPtr icon;
    bool loading;
    bool popupVisible;
    uint timerId = 1;
    DateTime lastHover = DateTime.MinValue;
    DateTime popupShownAt = DateTime.MinValue;
    string statusText = "در حال دریافت اطلاعات...";
    string lastError = "";

    public TrayApp()
    {
        logPath = Path.Combine(Path.GetTempPath(), "TGJU-desktop.log");
        try { File.WriteAllText(logPath, "", Encoding.UTF8); } catch { }
        Log("=== TGJU Desktop started ===");
        Log($"Log file: {logPath}");

        http = CreateHttpClient();
        wndProc = MainWndProc;
        popupProc = PopupWndProc;

        var hInst = GetModuleHandle(null);
        Register(className, wndProc, hInst);
        Register(popupClass, popupProc, hInst);

        hwnd = CreateWindowEx(WS_EX_TOOLWINDOW, className, "TGJU", 0,
            0, 0, 0, 0, IntPtr.Zero, IntPtr.Zero, hInst, IntPtr.Zero);
        Log($"Main hwnd={hwnd}");

        popup = CreateWindowEx(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
            popupClass, "TGJU", WS_POPUP | WS_BORDER,
            0, 0, 455, 400, IntPtr.Zero, IntPtr.Zero, hInst, IntPtr.Zero);
        Log($"Popup hwnd={popup}");

        icon = LoadIcon(IntPtr.Zero, new IntPtr(32512));
        AddTrayIcon();
        SetTimer(hwnd, timerId, 300000, IntPtr.Zero);
        _ = LoadData();
    }

    public void Run()
    {
        MSG msg;
        while (GetMessage(out msg, IntPtr.Zero, 0, 0) > 0)
        {
            TranslateMessage(ref msg);
            DispatchMessage(ref msg);
        }
    }

    void Log(string line)
    {
        try
        {
            File.AppendAllText(logPath,
                $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}] {line}{Environment.NewLine}",
                Encoding.UTF8);
        }
        catch { }
    }

    void NotifyUi()
    {
        // marshal back to UI thread
        if (hwnd != IntPtr.Zero)
            PostMessage(hwnd, WM_DATA_READY, IntPtr.Zero, IntPtr.Zero);
    }

    async Task LoadData()
    {
        if (loading) return;
        loading = true;
        statusText = "در حال دریافت اطلاعات...";
        NotifyUi();

        try
        {
            var api = "https://api.tgju.org/v1/widget/tmp?keys=" + string.Join(",", slugs);
            Log($"GET {api}");

            using var request = new HttpRequestMessage(HttpMethod.Get, api)
            {
                Version = HttpVersion.Version11,
                VersionPolicy = HttpVersionPolicy.RequestVersionExact
            };
            using var response = await http.SendAsync(request).ConfigureAwait(false);
            var body = await response.Content.ReadAsStringAsync().ConfigureAwait(false);
            Log($"HTTP {(int)response.StatusCode} len={body.Length}");
            if (body.Length > 0)
                Log("Body head: " + body[..Math.Min(body.Length, 300)]);

            if (!response.IsSuccessStatusCode)
                throw new Exception($"HTTP {(int)response.StatusCode}");

            var opts = new JsonSerializerOptions { PropertyNameCaseInsensitive = true };
            var data = JsonSerializer.Deserialize<ApiResponse>(body, opts);
            var indicators = data?.Response?.Indicators ?? new List<Quote>();
            Log($"Parsed indicators={indicators.Count}");

            lock (dataLock)
            {
                lastGood.Clear();
                foreach (var item in indicators)
                {
                    var key = !string.IsNullOrWhiteSpace(item.Name) ? item.Name
                        : !string.IsNullOrWhiteSpace(item.Slug) ? item.Slug
                        : null;
                    if (key == null) continue;
                    lastGood[key] = item;
                    // also index by slug when different
                    if (!string.IsNullOrWhiteSpace(item.Slug) && item.Slug != key)
                        lastGood[item.Slug] = item;
                    Log($"  key={key} p={item.P} dp={item.Dp}");
                }
            }

            lastError = "";
            int count;
            lock (dataLock) count = lastGood.Count;
            statusText = count > 0
                ? $"آخرین به‌روزرسانی: {DateTime.Now:HH:mm:ss}"
                : "داده‌ای دریافت نشد";
        }
        catch (Exception ex)
        {
            lastError = ex.Message;
            statusText = "خطا در دریافت داده";
            Log($"ERROR: {ex.GetType().Name}: {ex.Message}");
            if (ex.InnerException != null)
                Log($"  Inner: {ex.InnerException.Message}");
        }
        finally
        {
            loading = false;
            NotifyUi();
        }
    }

    void AddTrayIcon()
    {
        var n = new NOTIFYICONDATA
        {
            cbSize = Marshal.SizeOf<NOTIFYICONDATA>(),
            hWnd = hwnd,
            uID = 1,
            uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP,
            uCallbackMessage = WM_TRAY,
            hIcon = icon,
            szTip = "شاخص‌های TGJU"
        };
        var added = Shell_NotifyIcon(NIM_ADD, ref n);
        Log($"Shell_NotifyIcon(NIM_ADD) => {added}");
        if (added)
        {
            n.uVersion = NOTIFYICON_VERSION_4;
            var ver = Shell_NotifyIcon(NIM_SETVERSION, ref n);
            Log($"Shell_NotifyIcon(NIM_SETVERSION) => {ver}");
        }
        else
        {
            Log("FAILED to add tray icon");
        }
    }

    void RemoveTrayIcon()
    {
        var n = new NOTIFYICONDATA { cbSize = Marshal.SizeOf<NOTIFYICONDATA>(), hWnd = hwnd, uID = 1 };
        Shell_NotifyIcon(NIM_DELETE, ref n);
        Log("Tray icon removed");
    }

    void ShowPopup()
    {
        GetCursorPos(out var pt);
        var screen = MonitorFromPoint(pt, 2);
        var mi = new MONITORINFO { cbSize = Marshal.SizeOf<MONITORINFO>() };
        GetMonitorInfo(screen, ref mi);

        const int w = 455, h = 400;
        int x = Math.Min(pt.X - w + 16, mi.rcWork.Right - w - 8);
        int y = pt.Y - h - 12;
        if (y < mi.rcWork.Top + 8) y = pt.Y + 24;
        if (x < mi.rcWork.Left + 8) x = mi.rcWork.Left + 8;

        int count;
        lock (dataLock) count = lastGood.Count;
        Log($"ShowPopup at ({x},{y}) cursor=({pt.X},{pt.Y}) items={count}");

        SetWindowPos(popup, new IntPtr(HWND_TOPMOST), x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ShowWindow(popup, SW_SHOWNOACTIVATE);
        popupVisible = true;
        popupShownAt = DateTime.Now;
        InvalidateRect(popup, IntPtr.Zero, true);
        UpdateWindow(popup);
    }

    void HidePopup()
    {
        // prevent instant hide right after show (mouse still on tray)
        if ((DateTime.Now - popupShownAt).TotalMilliseconds < 500)
            return;
        if (!popupVisible) return;
        ShowWindow(popup, SW_HIDE);
        popupVisible = false;
        Log("HidePopup");
    }

    void ShowContextMenu()
    {
        GetCursorPos(out var pt);
        var menu = CreatePopupMenu();
        AppendMenuW(menu, 0, (UIntPtr)IDM_REFRESH, "به‌روزرسانی");
        AppendMenuW(menu, 0, (UIntPtr)IDM_EXIT, "خروج");

        SetForegroundWindow(hwnd);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_RIGHTALIGN,
            pt.X, pt.Y, 0, hwnd, IntPtr.Zero);
        PostMessage(hwnd, 0, IntPtr.Zero, IntPtr.Zero);
        DestroyMenu(menu);
        Log("Context menu shown");
    }

    void HandleTrayActivate()
    {
        lastHover = DateTime.Now;
        ShowPopup();
        _ = LoadData();
    }

    IntPtr MainWndProc(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam)
    {
        if (msg == WM_TRAY)
        {
            int ev = unchecked((int)(long)lParam) & 0xFFFF;
            Log($"WM_TRAY event=0x{ev:X4}");

            // Version-4 selection messages + classic mouse messages
            if (ev == (int)NIN_SELECT || ev == (int)NIN_KEYSELECT
                || ev == (int)WM_LBUTTONUP || ev == (int)WM_LBUTTONDBLCLK)
            {
                HandleTrayActivate();
            }
            else if (ev == (int)WM_MOUSEMOVE)
            {
                if ((DateTime.Now - lastHover).TotalMilliseconds >= 500)
                    HandleTrayActivate();
            }
            else if (ev == (int)WM_RBUTTONUP || ev == (int)WM_CONTEXTMENU)
            {
                HidePopup();
                ShowContextMenu();
            }
            return IntPtr.Zero;
        }

        if (msg == WM_DATA_READY)
        {
            if (popupVisible)
            {
                InvalidateRect(popup, IntPtr.Zero, true);
                UpdateWindow(popup);
            }
            return IntPtr.Zero;
        }

        if (msg == WM_COMMAND)
        {
            int id = unchecked((int)(long)wParam) & 0xFFFF;
            if (id == IDM_REFRESH)
            {
                Log("Menu: Refresh");
                ShowPopup();
                _ = LoadData();
            }
            else if (id == IDM_EXIT)
            {
                Log("Menu: Exit");
                DestroyWindow(hwnd);
            }
            return IntPtr.Zero;
        }

        if (msg == 0x0113 && wParam == (IntPtr)timerId) // WM_TIMER
        {
            Log("Timer tick");
            _ = LoadData();
            return IntPtr.Zero;
        }

        if (msg == WM_DESTROY)
        {
            KillTimer(hWnd, timerId);
            RemoveTrayIcon();
            PostQuitMessage(0);
            return IntPtr.Zero;
        }

        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    IntPtr PopupWndProc(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam)
    {
        if (msg == WM_PAINT)
        {
            PaintPopup(hWnd);
            return IntPtr.Zero;
        }
        if (msg == WM_MOUSEMOVE)
        {
            var tme = new TRACKMOUSEEVENT
            {
                cbSize = Marshal.SizeOf<TRACKMOUSEEVENT>(),
                dwFlags = 0x00000002, // TME_LEAVE
                hwndTrack = hWnd
            };
            TrackMouseEvent(ref tme);
            return IntPtr.Zero;
        }
        if (msg == WM_MOUSELEAVE)
        {
            HidePopup();
            return IntPtr.Zero;
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    void PaintPopup(IntPtr hWnd)
    {
        PAINTSTRUCT ps;
        var dc = BeginPaint(hWnd, out ps);
        GetClientRect(hWnd, out var rc);

        var bg = CreateSolidBrush(0x00FFFFFF);
        FillRect(dc, ref rc, bg);
        DeleteObject(bg);

        var font = CreateFontW(22, 0, 0, 0, 700, 0, 0, 0, 178, 0, 0, 0, 0, "Segoe UI");
        var small = CreateFontW(18, 0, 0, 0, 400, 0, 0, 0, 178, 0, 0, 0, 0, "Segoe UI");
        var old = SelectObject(dc, font);

        DrawTextRtl(dc, "شاخص", 12, 8, 100, 38, 0x00555555, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DrawTextRtl(dc, "قیمت", 112, 8, 270, 38, 0x00555555, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DrawTextRtl(dc, "تغییر / زمان", 282, 8, 443, 38, 0x00555555, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        Dictionary<string, Quote> snapshot;
        lock (dataLock)
            snapshot = new Dictionary<string, Quote>(lastGood);

        bool hasAny = false;
        for (int i = 0; i < slugs.Length; i++)
        {
            int y = 48 + i * 47;
            DrawTextRtl(dc, names[i], 12, y, 112, y + 42, 0x00111111, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            if (!snapshot.TryGetValue(slugs[i], out var q))
            {
                SelectObject(dc, small);
                DrawTextRtl(dc, "—", 112, y, 443, y + 42, 0x00999999, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                continue;
            }

            hasAny = true;
            double.TryParse(q.P, System.Globalization.NumberStyles.Any,
                System.Globalization.CultureInfo.InvariantCulture, out var p);
            var shown = slugs[i] is "ons" or "oil_brent" ? p : p / 10.0;
            var price = slugs[i] is "ons" or "oil_brent" ? shown.ToString("N2") : shown.ToString("N0");

            var color = q.Dp > 0 ? 0x00228B22 : q.Dp < 0 ? 0x002323B0 : 0x00008C8C;
            SelectObject(dc, font);
            DrawTextRtl(dc, price, 112, y, 282, y + 42, color, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            SelectObject(dc, small);
            var symbol = q.Dp > 0 ? "▲ " : q.Dp < 0 ? "▼ " : "● ";
            var text = symbol + Math.Abs(q.Dp).ToString("0.00") + "%  " + (string.IsNullOrWhiteSpace(q.T) ? "—" : q.T);
            DrawTextRtl(dc, text, 282, y, 443, y + 42, color, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        SelectObject(dc, small);
        var statusColor = string.IsNullOrEmpty(lastError) ? 0x00666666 : 0x000000CC;
        var bar = statusText;
        if (!string.IsNullOrEmpty(lastError))
            bar = statusText + " — " + lastError;
        DrawTextRtl(dc, bar, 12, 370, 443, 395, statusColor, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        if (!hasAny)
        {
            SelectObject(dc, font);
            DrawTextRtl(dc, statusText, 12, 160, 443, 220, 0x00555555, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (!string.IsNullOrEmpty(lastError))
            {
                SelectObject(dc, small);
                DrawTextRtl(dc, lastError, 12, 220, 443, 270, 0x000000CC, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }

        SelectObject(dc, old);
        DeleteObject(font);
        DeleteObject(small);
        EndPaint(hWnd, ref ps);
    }

    static void DrawTextRtl(IntPtr dc, string text, int l, int t, int r, int b, int color, uint flags)
    {
        SetTextColor(dc, color);
        SetBkMode(dc, (int)TRANSPARENT);
        var rect = new RECT { Left = l, Top = t, Right = r, Bottom = b };
        DrawTextW(dc, text, text.Length, ref rect, flags | DT_RTLREADING | DT_NOPREFIX);
    }

    public void Dispose()
    {
        RemoveTrayIcon();
        if (popup != IntPtr.Zero) DestroyWindow(popup);
        if (hwnd != IntPtr.Zero) DestroyWindow(hwnd);
        http.Dispose();
        Log("=== TGJU Desktop exited ===");
    }

    static HttpClient CreateHttpClient()
    {
        var handler = new HttpClientHandler
        {
            UseProxy = true,
            Proxy = null,
            SslProtocols = SslProtocols.Tls12 | SslProtocols.Tls13,
            AutomaticDecompression = DecompressionMethods.All
        };
        var c = new HttpClient(handler) { Timeout = TimeSpan.FromSeconds(25) };
        c.DefaultRequestVersion = HttpVersion.Version11;
        c.DefaultVersionPolicy = HttpVersionPolicy.RequestVersionExact;
        c.DefaultRequestHeaders.UserAgent.ParseAdd("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36");
        c.DefaultRequestHeaders.Accept.ParseAdd("application/json,text/plain,*/*");
        c.DefaultRequestHeaders.TryAddWithoutValidation("Accept-Language", "fa-IR,fa;q=0.9,en;q=0.8");
        return c;
    }

    static void Register(string name, WndProcDelegate proc, IntPtr hInst)
    {
        var wc = new WNDCLASS
        {
            lpfnWndProc = proc,
            hInstance = hInst,
            lpszClassName = name,
            hCursor = LoadCursor(IntPtr.Zero, 32512),
            hbrBackground = IntPtr.Zero
        };
        RegisterClass(ref wc);
    }

    delegate IntPtr WndProcDelegate(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern ushort RegisterClass(ref WNDCLASS lpWndClass);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern IntPtr CreateWindowEx(int exStyle, string className, string windowName, int style, int x, int y, int w, int h, IntPtr parent, IntPtr menu, IntPtr instance, IntPtr param);
    [DllImport("user32.dll")] static extern IntPtr DefWindowProc(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] static extern bool DestroyWindow(IntPtr hWnd);
    [DllImport("user32.dll")] static extern void PostQuitMessage(int code);
    [DllImport("user32.dll")] static extern int GetMessage(out MSG msg, IntPtr hWnd, uint min, uint max);
    [DllImport("user32.dll")] static extern bool TranslateMessage(ref MSG msg);
    [DllImport("user32.dll")] static extern IntPtr DispatchMessage(ref MSG msg);
    [DllImport("user32.dll")] static extern bool GetCursorPos(out POINT p);
    [DllImport("user32.dll")] static extern IntPtr MonitorFromPoint(POINT p, uint flags);
    [DllImport("user32.dll")] static extern bool GetMonitorInfo(IntPtr hMonitor, ref MONITORINFO info);
    [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr hWnd, IntPtr insertAfter, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr hWnd, uint cmd);
    [DllImport("user32.dll")] static extern bool UpdateWindow(IntPtr hWnd);
    [DllImport("user32.dll")] static extern bool InvalidateRect(IntPtr hWnd, IntPtr rect, bool erase);
    [DllImport("user32.dll")] static extern IntPtr LoadIcon(IntPtr hInstance, IntPtr iconName);
    [DllImport("user32.dll")] static extern IntPtr LoadCursor(IntPtr hInstance, int cursor);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern IntPtr GetModuleHandle(string? name);
    [DllImport("user32.dll")] static extern bool SetTimer(IntPtr hWnd, uint id, uint ms, IntPtr callback);
    [DllImport("user32.dll")] static extern bool KillTimer(IntPtr hWnd, uint id);
    [DllImport("user32.dll")] static extern bool TrackMouseEvent(ref TRACKMOUSEEVENT tme);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int DrawTextW(IntPtr hdc, string text, int len, ref RECT rect, uint format);
    [DllImport("user32.dll")] static extern IntPtr BeginPaint(IntPtr hWnd, out PAINTSTRUCT ps);
    [DllImport("user32.dll")] static extern bool EndPaint(IntPtr hWnd, ref PAINTSTRUCT ps);
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] static extern int SetTextColor(IntPtr hdc, int color);
    [DllImport("user32.dll")] static extern int SetBkMode(IntPtr hdc, int mode);
    [DllImport("user32.dll")] static extern bool FillRect(IntPtr hdc, ref RECT rect, IntPtr brush);
    [DllImport("user32.dll")] static extern IntPtr CreateSolidBrush(int color);
    [DllImport("user32.dll")] static extern bool DeleteObject(IntPtr obj);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr CreateFontW(int h, int w, int e, int o, int weight, uint italic, uint underline, uint strike, uint charset, uint outPrecision, uint clipPrecision, uint quality, uint pitchAndFamily, string face);
    [DllImport("gdi32.dll")] static extern IntPtr SelectObject(IntPtr hdc, IntPtr obj);
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)] static extern bool Shell_NotifyIcon(uint message, ref NOTIFYICONDATA data);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr CreatePopupMenu();
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern bool AppendMenuW(IntPtr hMenu, uint uFlags, UIntPtr uIDNewItem, string lpNewItem);
    [DllImport("user32.dll")] static extern bool DestroyMenu(IntPtr hMenu);
    [DllImport("user32.dll")] static extern bool TrackPopupMenu(IntPtr hMenu, uint uFlags, int x, int y, int nReserved, IntPtr hWnd, IntPtr prcRect);
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct WNDCLASS { public uint style; public WndProcDelegate lpfnWndProc; public int cbClsExtra, cbWndExtra; public IntPtr hInstance, hIcon, hCursor, hbrBackground; public string lpszMenuName; public string lpszClassName; }
    [StructLayout(LayoutKind.Sequential)] struct MSG { public IntPtr hWnd; public uint message; public IntPtr wParam, lParam; public uint time; public POINT pt; }
    [StructLayout(LayoutKind.Sequential)] struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] struct MONITORINFO { public int cbSize; public RECT rcMonitor, rcWork; public uint dwFlags; }
    [StructLayout(LayoutKind.Sequential)] struct PAINTSTRUCT { public IntPtr hdc; public bool fErase; public RECT rcPaint; public bool fRestore; public bool fIncUpdate; [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)] public byte[] rgbReserved; }
    [StructLayout(LayoutKind.Sequential)] struct TRACKMOUSEEVENT { public int cbSize; public uint dwFlags; public IntPtr hwndTrack; public uint dwHoverTime; }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct NOTIFYICONDATA
    {
        public int cbSize; public IntPtr hWnd; public uint uID, uFlags, uCallbackMessage; public IntPtr hIcon;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string szTip;
        public uint dwState, dwStateMask;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string szInfo;
        public uint uVersion;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string szInfoTitle;
        public uint dwInfoFlags;
        public Guid guidItem; public IntPtr hBalloonIcon;
    }

    public sealed class ApiResponse
    {
        [JsonPropertyName("response")] public ApiBody? Response { get; set; }
    }
    public sealed class ApiBody
    {
        [JsonPropertyName("indicators")] public List<Quote>? Indicators { get; set; }
    }
    public sealed class Quote
    {
        [JsonPropertyName("name")] public string Name { get; set; } = "";
        [JsonPropertyName("slug")] public string Slug { get; set; } = "";
        [JsonPropertyName("p")] public string P { get; set; } = "";
        [JsonPropertyName("dp")] public double Dp { get; set; }
        [JsonPropertyName("t")] public string T { get; set; } = "";
    }
}
