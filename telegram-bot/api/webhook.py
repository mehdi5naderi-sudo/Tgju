import html
import json
import os
import re
import urllib.parse
import urllib.request
import time
from datetime import datetime
from zoneinfo import ZoneInfo
from http.server import BaseHTTPRequestHandler

TGJU_KEYS = [
    ("crypto-tether-irr", "تتر", ""),
    ("price_dollar_rl", "دلار", ""),
    ("geram18", "گرم", ""),
    ("sekee", "امامی", ""),
    ("ime_fund_kahroba", "کهربا", ""),
    ("ime_fund_ayar", "عیار", ""),
    ("ons", "انس", ""),
    ("oil_brent", "برنت", ""),
    ("gc30", "بورس", ""),
    ("crypto-bitcoin", "بیت‌کوین", "USD"),
]

# Preferences are per chat for the lifetime of this running instance.
CHAT_SETTINGS = {}

def settings_for(chat_id):
    return CHAT_SETTINGS.setdefault(str(chat_id), {"show_change": True, "show_time": True})

def api_json(url, payload=None, headers=None):
    data = None if payload is None else json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(url, data=data, headers=headers or {}, method="GET" if data is None else "POST")
    with urllib.request.urlopen(req, timeout=15) as response:
        return json.loads(response.read().decode("utf-8"))

def telegram(method, payload):
    token = os.environ["TGJU_BOT_TOKEN"]
    url = f"https://api.telegram.org/bot{token}/{method}"
    return api_json(url, payload, {"Content-Type": "application/json"})

def fa_digits(value):
    return str(value).translate(str.maketrans("0123456789", "۰۱۲۳۴۵۶۷۸۹"))

def numeric(value):
    try:
        return float(str(value).replace(",", "").strip())
    except (TypeError, ValueError):
        return None

def format_price(item, slug):
    if slug in ("gc30", "ons", "oil_brent", "crypto-bitcoin"):
        value = numeric(item.get("p"))
    elif slug == "crypto-tether-irr" and item.get("p_irr"):
        value = numeric(item.get("p_irr"))
        value = value / 10 if value is not None else None
    else:
        value = numeric(item.get("p"))
        value = value / 10 if value is not None else None
    if value is None:
        return "—"
    # Bitcoin, like local market indicators, is shown without decimal places.
    decimals = 2 if slug in ("ons", "oil_brent") else 0
    return fa_digits(f"{value:,.{decimals}f}")

def change_is_fresh(raw_time):
    # Match the web app: only timestamps less than five minutes old are fresh.
    value = str(raw_time or "").strip().translate(str.maketrans("۰۱۲۳۴۵۶۷۸۹", "0123456789"))
    match = re.search(r"(?:^|\s)(\d{1,2}):(\d{2})(?::(\d{2}))?(?:\s|$)", value)
    if not match:
        return False
    hour, minute, second = int(match.group(1)), int(match.group(2)), int(match.group(3) or 0)
    if hour > 23 or minute > 59 or second > 59:
        return False
    now = datetime.now(ZoneInfo("Asia/Tehran"))
    updated = now.replace(hour=hour, minute=minute, second=second, microsecond=0)
    age = (now - updated).total_seconds()
    return 0 <= age < 5 * 60

def format_change(item):
    pct = numeric(item.get("dp"))
    if pct is None:
        return "⚪ —"
    # Telegram does not support font colors; use a colored marker instead.
    if not change_is_fresh(item.get("t")):
        marker = "⚪"
    else:
        direction = str(item.get("dt", "")).strip().lower()
        marker = "🟢" if direction == "high" else ("🔴" if direction == "low" else "🟡")
    direction = str(item.get("dt", "")).strip().lower()
    sign = "+" if direction == "high" else ("−" if direction == "low" else "")
    return marker + " " + fa_digits(f"{abs(pct):.2f}")

def format_time(value):
    text = str(value or "—").strip()
    # If the source includes HH:MM:SS, omit seconds.
    return fa_digits(re.sub(r"(\d{1,2}:\d{2}):\d{2}", r"\1", text))

def fetch_prices(chat_id):
    settings = settings_for(chat_id)
    # Use the same first-party feed as the Android widget and PWA.
    # The timestamp query helps avoid a stale shared CDN response.
    url = "https://call1.tgju.org/ajax.json?_= " + str(int(time.time() * 1000))
    url = url.replace("?_= ", "?_=")
    data = api_json(url, headers={"Accept": "application/json", "Cache-Control": "no-cache", "Pragma": "no-cache"})
    current = data.get("current", {}) if isinstance(data, dict) else {}
    if not isinstance(current, dict):
        current = {}

    rows = []
    found = 0
    for slug, label, _unit in TGJU_KEYS:
        item = current.get(slug)
        if not isinstance(item, dict) or item.get("p") is None:
            rows.append({"name": label, "price": "—", "change": "", "time": ""})
            continue
        found += 1
        rows.append({
            "name": label,
            "price": format_price(item, slug),
            "change": format_change(item) if settings["show_change"] else "",
            "time": format_time(item.get("t")) if settings["show_time"] else "",
        })
    if not found:
        raise RuntimeError("TGJU API returned no indicators")

    # Restore the previous spacing style, with time and change first.
    price_width = max(len("قیمت"), max(len(row["price"]) for row in rows))
    change_width = max([len("تغییر")] + [len(row["change"]) for row in rows]) if settings["show_change"] else 0
    time_width = max([len("زمان")] + [len(row["time"]) for row in rows]) if settings["show_time"] else 0
    lines = ["<b>📊 قیمت بازار TGJU</b>"]
    for row in rows:
        parts = []
        if settings["show_time"]:
            parts.append(row["time"].rjust(time_width))
        if settings["show_change"]:
            parts.append(row["change"].rjust(change_width))
        parts.extend([
            row["name"],
            row["price"],
        ])
        lines.append(" ".join(html.escape(part) for part in parts))
    return "\n".join(lines)


def fetch_widget_data(chat_id):
    settings = settings_for(chat_id)
    url = "https://call1.tgju.org/ajax.json?_=" + str(int(time.time() * 1000))
    data = api_json(url, headers={"Accept": "application/json", "Cache-Control": "no-cache", "Pragma": "no-cache"})
    current = data.get("current", {}) if isinstance(data, dict) else {}
    if not isinstance(current, dict):
        current = {}
    rows = []
    found = 0
    for slug, label, _unit in TGJU_KEYS:
        item = current.get(slug)
        if not isinstance(item, dict) or item.get("p") is None:
            continue
        found += 1
        rows.append({
            "name": label,
            "price": format_price(item, slug),
            "change": format_change(item) if settings["show_change"] else "",
            "time": format_time(item.get("t")) if settings["show_time"] else "",
        })
    if not found:
        raise RuntimeError("TGJU API returned no indicators")
    return json.dumps({
        "rows": rows,
        "updated": fa_digits(datetime.now(ZoneInfo("Asia/Tehran")).strftime("%H:%M")),
    }, ensure_ascii=False, separators=(",", ":"))

def widget_html():
    return """<!doctype html><html><head><meta charset="utf-8"><style>
body{margin:0;color:var(--tg-theme-text-color);font:14px system-ui,sans-serif}
header{display:flex;justify-content:space-between;align-items:center;margin-bottom:9px}
h3{font-size:16px;margin:0}.muted{color:var(--tg-theme-hint-color);font-size:11px}
.rows{display:grid;grid-template-columns:1fr auto;gap:0 10px}
.row{display:contents}.name,.value{padding:7px 0;border-bottom:1px solid var(--tg-theme-section-separator-color)}
.value{text-align:left;font-weight:650;font-variant-numeric:tabular-nums}
.meta{font-size:10px;color:var(--tg-theme-hint-color);font-weight:400}
footer{display:flex;justify-content:space-between;align-items:center;margin-top:10px}
button{border:0;border-radius:9px;padding:8px 12px;background:var(--tg-theme-button-color);color:var(--tg-theme-button-text-color);font:inherit}
button:disabled{opacity:.55}
</style></head><body>
<header><h3>📊 قیمت‌های TGJU</h3><span class="muted" id="updated"></span></header>
<div class="rows" id="rows"></div>
<footer><span class="muted" id="status">قیمت‌ها از منبع TGJU</span><button id="refresh">↻ به‌روزرسانی</button></footer>
<script>
(function(){
const W=window.TelegramWidget, rows=document.getElementById('rows'), updated=document.getElementById('updated'), status=document.getElementById('status'), button=document.getElementById('refresh');
function render(){
 let data={}; try{data=JSON.parse(W.chatData||'{}')}catch(e){}
 rows.textContent='';
 (data.rows||[]).forEach(function(item){
  const name=document.createElement('div'); name.className='name'; name.textContent=item.name;
  const value=document.createElement('div'); value.className='value'; value.textContent=item.price;
  if(item.change||item.time){const meta=document.createElement('div');meta.className='meta';meta.textContent=[item.change,item.time].filter(Boolean).join(' · ');value.appendChild(meta)}
  rows.appendChild(name);rows.appendChild(value);
 });
 updated.textContent=data.updated?'بروزرسانی '+data.updated:'';
}
W.onEvent('chatDataChanged',render);render();
button.addEventListener('click',async function(){
 button.disabled=true;status.textContent='در حال دریافت...';
 try{const answer=await W.request('refresh');status.textContent=answer.result||'درخواست انجام شد'}
 catch(e){status.textContent='خطا در دریافت؛ دوباره تلاش کنید'}
 finally{button.disabled=false}
});
})();
</script></body></html>"""

def send_price_widget(chat_id):
    state = fetch_widget_data(chat_id)
    return telegram("sendWidget", {
        "chat_id": chat_id,
        "html": widget_html(),
        "caption": "داشبورد قیمت‌های TGJU • برای دریافت قیمت تازه، به‌روزرسانی را بزنید",
        "allow_queries": True,
        "chat_data": state,
    })

def handle_widget_query(query):
    query_id = query.get("id")
    chat = query.get("chat") or {}
    message_id = query.get("message_id")
    chat_id = chat.get("id")
    if query.get("data") != "refresh" or not chat_id or not message_id:
        telegram("answerWidgetQuery", {"widget_query_id": query_id, "data": "درخواست نامعتبر است"})
        return
    try:
        state = fetch_widget_data(chat_id)
        telegram("editMessageWidgetData", {
            "chat_id": chat_id,
            "message_id": message_id,
            "chat_data": state,
        })
        telegram("answerWidgetQuery", {"widget_query_id": query_id, "data": "قیمت‌ها به‌روز شدند"})
    except Exception:
        telegram("answerWidgetQuery", {"widget_query_id": query_id, "data": "دریافت قیمت‌ها ناموفق بود؛ دوباره تلاش کنید"})

def price_keyboard():
    return {"inline_keyboard": [
        [{"text": "📊 داشبورد تعاملی", "callback_data": "open_widget"}],
        [{"text": "↻ بروزرسانی قیمت‌ها", "callback_data": "refresh_prices"}],
    ]}

def settings_keyboard(chat_id):
    settings = settings_for(chat_id)
    change_label = ("✅ " if settings["show_change"] else "☐ ") + "نمایش تغییر"
    time_label = ("✅ " if settings["show_time"] else "☐ ") + "نمایش زمان بروزرسانی"
    return {"inline_keyboard": [
        [{"text": change_label, "callback_data": "toggle_change"}],
        [{"text": time_label, "callback_data": "toggle_time"}],
        [{"text": "↩️ بازگشت به قیمت‌ها", "callback_data": "back_prices"}],
    ]}

def reply(chat_id, text, markup=None):
    payload = {"chat_id": chat_id, "text": text, "parse_mode": "HTML", "disable_web_page_preview": True}
    if markup:
        payload["reply_markup"] = markup
    return telegram("sendMessage", payload)

def show_settings(chat_id):
    settings = settings_for(chat_id)
    text = (
        "<b>⚙️ تنظیمات نمایش</b>\n"
        f"تغییر: {'روشن' if settings['show_change'] else 'خاموش'}\n"
        f"زمان بروزرسانی: {'روشن' if settings['show_time'] else 'خاموش'}"
    )
    return reply(chat_id, text, settings_keyboard(chat_id))

class handler(BaseHTTPRequestHandler):
    def do_POST(self):
        secret = os.environ.get("TGJU_WEBHOOK_SECRET", "")
        received = self.headers.get("X-Telegram-Bot-Api-Secret-Token", "")
        if secret and received != secret:
            self.send_response(403)
            self.end_headers()
            self.wfile.write(b"forbidden")
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            update = json.loads(self.rfile.read(length).decode("utf-8"))
            message = update.get("message") or update.get("edited_message")
            callback = update.get("callback_query")
            widget_query = update.get("widget_query")
            chat_id = None
            if widget_query:
                handle_widget_query(widget_query)
            elif callback:
                chat_id = callback.get("message", {}).get("chat", {}).get("id")
                telegram("answerCallbackQuery", {"callback_query_id": callback.get("id")})
                action = callback.get("data", "")
                if action == "open_widget":
                    try:
                        send_price_widget(chat_id)
                    except Exception:
                        reply(chat_id, "⚠️ ارسال داشبورد تعاملی ممکن نشد. لطفاً تلگرام را به‌روز کنید یا از /price استفاده کنید.")
                elif action == "open_settings":
                    show_settings(chat_id)
                elif action == "toggle_change":
                    settings_for(chat_id)["show_change"] = not settings_for(chat_id)["show_change"]
                    show_settings(chat_id)
                elif action == "toggle_time":
                    settings_for(chat_id)["show_time"] = not settings_for(chat_id)["show_time"]
                    show_settings(chat_id)
                elif action in ("refresh_prices", "back_prices"):
                    try:
                        reply(chat_id, fetch_prices(chat_id), price_keyboard())
                    except Exception:
                        reply(chat_id, "⚠️ دریافت قیمت‌ها از TGJU ناموفق بود. کمی بعد دوباره تلاش کنید.")
            elif message:
                chat_id = message.get("chat", {}).get("id")
                command = str(message.get("text", "")).strip().lower()
                if command in ("/settings", "تنظیمات"):
                    show_settings(chat_id)
                elif command in ("/widget", "داشبورد"):
                    try:
                        send_price_widget(chat_id)
                    except Exception:
                        reply(chat_id, "⚠️ ارسال داشبورد تعاملی ممکن نشد. لطفاً تلگرام را به‌روز کنید یا از /price استفاده کنید.")
                elif command in ("/start", "/price", "/prices", "قیمت", "قیمت‌ها", "قیمت ها", "بروزرسانی"):
                    try:
                        reply(chat_id, fetch_prices(chat_id), price_keyboard())
                    except Exception:
                        reply(chat_id, "⚠️ دریافت قیمت‌ها از TGJU ناموفق بود. کمی بعد دوباره تلاش کنید.")
                else:
                    reply(chat_id, "برای قیمت‌ها /price و برای تنظیمات /settings را بفرستید.")
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b"ok")
        except Exception:
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b"ok")

    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.end_headers()
        self.wfile.write("TGJU Telegram bot webhook is running".encode("utf-8"))

    def do_HEAD(self):
        self.send_response(200)
        self.end_headers()
