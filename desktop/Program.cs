using System.Drawing.Drawing2D;
using System.Net.Http.Json;
using System.Text.Json.Serialization;

namespace TgjuDesktop;

internal static class Program
{
    [STAThread]
    static void Main()
    {
        ApplicationConfiguration.Initialize();
        Application.Run(new MainForm());
    }
}

public sealed class MainForm : Form
{
    private readonly HttpClient http = CreateHttpClient();

    private static HttpClient CreateHttpClient()
    {
        var client = new HttpClient { Timeout = TimeSpan.FromSeconds(20) };
        client.DefaultRequestHeaders.UserAgent.ParseAdd("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/154.0 Safari/537.36");
        client.DefaultRequestHeaders.Accept.ParseAdd("application/json,text/plain,*/*");
        client.DefaultRequestHeaders.CacheControl = new System.Net.Http.Headers.CacheControlHeaderValue { NoCache = true };
        return client;
    }
    private readonly TableLayoutPanel table = new();
    private readonly Label status = new();
    private readonly Button refresh = new();
    private readonly System.Windows.Forms.Timer timer = new();
    private readonly Dictionary<string, Quote> lastGood = new();
    private readonly string[] slugs =
    {
        "crypto-tether-irr", "price_dollar_rl", "geram18",
        "ime_fund_kahroba", "ime_fund_ayar", "ons", "oil_brent"
    };
    private readonly string[] names = { "تتر", "دلار", "گرم ۱۸", "کهربا", "عیار", "انس", "نفت برنت" };

    public MainForm()
    {
        Text = "شاخص‌های TGJU";
        RightToLeft = RightToLeft.Yes;
        RightToLeftLayout = true;
        StartPosition = FormStartPosition.CenterScreen;
        ClientSize = new Size(470, 430);
        MinimumSize = new Size(430, 360);
        FormBorderStyle = FormBorderStyle.FixedSingle;
        MaximizeBox = false;
        ShowInTaskbar = true;
        Icon = MakeIcon();

        var title = new Label
        {
            Text = "شاخص‌های TGJU",
            Dock = DockStyle.Fill,
            Font = new Font("Segoe UI", 18, FontStyle.Bold),
            TextAlign = ContentAlignment.MiddleCenter
        };

        refresh.Text = "↻  بروزرسانی";
        refresh.AutoSize = true;
        refresh.Font = new Font("Segoe UI", 10);
        refresh.Padding = new Padding(8, 4, 8, 4);
        refresh.Click += async (_, _) => await LoadData();

        status.Text = "در حال دریافت اطلاعات...";
        status.Dock = DockStyle.Fill;
        status.TextAlign = ContentAlignment.MiddleCenter;
        status.ForeColor = Color.DimGray;
        status.Font = new Font("Segoe UI", 9);

        table.Dock = DockStyle.Fill;
        table.ColumnCount = 3;
        table.RowCount = slugs.Length;
        table.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 26));
        table.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 48));
        table.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 26));
        for (int i = 0; i < slugs.Length; i++)
            table.RowStyles.Add(new RowStyle(SizeType.Percent, 100f / slugs.Length));

        for (int i = 0; i < slugs.Length; i++)
        {
            var name = new Label { Text = names[i], Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleCenter, Font = new Font("Segoe UI", 11) };
            var price = new Label { Name = "p" + i, Text = "—", Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleCenter, Font = new Font("Segoe UI", 12, FontStyle.Bold) };
            var change = new Label { Name = "c" + i, Text = "—", Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleCenter, Font = new Font("Segoe UI", 10) };
            table.Controls.Add(name, 0, i);
            table.Controls.Add(price, 1, i);
            table.Controls.Add(change, 2, i);
        }

        var bottom = new FlowLayoutPanel
        {
            Dock = DockStyle.Fill,
            FlowDirection = FlowDirection.RightToLeft,
            WrapContents = false,
            AutoSize = true
        };
        bottom.Controls.Add(refresh);
        bottom.Controls.Add(status);

        var root = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 4, Padding = new Padding(10) };
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 45));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 42));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 34));
        root.Controls.Add(title, 0, 0);
        root.Controls.Add(new Label { Text = "قیمت‌ها و درصد تغییر", Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleCenter, ForeColor = Color.Gray }, 0, 1);
        root.Controls.Add(table, 0, 2);
        root.Controls.Add(bottom, 0, 3);
        Controls.Add(root);

        timer.Interval = 300000; // 5 minutes
        timer.Tick += async (_, _) => await LoadData();
        timer.Start();
        Shown += async (_, _) => await LoadData();
    }

    private async Task LoadData()
    {
        refresh.Enabled = false;
        try
        {
            var api = "https://api.tgju.org/v1/widget/tmp?keys=" + string.Join(",", slugs);
            using var response = await http.GetAsync(api, HttpCompletionOption.ResponseHeadersRead);
            response.EnsureSuccessStatusCode();
            var data = await response.Content.ReadFromJsonAsync<ApiResponse>();
            var items = data?.Response?.Indicators ?? new List<Quote>();

            foreach (var item in items)
                lastGood[item.Name] = item;

            Render();
            status.Text = "آخرین بروزرسانی: " + DateTime.Now.ToString("HH:mm:ss");
            status.ForeColor = Color.DimGray;
        }
        catch (Exception ex)
        {
            if (lastGood.Count > 0)
                Render();

            status.Text = "خطا: " + ex.GetType().Name + " | " + ex.Message;
            status.ForeColor = Color.Firebrick;
        }
        finally
        {
            refresh.Enabled = true;
        }
    }

    private void Render()
    {
        for (int i = 0; i < slugs.Length; i++)
        {
            if (!lastGood.TryGetValue(slugs[i], out var q))
                continue;

            var price = table.Controls["p" + i] as Label;
            var change = table.Controls["c" + i] as Label;
            if (price is null || change is null) continue;

            double.TryParse(q.P, System.Globalization.NumberStyles.Any, System.Globalization.CultureInfo.InvariantCulture, out var p);
            double.TryParse(q.Dp, System.Globalization.NumberStyles.Any, System.Globalization.CultureInfo.InvariantCulture, out var dp);

            var isUsd = slugs[i] is "ons" or "oil_brent";
            var shown = isUsd ? p : p / 10.0;
            price.Text = isUsd ? shown.ToString("N2") : shown.ToString("N0");

            change.Text = (dp > 0 ? "▲ " : dp < 0 ? "▼ " : "● ") + Math.Abs(dp).ToString("0.00") + "%";
            var color = dp > 0 ? Color.ForestGreen : dp < 0 ? Color.Firebrick : Color.DarkGoldenrod;
            price.ForeColor = color;
            change.ForeColor = color;
        }
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
    public string Dp { get; set; } = "";
}
