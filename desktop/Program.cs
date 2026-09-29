using System.Drawing.Drawing2D;
using System.Net;
using System.Net.Http.Json;
using System.Security.Authentication;
using System.Text.Json.Serialization;

namespace TgjuDesktop;

internal static class Program
{
    [STAThread]
    static void Main()
    {
        ApplicationConfiguration.Initialize();
        Application.Run(new TrayContext());
    }
}

public sealed class TrayContext : ApplicationContext
{
    private readonly HttpClient http = CreateHttpClient();
    private readonly NotifyIcon tray = new();
    private readonly System.Windows.Forms.Timer timer = new();
    private readonly Dictionary<string, Quote> lastGood = new();
    private readonly string[] slugs =
    {
        "crypto-tether-irr", "price_dollar_rl", "geram18",
        "ime_fund_kahroba", "ime_fund_ayar", "ons", "oil_brent"
    };
    private readonly string[] names = { "تتر", "دلار", "گرم ۱۸", "کهربا", "عیار", "انس", "نفت برنت" };
    private PopupForm? popup;
    private DateTime lastHoverRefresh = DateTime.MinValue;
    private bool loading;

    public TrayContext()
    {
        tray.Icon = MakeIcon();
        tray.Text = "شاخص‌های TGJU";
        tray.Visible = true;
        tray.MouseMove += Tray_MouseMove;
        tray.MouseClick += Tray_MouseClick;

        var menu = new ContextMenuStrip();
        menu.Items.Add("نمایش شاخص‌ها", null, async (_, _) =>
        {
            await LoadData();
            ShowPopup();
        });
        menu.Items.Add("بروزرسانی", null, async (_, _) => await LoadData());
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("خروج", null, (_, _) => ExitThread());
        tray.ContextMenuStrip = menu;

        timer.Interval = 300000;
        timer.Tick += async (_, _) => await LoadData();
        timer.Start();

        _ = LoadData();
    }

    private async void Tray_MouseMove(object? sender, MouseEventArgs e)
    {
        if (loading) return;
        if ((DateTime.Now - lastHoverRefresh).TotalSeconds < 2) return;

        lastHoverRefresh = DateTime.Now;
        await LoadData();
        ShowPopup();
    }

    private async void Tray_MouseClick(object? sender, MouseEventArgs e)
    {
        if (e.Button == MouseButtons.Left)
        {
            await LoadData();
            ShowPopup();
        }
    }

    private void ShowPopup()
    {
        if (lastGood.Count == 0) return;

        popup?.Close();
        popup?.Dispose();
        popup = new PopupForm(slugs, names, lastGood);

        var cursor = Cursor.Position;
        var screen = Screen.FromPoint(cursor);
        var x = Math.Min(cursor.X - popup.Width + 16, screen.WorkingArea.Right - popup.Width - 8);
        var y = cursor.Y - popup.Height - 10;
        if (y < screen.WorkingArea.Top + 8) y = cursor.Y + 20;
        if (x < screen.WorkingArea.Left + 8) x = screen.WorkingArea.Left + 8;

        popup.StartPosition = FormStartPosition.Manual;
        popup.Location = new Point(x, y);
        popup.Show();
    }

    private async Task LoadData()
    {
        if (loading) return;
        loading = true;
        var api = "https://api.tgju.org/v1/widget/tmp?keys=" + string.Join(",", slugs);

        try
        {
            using var request = new HttpRequestMessage(HttpMethod.Get, api)
            {
                Version = HttpVersion.Version11,
                VersionPolicy = HttpVersionPolicy.RequestVersionExact
            };
            using var response = await http.SendAsync(request, HttpCompletionOption.ResponseHeadersRead);
            response.EnsureSuccessStatusCode();

            var data = await response.Content.ReadFromJsonAsync<ApiResponse>();
            var items = data?.Response?.Indicators ?? new List<Quote>();

            foreach (var item in items)
                lastGood[item.Name] = item;

            if (popup is not null && !popup.IsDisposed)
                popup.UpdateData(lastGood);
        }
        catch
        {
        }
        finally
        {
            loading = false;
        }
    }

    protected override void ExitThreadCore()
    {
        timer.Stop();
        tray.Visible = false;
        tray.Dispose();
        popup?.Close();
        popup?.Dispose();
        http.Dispose();
        base.ExitThreadCore();
    }

    private static HttpClient CreateHttpClient()
    {
        var handler = new HttpClientHandler
        {
            UseProxy = true,
            Proxy = null,
            SslProtocols = SslProtocols.Tls12,
            AutomaticDecompression = DecompressionMethods.All
        };

        var client = new HttpClient(handler) { Timeout = TimeSpan.FromSeconds(20) };
        client.DefaultRequestVersion = HttpVersion.Version11;
        client.DefaultVersionPolicy = HttpVersionPolicy.RequestVersionExact;
        client.DefaultRequestHeaders.UserAgent.ParseAdd(
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/154.0 Safari/537.36");
        client.DefaultRequestHeaders.Accept.ParseAdd("application/json,text/plain,*/*");
        client.DefaultRequestHeaders.CacheControl =
            new System.Net.Http.Headers.CacheControlHeaderValue { NoCache = true };
        return client;
    }

    private static Icon MakeIcon()
    {
        using var bmp = new Bitmap(64, 64);
        using (var g = Graphics.FromImage(bmp))
        {
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(Color.FromArgb(24, 90, 150));
            using var pen = new Pen(Color.White, 5);
            g.DrawLine(pen, 10, 48, 22, 36);
            g.DrawLine(pen, 22, 36, 33, 42);
            g.DrawLine(pen, 33, 42, 52, 18);
            g.FillEllipse(Brushes.White, 7, 45, 6, 6);
            g.FillEllipse(Brushes.White, 49, 15, 6, 6);
        }
        return Icon.FromHandle(bmp.GetHicon());
    }
}

public sealed class PopupForm : Form
{
    private readonly string[] slugs;
    private readonly string[] names;
    private readonly TableLayoutPanel table = new();

    public PopupForm(string[] slugs, string[] names, Dictionary<string, Quote> data)
    {
        this.slugs = slugs;
        this.names = names;

        FormBorderStyle = FormBorderStyle.FixedSingle;
        ControlBox = false;
        ShowInTaskbar = false;
        TopMost = true;
        RightToLeft = RightToLeft.Yes;
        RightToLeftLayout = true;
        BackColor = Color.White;
        ClientSize = new Size(455, 390);
        Padding = new Padding(10);

        table.Dock = DockStyle.Fill;
        table.ColumnCount = 3;
        table.RowCount = slugs.Length + 1;
        table.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 22));
        table.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 43));
        table.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 35));

        table.Controls.Add(Header("شاخص"), 0, 0);
        table.Controls.Add(Header("قیمت"), 1, 0);
        table.Controls.Add(Header("تغییر / زمان"), 2, 0);

        for (int i = 0; i < slugs.Length; i++)
        {
            table.RowStyles.Add(new RowStyle(SizeType.Percent, 100f / (slugs.Length + 1)));
            table.Controls.Add(new Label
            {
                Text = names[i],
                Dock = DockStyle.Fill,
                TextAlign = ContentAlignment.MiddleCenter,
                Font = new Font("Segoe UI", 11, FontStyle.Bold)
            }, 0, i + 1);

            table.Controls.Add(new Label
            {
                Name = "p" + i,
                Dock = DockStyle.Fill,
                TextAlign = ContentAlignment.MiddleCenter,
                Font = new Font("Segoe UI", 12, FontStyle.Bold)
            }, 1, i + 1);

            table.Controls.Add(new Label
            {
                Name = "c" + i,
                Dock = DockStyle.Fill,
                TextAlign = ContentAlignment.MiddleCenter,
                Font = new Font("Segoe UI", 9.5f)
            }, 2, i + 1);
        }

        Controls.Add(table);
        UpdateData(data);
        Deactivate += (_, _) => Hide();
    }

    private static Label Header(string text) => new()
    {
        Text = text,
        Dock = DockStyle.Fill,
        TextAlign = ContentAlignment.MiddleCenter,
        Font = new Font("Segoe UI", 10, FontStyle.Bold),
        ForeColor = Color.DimGray
    };

    protected override bool ShowWithoutActivation => true;

    public void UpdateData(Dictionary<string, Quote> data)
    {
        for (int i = 0; i < slugs.Length; i++)
        {
            if (!data.TryGetValue(slugs[i], out var q)) continue;

            var price = table.Controls["p" + i] as Label;
            var change = table.Controls["c" + i] as Label;
            if (price is null || change is null) continue;

            double.TryParse(q.P, System.Globalization.NumberStyles.Any,
                System.Globalization.CultureInfo.InvariantCulture, out var p);

            var isUsd = slugs[i] is "ons" or "oil_brent";
            var shown = isUsd ? p : p / 10.0;
            price.Text = isUsd ? shown.ToString("N2") : shown.ToString("N0");

            var dp = q.Dp;
            var color = dp > 0 ? Color.ForestGreen
                : dp < 0 ? Color.Firebrick
                : Color.DarkGoldenrod;

            var symbol = dp > 0 ? "▲" : dp < 0 ? "▼" : "●";
            var time = string.IsNullOrWhiteSpace(q.T) ? "—" : q.T;
            change.Text = symbol + " " + Math.Abs(dp).ToString("0.00") + "%\r\n" + time;
            change.ForeColor = color;
            price.ForeColor = color;
        }
    }
}

public sealed class ApiResponse
{
    [JsonPropertyName("response")]
    public ApiBody? Response { get; set; }
}

public sealed class ApiBody
{
    [JsonPropertyName("indicators")]
    public List<Quote>? Indicators { get; set; }
}

public sealed class Quote
{
    [JsonPropertyName("name")]
    public string Name { get; set; } = "";

    [JsonPropertyName("p")]
    public string P { get; set; } = "";

    [JsonPropertyName("dp")]
    public double Dp { get; set; }

    [JsonPropertyName("t")]
    public string T { get; set; } = "";
}
