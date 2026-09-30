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
                    $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}] FATAL: {e.ExceptionObject}{Environment.NewLine}", Encoding.UTF8);
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
                    $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}] MAIN EX: {ex}{Environment.NewLine}", Encoding.UTF8);
            }
            catch { }
        }
    }
}

internal sealed class TrayApp : IDisposable
{
    // --- messages ---
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

    // --- window styles ---
    const int WS_EX_TOOLWINDOW = 0x00000080;
    const int WS_EX_TOPMOST = 0x00000008;
    const int WS_EX_NOACTIVATE = 0x08000000;
    const int WS_POPUP = unchecked((int)0x80000000);
    const int WS_BORDER = 0x00800000;
    const int WS_VISIBLE = 0x10000000;

    const uint SW_HIDE = 0;
    const uint SW_SHOW = 5;
    const uint SW_SHOWNOACTIVATE = 4;

    // CRITICAL: must be -1 as IntPtr on both 32/64-bit (not 0xFFFFFFFF uint)
    static readonly IntPtr HWND_TOPMOST = new IntPtr(-1);

    const uint SWP_NOACTIVATE = 0x0010;
    const uint SWP_SHOWWINDOW = 0x0040;
    const uint SWP_NOMOVE = 0x0002;
    const uint SWP_NOSIZE = 0x0001;

    const uint NIM_ADD = 0;
    const uint NIM_DELETE = 2;
    const uint NIM_SETVERSION = 4;
    const uint NOTIFYICON_VERSION_4 = 4;
    const uint NIF_MESSAGE = 1;
    const uint NIF_ICON = 2;
    const uint NIF_TIP = 4;
    const uint NIF_SHOWTIP = 0x80;

    const uint TRANSPARENT = 1;
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
    readonly string[] namesFa = { "تتر", "دلار", "گرم18", "کهربا", "عیار", "انس", "نفت" };
    readonly string[] namesEn = { "USDT", "USD", "Gold18", "Kahroba", "Ayar", "ONS", "Brent" };

    readonly object dataLock = new();
    readonly Dictionary<string, Quote> lastGood = new(StringComparer.OrdinalIgnoreCase);
    readonly WndProcDelegate wndProc;
    readonly WndProcDelegate popupProc;
    readonly string className = "TGJUTrayNative2";
    readonly string popupClass = "TGJUPopupNative2";
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
    string statusText = "loading...";
    string lastError = "";
    int paintCount;

    public TrayApp()
    {
        logPath = Path.Combine(Path.GetTempPath(), "TGJU-desktop.log");
        try { File.WriteAllText(logPath, "", Encoding.UTF8); } catch { }

        Log("=== TGJU Desktop started ===");
        Log($"Log: {logPath}");
        Log($"OS={Environment.OSVersion} x64={Environment.Is64BitProcess} CLR={Environment.Version}");
        Log($"HWND_TOPMOST ptr={HWND_TOPMOST.ToInt64()}");

        wndProc = MainWndProc;
        popupProc = PopupWndProc;

        var hInst = GetModuleHandle(null);
        Log($"hInst={hInst}");

        var reg1 = Register(className, wndProc, hInst);
        var reg2 = Register(popupClass, popupProc, hInst);
        Log($"RegisterClass main={reg1} popup={reg2} err={GetLastError()}");

        hwnd = CreateWindowEx(WS_EX_TOOLWINDOW, className, "TGJU", 0,
            0, 0, 0, 0, IntPtr.Zero, IntPtr.Zero, hInst, IntPtr.Zero);
        Log($"Main hwnd={hwnd} err={GetLastError()}");

        // Popup: TOPMOST + TOOLWINDOW. Start hidden.
        popup = CreateWindowEx(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            popupClass, "TGJU Popup",
            WS_POPUP | WS_BORDER,
            100, 100, 480, 420,
            IntPtr.Zero, IntPtr.Zero, hInst, IntPtr.Zero);
        Log($"Popup hwnd={popup} err={GetLastError()}");

        icon = LoadIcon(IntPtr.Zero, new IntPtr(32512));
        Log($"Icon={icon}");

        AddTrayIcon();
        SetTimer(hwnd, timerId, 300000, IntPtr.Zero);
        Log("Timer set, loading data...");
        _ = LoadData();
    }

    public void Run()
    {
        Log("Message loop start");
        MSG msg;
        while (GetMessage(out msg, IntPtr.Zero, 0, 0) > 0)
        {
            TranslateMessage(ref msg);
            DispatchMessage(ref msg);
        }
        Log("Message loop end");
    }

    void Log(string line)
    {
        try
        {
            File.AppendAllText(logPath,
                $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}] {line}{Environment.NewLine}", Encoding.UTF8);
        }
        catch { }
    }

    void NotifyUi()
    {
        if (hwnd != IntPtr.Zero)
            PostMessage(hwnd, WM_DATA_READY, IntPtr.Zero, IntPtr.Zero);
    }

    static string ApiUrl(string[] s) =>
        "https://api.tgju.org/v1/widget/tmp?keys=" + string.Join(",", s);

    static HttpClient CreateClient()
    {
        var sockets = new SocketsHttpHandler
        {
            AutomaticDecompression = DecompressionMethods.All,
            ConnectTimeout = TimeSpan.FromSeconds(15),
            SslOptions = new SslClientAuthenticationOptions
            {
                EnabledSslProtocols = SslProtocols.Tls12,
                RemoteCertificateValidationCallback = static (_, _, _, _) => true
            }
        };
        var c = new HttpClient(sockets) { Timeout = TimeSpan.FromSeconds(25) };
        c.DefaultRequestVersion = HttpVersion.Version11;
        c.DefaultVersionPolicy = HttpVersionPolicy.RequestVersionExact;
        c.DefaultRequestHeaders.UserAgent.ParseAdd("Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/128.0.0.0");
        c.DefaultRequestHeaders.Accept.ParseAdd("application/json");
        return c;
    }

    async Task LoadData()
    {
        if (loading) { Log("LoadData skipped (already loading)"); return; }
        if ((DateTime.Now - lastFetch).TotalSeconds < 3 && lastGood.Count > 0)
        {
            Log("LoadData skipped (fresh cache)");
            return;
        }

        loading = true;
        statusText = "loading...";
        NotifyUi();
        Log("LoadData start");

        try
        {
            var api = ApiUrl(slugs);
            Log("GET " + api);
            using var client = CreateClient();
            using var response = await client.GetAsync(api).ConfigureAwait(false);
            var body = await response.Content.ReadAsStringAsync().ConfigureAwait(false);
            Log($"HTTP {(int)response.StatusCode} len={body.Length}");
            if (body.Length > 0)
                Log("Body: " + body[..Math.Min(body.Length, 180)]);

            if (!response.IsSuccessStatusCode)
                throw new Exception($"HTTP {(int)response.StatusCode}");

            var opts = new JsonSerializerOptions { PropertyNameCaseInsensitive = true };
            var data = JsonSerializer.Deserialize<ApiResponse>(body, opts);
            var indicators = data?.Response?.Indicators ?? new List<Quote>();
            Log($"Parsed count={indicators.Count}");

            lock (dataLock)
            {
                lastGood.Clear();
                foreach (var item in indicators)
                {
                    if (!string.IsNullOrWhiteSpace(item.Slug))
                        lastGood[item.Slug] = item;
                    if (!string.IsNullOrWhiteSpace(item.Name))
                        lastGood[item.Name] = item;
                    Log($"  + {item.Slug}/{item.Name} p={item.P} dp={item.Dp} t={item.T}");
                }
            }

            lastError = "";
            lastFetch = DateTime.Now;
            statusText = $"ok {DateTime.Now:HH:mm:ss}";
            Log($"LoadData ok items={lastGood.Count}");
        }
        catch (Exception ex)
        {
            lastError = ex.Message.Length > 80 ? ex.Message[..80] : ex.Message;
            statusText = "error";
            Log("LoadData ERROR: " + ex);
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
            szTip = "TGJU"
        };
        var ok = Shell_NotifyIcon(NIM_ADD, ref n);
        Log($"NIM_ADD={ok} err={GetLastError()}");
        if (ok)
        {
            n.uVersion = NOTIFYICON_VERSION_4;
            var v = Shell_NotifyIcon(NIM_SETVERSION, ref n);
            Log($"NIM_SETVERSION={v}");
        }
    }

    void RemoveTrayIcon()
    {
        var n = new NOTIFYICONDATA { cbSize = Marshal.SizeOf<NOTIFYICONDATA>(), hWnd = hwnd, uID = 1 };
        Shell_NotifyIcon(NIM_DELETE, ref n);
        Log("Tray removed");
    }

    void ShowPopup()
    {
        try
        {
            GetCursorPos(out var pt);
            Log($"ShowPopup cursor=({pt.X},{pt.Y})");

            // Place above cursor; clamp to primary-ish area
            const int w = 480, h = 420;
            int x = pt.X - w / 2;
            int y = pt.Y - h - 20;
            if (x < 8) x = 8;
            if (y < 8) y = pt.Y + 30;

            int count;
            lock (dataLock) count = lastGood.Count;

            // Use HWND_TOPMOST = -1 (fixed for x64)
            var posOk = SetWindowPos(popup, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW);
            Log($"SetWindowPos ok={posOk} at ({x},{y},{w},{h}) err={GetLastError()}");

            var showOk = ShowWindow(popup, SW_SHOW);
            Log($"ShowWindow(SW_SHOW) prevVisible={showOk}");

            var visible = IsWindowVisible(popup);
            GetWindowRect(popup, out var wr);
            Log($"IsWindowVisible={visible} rect=({wr.Left},{wr.Top})-({wr.Right},{wr.Bottom}) items={count}");

            popupVisible = true;
            popupShownAt = DateTime.Now;

            var inv = InvalidateRect(popup, IntPtr.Zero, true);
            var upd = UpdateWindow(popup);
            Log($"Invalidate={inv} UpdateWindow={upd} paintCount={paintCount}");
        }
        catch (Exception ex)
        {
            Log("ShowPopup EX: " + ex);
        }
    }

    void HidePopup()
    {
        // Keep visible at least 3s so user can see it
        if ((DateTime.Now - popupShownAt).TotalMilliseconds < 3000) return;
        if (!popupVisible) return;
        ShowWindow(popup, SW_HIDE);
        popupVisible = false;
        Log("HidePopup");
    }

    void ShowContextMenu()
    {
        GetCursorPos(out var pt);
        var menu = CreatePopupMenu();
        AppendMenuW(menu, 0, (UIntPtr)IDM_REFRESH, "Refresh");
        AppendMenuW(menu, 0, (UIntPtr)IDM_EXIT, "Exit");
        SetForegroundWindow(hwnd);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_RIGHTALIGN,
            pt.X, pt.Y, 0, hwnd, IntPtr.Zero);
        PostMessage(hwnd, 0, IntPtr.Zero, IntPtr.Zero);
        DestroyMenu(menu);
        Log("Context menu");
    }

    void HandleTrayActivate(string reason)
    {
        Log($"TrayActivate reason={reason}");
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
                Log($"WM_TRAY ev=0x{ev:X4}");
                if (ev == (int)NIN_SELECT || ev == (int)NIN_KEYSELECT
                    || ev == (int)WM_LBUTTONUP || ev == (int)WM_LBUTTONDBLCLK)
                    HandleTrayActivate($"click/select 0x{ev:X4}");
                else if (ev == (int)WM_MOUSEMOVE)
                {
                    if ((DateTime.Now - lastHover).TotalMilliseconds >= 800)
                        HandleTrayActivate("hover");
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
                Log($"WM_DATA_READY popupVisible={popupVisible}");
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
                Log($"WM_COMMAND id={id}");
                if (id == IDM_REFRESH)
                {
                    lastFetch = DateTime.MinValue;
                    ShowPopup();
                    _ = LoadData();
                }
                else if (id == IDM_EXIT)
                {
                    Log("Exit");
                    DestroyWindow(hwnd);
                }
                return IntPtr.Zero;
            }

            if (msg == 0x0113 && wParam == (IntPtr)timerId)
            {
                Log("Timer");
                lastFetch = DateTime.MinValue;
                _ = LoadData();
                return IntPtr.Zero;
            }

            if (msg == WM_DESTROY)
            {
                Log("WM_DESTROY");
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
                Log("WM_MOUSELEAVE on popup");
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
        paintCount++;
        PAINTSTRUCT ps = default;
        var dc = BeginPaint(hWnd, out ps);
        Log($"PaintPopup #{paintCount} dc={dc} paintRect=({ps.rcPaint.Left},{ps.rcPaint.Top})-({ps.rcPaint.Right},{ps.rcPaint.Bottom})");

        try
        {
            if (dc == IntPtr.Zero)
            {
                Log("BeginPaint returned NULL");
                return;
            }

            GetClientRect(hWnd, out var rc);
            int cw = rc.Right - rc.Left;
            int ch = rc.Bottom - rc.Top;
            Log($"Client size={cw}x{ch}");

            // Bright yellow background so the window is unmistakable
            var bg = CreateSolidBrush(0x00CCFFFF); // light yellow (COLORREF = 0x00BBGGRR)
            FillRect(dc, ref rc, bg);
            DeleteObject(bg);

            SetBkMode(dc, (int)TRANSPARENT);
            SetTextColor(dc, 0x00000000); // black

            var font = CreateFontW(18, 0, 0, 0, 700, 0, 0, 0, 1, 0, 0, 0, 0, "Segoe UI");
            var font2 = CreateFontW(16, 0, 0, 0, 400, 0, 0, 0, 1, 0, 0, 0, 0, "Consolas");
            if (font == IntPtr.Zero) { font = GetStockObject(17); Log("CreateFont failed, stock"); }
            if (font2 == IntPtr.Zero) font2 = font;
            var old = SelectObject(dc, font);

            // Header in plain English with TextOutW (most reliable)
            TextAt(dc, 12, 8, "TGJU Prices");
            TextAt(dc, 200, 8, statusText);
            if (!string.IsNullOrEmpty(lastError))
            {
                SetTextColor(dc, 0x000000CC);
                TextAt(dc, 12, 30, "ERR: " + lastError);
                SetTextColor(dc, 0x00000000);
            }

            SelectObject(dc, font2);
            TextAt(dc, 12, 55, "Name");
            TextAt(dc, 120, 55, "Price");
            TextAt(dc, 280, 55, "Change");

            Dictionary<string, Quote> snapshot;
            lock (dataLock)
                snapshot = new Dictionary<string, Quote>(lastGood, StringComparer.OrdinalIgnoreCase);

            int drawn = 0;
            for (int i = 0; i < slugs.Length; i++)
            {
                int y = 80 + i * 40;
                TextAt(dc, 12, y, namesEn[i] + " / " + namesFa[i]);

                if (!snapshot.TryGetValue(slugs[i], out var q) || q == null)
                {
                    TextAt(dc, 120, y, "---");
                    continue;
                }

                drawn++;
                var pStr = (q.P ?? "").Replace(",", "");
                double.TryParse(pStr, System.Globalization.NumberStyles.Any,
                    System.Globalization.CultureInfo.InvariantCulture, out var p);

                string price = slugs[i] is "ons" or "oil_brent"
                    ? p.ToString("N2")
                    : (p / 10.0).ToString("N0");

                var color = q.Dp > 0 ? 0x00008800 : q.Dp < 0 ? 0x000000CC : 0x00555555;
                SetTextColor(dc, color);
                TextAt(dc, 120, y, price);
                TextAt(dc, 280, y, q.Dp.ToString("+0.00;-0.00;0") + "%  " + (q.T ?? ""));
                SetTextColor(dc, 0x00000000);
            }

            SetTextColor(dc, 0x00333333);
            TextAt(dc, 12, ch - 28, $"drawn={drawn} paints={paintCount}  (click tray icon)");

            Log($"Paint done drawn={drawn} snapshotKeys={snapshot.Count}");

            SelectObject(dc, old);
            if (font != IntPtr.Zero) DeleteObject(font);
            if (font2 != IntPtr.Zero && font2 != font) DeleteObject(font2);
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

    static void TextAt(IntPtr dc, int x, int y, string text)
    {
        if (string.IsNullOrEmpty(text)) return;
        TextOutW(dc, x, y, text, text.Length);
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
        Log("=== exited ===");
    }

    static ushort Register(string name, WndProcDelegate proc, IntPtr hInst)
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
        return RegisterClass(ref wc);
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
    [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr hWnd, IntPtr insertAfter, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr hWnd, uint cmd);
    [DllImport("user32.dll")] static extern bool UpdateWindow(IntPtr hWnd);
    [DllImport("user32.dll")] static extern bool InvalidateRect(IntPtr hWnd, IntPtr rect, bool erase);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] static extern IntPtr LoadIcon(IntPtr hInstance, IntPtr iconName);
    [DllImport("user32.dll")] static extern IntPtr LoadCursor(IntPtr hInstance, int cursor);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern IntPtr GetModuleHandle(string? name);
    [DllImport("kernel32.dll")] static extern uint GetLastError();
    [DllImport("user32.dll")] static extern bool SetTimer(IntPtr hWnd, uint id, uint ms, IntPtr callback);
    [DllImport("user32.dll")] static extern bool KillTimer(IntPtr hWnd, uint id);
    [DllImport("user32.dll")] static extern bool TrackMouseEvent(ref TRACKMOUSEEVENT tme);
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
    [DllImport("gdi32.dll", CharSet = CharSet.Unicode)] static extern bool TextOutW(IntPtr hdc, int x, int y, string lpString, int c);
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
