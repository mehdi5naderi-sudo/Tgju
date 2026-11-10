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
    return CHAT_SETTINGS.setdefault(str(chat_id), {"show_change": True, "show_time": True, "fonts": {"name": 12, "price": 15, "change": 10, "time": 9, "title": 16}})

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
            "unit": _unit,
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
        "fonts": settings.get("fonts", {}),
        "updated": fa_digits(datetime.now(ZoneInfo("Asia/Tehran")).strftime("%H:%M")),
    }, ensure_ascii=False, separators=(",", ":"))

def widget_html():
    return """<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><style>
:root{color-scheme:light dark}
*{box-sizing:border-box}
body{margin:0;padding:2px;color:var(--tg-theme-text-color);background:var(--tg-theme-bg-color);font:14px -apple-system,BlinkMacSystemFont,system-ui,sans-serif}
.top{display:flex;align-items:center;justify-content:space-between;gap:8px;padding:5px 2px 12px}
.brand{display:flex;align-items:center;gap:9px;min-width:0}
.logo{width:36px;height:36px;display:grid;place-items:center;border-radius:12px;background:var(--tg-theme-secondary-bg-color);font-size:19px}
h3{font-size:var(--font-title,16px);line-height:1.2;margin:0;font-weight:750;letter-spacing:-.2px}
.subtitle{font-size:10px;color:var(--tg-theme-hint-color);margin-top:3px}
.stamp{font-size:10px;color:var(--tg-theme-hint-color);white-space:nowrap;background:var(--tg-theme-secondary-bg-color);padding:6px 8px;border-radius:20px}
.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}
.card{min-width:0;padding:10px;border:1px solid var(--tg-theme-section-separator-color);border-radius:13px;background:var(--tg-theme-secondary-bg-color)}
.card-top{display:flex;align-items:center;justify-content:space-between;gap:5px;margin-bottom:9px}
.name{font-size:var(--font-name,12px);color:var(--tg-theme-hint-color);font-weight:600;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.dot{width:6px;height:6px;border-radius:50%;background:var(--tg-theme-button-color);flex:none}
.price{font-size:var(--font-price,15px);font-weight:800;line-height:1.25;letter-spacing:-.35px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;font-variant-numeric:tabular-nums}
.bottom{display:flex;align-items:center;justify-content:space-between;gap:4px;margin-top:7px;min-height:16px}
.change{font-size:var(--font-change,10px);font-weight:700;white-space:nowrap}
.time{font-size:var(--font-time,9px);color:var(--tg-theme-hint-color);white-space:nowrap}
.positive{color:#16a36a}.negative{color:#e05252}.neutral{color:var(--tg-theme-hint-color)}
.footer{display:flex;align-items:center;justify-content:space-between;gap:8px;margin-top:12px;padding:3px 1px 1px}
.note{font-size:10px;color:var(--tg-theme-hint-color)}
button{border:0;border-radius:10px;padding:9px 13px;background:var(--tg-theme-button-color);color:var(--tg-theme-button-text-color);font:600 12px system-ui,sans-serif;white-space:nowrap}
button:disabled{opacity:.55}
#settings{display:none;margin-top:10px;padding:12px;border:1px solid var(--tg-theme-section-separator-color);border-radius:13px;background:var(--tg-theme-secondary-bg-color)}
.setting{display:grid;grid-template-columns:1fr auto;gap:5px 10px;align-items:center;margin:9px 0}
.setting label{font-size:12px;color:var(--tg-theme-text-color)}
.setting output{font-size:11px;color:var(--tg-theme-hint-color);font-variant-numeric:tabular-nums}
.setting input{grid-column:1/3;width:100%;accent-color:var(--tg-theme-button-color);margin:0}
.settings-head{font-size:13px;font-weight:700;margin-bottom:8px}
@media(max-width:310px){.grid{grid-template-columns:1fr}.price{font-size:16px}}
</style></head><body>
<div class="top"><div class="brand"><div class="logo">📈</div><div><h3>بازارهای مالی</h3><div class="subtitle">قیمت لحظه‌ای از TGJU</div></div></div><div class="stamp" id="updated">--:--</div></div>
<div class="grid" id="rows"></div>
<div id="settings"><div class="settings-head">⚙️ تنظیم اندازه نوشته‌ها</div>
<div class="setting"><label for="f-name">نام شاخص‌ها</label><output id="v-name"></output><input id="f-name" type="range" min="10" max="20" step="1" data-font="name"></div>
<div class="setting"><label for="f-price">قیمت‌ها</label><output id="v-price"></output><input id="f-price" type="range" min="12" max="28" step="1" data-font="price"></div>
<div class="setting"><label for="f-change">درصد تغییرات</label><output id="v-change"></output><input id="f-change" type="range" min="9" max="20" step="1" data-font="change"></div>
<div class="setting"><label for="f-time">زمان</label><output id="v-time"></output><input id="f-time" type="range" min="8" max="18" step="1" data-font="time"></div>
<div class="setting"><label for="f-title">عنوان داشبورد</label><output id="v-title"></output><input id="f-title" type="range" min="12" max="24" step="1" data-font="title"></div>
<button id="reset" type="button">بازگشت به اندازه پیش‌فرض</button>
</div>
<div class="footer"><span class="note" id="status">آخرین داده دریافتی</span><div style="display:flex;gap:6px"><button id="settingsBtn" type="button">⚙️ تنظیمات</button><button id="refresh">↻ تازه‌سازی</button></div></div>
<script>
(function(){
const W=window.TelegramWidget, rows=document.getElementById('rows'), updated=document.getElementById('updated'), status=document.getElementById('status'), button=document.getElementById('refresh'), panel=document.getElementById('settings');
const defaults={name:12,price:15,change:10,time:9,title:16};
function applyFonts(fonts){Object.keys(defaults).forEach(function(key){const value=Number((fonts||{})[key])||defaults[key];document.documentElement.style.setProperty('--font-'+key,value+'px');const input=document.getElementById('f-'+key);input.value=value;document.getElementById('v-'+key).textContent=value+' px';});}
function render(){
 let data={}; try{data=JSON.parse(W.chatData||'{}')}catch(e){}
 rows.textContent='';
 (data.rows||[]).forEach(function(item){
  const card=document.createElement('div');card.className='card';
  const top=document.createElement('div');top.className='card-top';
  const name=document.createElement('div');name.className='name';name.textContent=item.name;
  const dot=document.createElement('span');dot.className='dot';top.append(name,dot);
  const price=document.createElement('div');price.className='price';price.textContent=item.price+(item.unit?' '+item.unit:'');
  const bottom=document.createElement('div');bottom.className='bottom';
  const change=document.createElement('span');change.className='change '+(String(item.change||'').includes('🟢')?'positive':String(item.change||'').includes('🔴')?'negative':'neutral');change.textContent=item.change||'';
  const time=document.createElement('span');time.className='time';time.textContent=item.time||'';
  bottom.append(change,time);card.append(top,price,bottom);rows.appendChild(card);
 });
 updated.textContent=data.updated?'به‌روز '+data.updated:'—';applyFonts(data.fonts||{});
}
W.onEvent('chatDataChanged',render);render();
document.getElementById('settingsBtn').addEventListener('click',function(){panel.style.display=panel.style.display==='block'?'none':'block';});
document.querySelectorAll('[data-font]').forEach(function(input){
 input.addEventListener('input',function(){const key=input.dataset.font;document.documentElement.style.setProperty('--font-'+key,input.value+'px');document.getElementById('v-'+key).textContent=input.value+' px';});
 input.addEventListener('change',async function(){const key=input.dataset.font;status.textContent='در حال ذخیره تنظیمات…';try{await W.request('font:'+key+':'+input.value);status.textContent='اندازه فونت ذخیره شد';}catch(e){status.textContent='ذخیره انجام نشد؛ دوباره تلاش کنید';}});
});
document.getElementById('reset').addEventListener('click',function(){Object.keys(defaults).forEach(function(key){const input=document.getElementById('f-'+key);input.value=defaults[key];input.dispatchEvent(new Event('input'));input.dispatchEvent(new Event('change'));});});
button.addEventListener('click',async function(){
 button.disabled=true;status.textContent='در حال دریافت قیمت‌ها…';
 try{const answer=await W.request('refresh');status.textContent=answer.result||'درخواست ارسال شد'}
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
    if not chat_id or not message_id:
        telegram("answerWidgetQuery", {"widget_query_id": query_id, "data": "درخواست نامعتبر است"})
        return
    request_data = str(query.get("data", ""))
    if request_data.startswith("font:"):
        parts = request_data.split(":")
        limits = {"name": (10, 20), "price": (12, 28), "change": (9, 20), "time": (8, 18), "title": (12, 24)}
        if len(parts) == 3 and parts[1] in limits:
            try:
                value = int(parts[2])
                low, high = limits[parts[1]]
                if low <= value <= high:
                    settings_for(chat_id)["fonts"][parts[1]] = value
                else:
                    raise ValueError("font out of range")
            except ValueError:
                telegram("answerWidgetQuery", {"widget_query_id": query_id, "data": "اندازه فونت نامعتبر است"})
                return
        else:
            telegram("answerWidgetQuery", {"widget_query_id": query_id, "data": "تنظیم نامعتبر است"})
            return
    elif request_data != "refresh":
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
