using System.Net;
using System.Net.Security;
using System.Runtime.InteropServices;
using System.Security.Authentication;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace TgjuDesktop;

internal static class Program
{
    [STAThread]
    static void Main()
    {
        try { ServicePointManager.SecurityProtocol = SecurityProtocolType.Tls12; } catch { }

        AppDomain.CurrentDomain.UnhandledException += (_, e) =>
        {
            try
            {
                var path = Path.Combine(Path.GetTempPath(), "TGJU-desktop.log");
                File.AppendAllText(path,
                    $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}] FATAL: {e.ExceptionObject}{Environment.NewLine}",
                    Encoding.UTF8);
            }
            catch { }
        };

        try
        {
            using var app = new TrayApp();
            app.Run();
        }
        catch (Exception ex)
        {
            try
            {
                var path = Path.Combine(Path.GetTempPath(), "TGJU-desktop.log");
                File.AppendAllText(path,
                    $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}] MAIN EX: {ex}{Environment.NewLine}",
                    Encoding.UTF8);
            }
            catch { }
        }
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
    const uint WM_ERASEBKGND = 0x0014;
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
    DateTime lastFetch = DateTime.MinValue;
    string statusText = "در حال دریافت اطلاعات...";
    string lastError = "";

    public TrayApp()
    {
        logPath = Path.Combine(Path.GetTempPath(), "TGJU-desktop.log");
        try { File.WriteAllText(logPath, "", Encoding.UTF8); } catch { }
        Log("=== TGJU Desktop started ===");
        Log($"Log file: {logPath}");
        Log($"OS: {Environment.OSVersion} 64bit={Environment.Is64BitProcess}");

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
            popupClass, "TGJU Popup", WS_POPUP | WS_BORDER,
            0, 0, 460, 410, IntPtr.Zero, IntPtr.Zero, hInst, IntPtr.Zero);
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
        if (hwnd != IntPtr.Zero)
            PostMessage(hwnd, WM_DATA_READY, IntPtr.Zero, IntPtr.Zero);
    }

    static string ApiUrl(string[] slugs) =>
        "https://api.tgju.org/v1/widget/tmp?keys=" + string.Join(",", slugs);

    static HttpClient CreateClient(bool bypassCert, bool tls12Only)
    {
        var ssl = tls12Only ? SslProtocols.Tls12 : (SslProtocols.Tls12 | SslProtocols.Tls13);
        var sockets = new SocketsHttpHandler
        {
            AutomaticDecompression = DecompressionMethods.All,
            ConnectTimeout = TimeSpan.FromSeconds(15),
            SslOptions = new SslClientAuthenticationOptions
            {
                EnabledSslProtocols = ssl,
                RemoteCertificateValidationCallback = bypassCert
                    ? static (_, _, _, _) => true
                    : null
            }
        };
        var c = new HttpClient(sockets) { Timeout = TimeSpan.FromSeconds(25) };
        c.DefaultRequestVersion = HttpVersion.Version11;
        c.DefaultVersionPolicy = HttpVersionPolicy.RequestVersionExact;
        c.DefaultRequestHeaders.UserAgent.ParseAdd(
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36");
        c.DefaultRequestHeaders.Accept.ParseAdd("application/json,text/plain,*/*");
        c.DefaultRequestHeaders.TryAddWithoutValidation("Accept-Language", "fa-IR,fa;q=0.9,en;q=0.8");
        return c;
    }

    async Task<(bool ok, string body, string error)> TryFetchHttp(bool bypassCert, bool tls12Only)
    {
        var api = ApiUrl(slugs);
        Log($"GET HttpClient (bypass={bypassCert}, tls12Only={tls12Only})");
        try
        {
            using var client = CreateClient(bypassCert, tls12Only);
            using var response = await client.GetAsync(api).ConfigureAwait(false);
            var body = await response.Content.ReadAsStringAsync().ConfigureAwait(false);
            Log($"HTTP {(int)response.StatusCode} len={body.Length}");
            if (body.Length > 0)
                Log("Body head: " + body[..Math.Min(body.Length, 200)]);
            if (!response.IsSuccessStatusCode)
                return (false, "", $"HTTP {(int)response.StatusCode}");
            return (true, body, "");
        }
        catch (Exception ex)
        {
            var msg = ex.Message;
            for (var e = ex.InnerException; e != null; e = e.InnerException)
                msg += " | " + e.Message;
            Log($"HttpClient error: {msg}");
            return (false, "", msg);
        }
    }

    async Task LoadData()
    {
        if (loading) return;
        if ((DateTime.Now - lastFetch).TotalSeconds < 3 && lastGood.Count > 0) return;

        loading = true;
        statusText = "در حال دریافت اطلاعات...";
        NotifyUi();

        try
        {
            var result = await TryFetchHttp(bypassCert: true, tls12Only: true).ConfigureAwait(false);
            if (!result.ok)
                result = await TryFetchHttp(bypassCert: false, tls12Only: false).ConfigureAwait(false);

            if (!result.ok)
                throw new Exception(result.error);

            var opts = new JsonSerializerOptions { PropertyNameCaseInsensitive = true };
            var data = JsonSerializer.Deserialize<ApiResponse>(result.body, opts);
            var indicators = data?.Response?.Indicators ?? new List<Quote>();
            Log($"Parsed indicators={indicators.Count}");

            lock (dataLock)
            {
                lastGood.Clear();
                foreach (var item in indicators)
                {
                    if (!string.IsNullOrWhiteSpace(item.Slug))
                        lastGood[item.Slug] = item;
                    if (!string.IsNullOrWhiteSpace(item.Name))
                        lastGood[item.Name] = item;
                    Log($"  slug={item.Slug} name={item.Name} p={item.P} dp={item.Dp}");
                }
            }

            lastError = "";
            lastFetch = DateTime.Now;
            int count;
            lock (dataLock) count = lastGood.Count;
            statusText = count > 0
                ? $"به‌روزرسانی: {DateTime.Now:HH:mm:ss}"
                : "داده‌ای دریافت نشد";
        }
        catch (Exception ex)
        {
            lastError = ex.Message.Length > 60 ? ex.Message[..60] + "…" : ex.Message;
            statusText = "خطا در دریافت داده";
            Log($"ERROR final: {ex.Message}");
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
            Shell_NotifyIcon(NIM_SETVERSION, ref n);
        }
    }

    void RemoveTrayIcon()
    {
        var n = new NOTIFYICONDATA { cbSize = Marshal.SizeOf<NOTIFYICONDATA>(), hWnd = hwnd, uID = 1 };
        Shell_NotifyIcon(NIM_DELETE, ref n);
    }

    void ShowPopup()
    {
        try
        {
            GetCursorPos(out var pt);
            var screen = MonitorFromPoint(pt, 2);
            var mi = new MONITORINFO { cbSize = Marshal.SizeOf<MONITORINFO>() };
            GetMonitorInfo(screen, ref mi);

            const int w = 460, h = 410;
            int x = Math.Min(pt.X - w + 16, mi.rcWork.Right - w - 8);
            int y = pt.Y - h - 12;
            if (y < mi.rcWork.Top + 8) y = pt.Y + 24;
            if (x < mi.rcWork.Left + 8) x = mi.rcWork.Left + 8;

            int count;
            lock (dataLock) count = lastGood.Count;
            Log($"ShowPopup at ({x},{y}) items={count} visible={popupVisible}");

            SetWindowPos(popup, new IntPtr(HWND_TOPMOST), x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
            ShowWindow(popup, SW_SHOWNOACTIVATE);
            popupVisible = true;
            popupShownAt = DateTime.Now;
            InvalidateRect(popup, IntPtr.Zero, true);
            UpdateWindow(popup);
        }
        catch (Exception ex)
        {
            Log("ShowPopup EX: " + ex.Message);
        }
    }

    void HidePopup()
    {
        if ((DateTime.Now - popupShownAt).TotalMilliseconds < 800) return;
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
    }

    void HandleTrayActivate()
    {
        lastHover = DateTime.Now;
        ShowPopup();
        _ = LoadData();
    }

    IntPtr MainWndProc(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam)
    {
        try
        {
            if (msg == WM_TRAY)
            {
                int ev = unchecked((int)(long)lParam) & 0xFFFF;
                if (ev == (int)NIN_SELECT || ev == (int)NIN_KEYSELECT
                    || ev == (int)WM_LBUTTONUP || ev == (int)WM_LBUTTONDBLCLK)
                    HandleTrayActivate();
                else if (ev == (int)WM_MOUSEMOVE)
                {
                    if ((DateTime.Now - lastHover).TotalMilliseconds >= 600)
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
                    lastFetch = DateTime.MinValue;
                    ShowPopup();
                    _ = LoadData();
                }
                else if (id == IDM_EXIT)
                {
                    Log("Exit requested");
                    DestroyWindow(hwnd);
                }
                return IntPtr.Zero;
            }

            if (msg == 0x0113 && wParam == (IntPtr)timerId)
            {
                lastFetch = DateTime.MinValue;
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
        catch (Exception ex)
        {
            Log("MainWndProc EX: " + ex);
            return IntPtr.Zero;
        }
    }

    IntPtr PopupWndProc(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam)
    {
        try
        {
            if (msg == WM_PAINT)
            {
                PaintPopup(hWnd);
                return IntPtr.Zero;
            }
            if (msg == WM_ERASEBKGND)
                return (IntPtr)1;
            if (msg == WM_MOUSEMOVE)
            {
                var tme = new TRACKMOUSEEVENT
                {
                    cbSize = Marshal.SizeOf<TRACKMOUSEEVENT>(),
                    dwFlags = 0x00000002,
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
        catch (Exception ex)
        {
            Log("PopupWndProc EX: " + ex);
            return IntPtr.Zero;
        }
    }

    void PaintPopup(IntPtr hWnd)
    {
        PAINTSTRUCT ps = default;
        var dc = BeginPaint(hWnd, out ps);
        try
        {
            GetClientRect(hWnd, out var rc);
            Log($"PaintPopup client={rc.Right - rc.Left}x{rc.Bottom - rc.Top}");

            var bg = CreateSolidBrush(0x00FFFFFF);
            FillRect(dc, ref rc, bg);
            DeleteObject(bg);

            var font = CreateFontW(20, 0, 0, 0, 700, 0, 0, 0, 1, 0, 0, 0, 0, "Segoe UI");
            var small = CreateFontW(16, 0, 0, 0, 400, 0, 0, 0, 1, 0, 0, 0, 0, "Segoe UI");
            if (font == IntPtr.Zero) font = GetStockObject(17);
            if (small == IntPtr.Zero) small = font;
            var old = SelectObject(dc, font);
            SetBkMode(dc, (int)TRANSPARENT);

            DrawTextRtl(dc, "شاخص", 8, 6, 100, 32, 0x00555555, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            DrawTextRtl(dc, "قیمت", 100, 6, 280, 32, 0x00555555, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            DrawTextRtl(dc, "تغییر / زمان", 280, 6, 450, 32, 0x00555555, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            Dictionary<string, Quote> snapshot;
            lock (dataLock)
                snapshot = new Dictionary<string, Quote>(lastGood, StringComparer.OrdinalIgnoreCase);

            bool hasAny = false;
            for (int i = 0; i < slugs.Length; i++)
            {
                int y = 40 + i * 48;
                DrawTextRtl(dc, names[i], 8, y, 100, y + 40, 0x00111111, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                if (!snapshot.TryGetValue(slugs[i], out var q) || q == null)
                {
                    SelectObject(dc, small);
                    DrawTextRtl(dc, "—", 100, y, 450, y + 40, 0x00999999, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    SelectObject(dc, font);
                    continue;
                }

                hasAny = true;
                var pStr = (q.P ?? "").Replace(",", "");
                double.TryParse(pStr, System.Globalization.NumberStyles.Any,
                    System.Globalization.CultureInfo.InvariantCulture, out var p);

                string price = slugs[i] is "ons" or "oil_brent"
                    ? p.ToString("N2")
                    : (p / 10.0).ToString("N0");

                var color = q.Dp > 0 ? 0x00228B22 : q.Dp < 0 ? 0x002323B0 : 0x00008C8C;
                SelectObject(dc, font);
                DrawTextRtl(dc, price, 100, y, 280, y + 40, color, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                SelectObject(dc, small);
                var symbol = q.Dp > 0 ? "+" : q.Dp < 0 ? "-" : "=";
                var t = string.IsNullOrWhiteSpace(q.T) ? "-" : q.T;
                var text = symbol + Math.Abs(q.Dp).ToString("0.00") + "%  " + t;
                DrawTextRtl(dc, text, 280, y, 450, y + 40, color, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(dc, font);
            }

            SelectObject(dc, small);
            var statusColor = string.IsNullOrEmpty(lastError) ? 0x00666666 : 0x000000CC;
            var bar = string.IsNullOrEmpty(lastError) ? statusText : statusText + " | " + lastError;
            DrawTextRtl(dc, bar, 8, 380, 450, 405, statusColor, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            if (!hasAny)
            {
                SelectObject(dc, font);
                DrawTextRtl(dc, statusText, 8, 160, 450, 220, 0x00555555, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }

            Log($"PaintPopup done hasAny={hasAny} status={statusText}");

            SelectObject(dc, old);
            if (font != IntPtr.Zero) DeleteObject(font);
            if (small != IntPtr.Zero && small != font) DeleteObject(small);
        }
        catch (Exception ex)
        {
            Log("PaintPopup EX: " + ex);
        }
        finally
        {
            EndPaint(hWnd, ref ps);
        }
    }

    static void DrawTextRtl(IntPtr dc, string text, int l, int t, int r, int b, int color, uint flags)
    {
        if (string.IsNullOrEmpty(text)) text = " ";
        SetTextColor(dc, color);
        SetBkMode(dc, (int)TRANSPARENT);
        var rect = new RECT { Left = l, Top = t, Right = r, Bottom = b };
        DrawTextW(dc, text, -1, ref rect, flags | DT_RTLREADING | DT_NOPREFIX);
    }

    public void Dispose()
    {
        try
        {
            RemoveTrayIcon();
            if (popup != IntPtr.Zero) DestroyWindow(popup);
            if (hwnd != IntPtr.Zero) DestroyWindow(hwnd);
        }
        catch { }
        Log("=== TGJU Desktop exited ===");
    }

    static void Register(string name, WndProcDelegate proc, IntPtr hInst)
    {
        var wc = new WNDCLASS
        {
            style = 0x0003,
            lpfnWndProc = proc,
            hInstance = hInst,
            lpszClassName = name,
            hCursor = LoadCursor(IntPtr.Zero, 32512),
            hbrBackground = IntPtr.Zero
        };
        RegisterClass(ref wc);
    }

    delegate IntPtr WndProcDelegate(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern ushort RegisterClass(ref WNDCLASS lpWndClass);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr CreateWindowEx(int exStyle, string className, string windowName, int style, int x, int y, int w, int h, IntPtr parent, IntPtr menu, IntPtr instance, IntPtr param);
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
    [DllImport("gdi32.dll")] static extern bool DeleteObject(IntPtr obj);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr CreateFontW(int h, int w, int e, int o, int weight, uint italic, uint underline, uint strike, uint charset, uint outPrecision, uint clipPrecision, uint quality, uint pitchAndFamily, string face);
    [DllImport("gdi32.dll")] static extern IntPtr SelectObject(IntPtr hdc, IntPtr obj);
    [DllImport("gdi32.dll")] static extern IntPtr GetStockObject(int fnObject);
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)] static extern bool Shell_NotifyIcon(uint message, ref NOTIFYICONDATA data);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr CreatePopupMenu();
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern bool AppendMenuW(IntPtr hMenu, uint uFlags, UIntPtr uIDNewItem, string lpNewItem);
    [DllImport("user32.dll")] static extern bool DestroyMenu(IntPtr hMenu);
    [DllImport("user32.dll")] static extern bool TrackPopupMenu(IntPtr hMenu, uint uFlags, int x, int y, int nReserved, IntPtr hWnd, IntPtr prcRect);
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct WNDCLASS
    {
        public uint style;
        public WndProcDelegate lpfnWndProc;
        public int cbClsExtra, cbWndExtra;
        public IntPtr hInstance, hIcon, hCursor, hbrBackground;
        public string lpszMenuName;
        public string lpszClassName;
    }
    [StructLayout(LayoutKind.Sequential)] struct MSG { public IntPtr hWnd; public uint message; public IntPtr wParam, lParam; public uint time; public POINT pt; }
    [StructLayout(LayoutKind.Sequential)] struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] struct MONITORINFO { public int cbSize; public RECT rcMonitor, rcWork; public uint dwFlags; }
    [StructLayout(LayoutKind.Sequential)]
    struct PAINTSTRUCT
    {
        public IntPtr hdc;
        public bool fErase;
        public RECT rcPaint;
        public bool fRestore;
        public bool fIncUpdate;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)] public byte[] rgbReserved;
    }
    [StructLayout(LayoutKind.Sequential)] struct TRACKMOUSEEVENT { public int cbSize; public uint dwFlags; public IntPtr hwndTrack; public uint dwHoverTime; }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct NOTIFYICONDATA
    {
        public int cbSize;
        public IntPtr hWnd;
        public uint uID, uFlags, uCallbackMessage;
        public IntPtr hIcon;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string szTip;
        public uint dwState, dwStateMask;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string szInfo;
        public uint uVersion;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string szInfoTitle;
        public uint dwInfoFlags;
        public Guid guidItem;
        public IntPtr hBalloonIcon;
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
