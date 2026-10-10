import html
import json
import os
import re
import urllib.parse
import urllib.request
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
    if slug in ("gc30", "ons", "oil_brent"):
        value = numeric(item.get("p"))
    elif slug == "crypto-tether-irr" and item.get("p_irr"):
        value = numeric(item.get("p_irr"))
        value = value / 10 if value is not None else None
    else:
        value = numeric(item.get("p"))
        value = value / 10 if value is not None else None
    if value is None:
        return "—"
    decimals = 2 if slug in ("ons", "oil_brent") else 0
    return fa_digits(f"{value:,.{decimals}f}")

def format_change(item):
    pct = numeric(item.get("dp"))
    if pct is None:
        return "—"
    sign = "+" if pct > 0 else ("−" if pct < 0 else "")
    return fa_digits(sign + f"{abs(pct):.2f}")

def format_time(value):
    text = str(value or "—").strip()
    # If the source includes HH:MM:SS, omit seconds.
    return fa_digits(re.sub(r"(\d{1,2}:\d{2}):\d{2}", r"\1", text))

def fetch_prices(chat_id):
    settings = settings_for(chat_id)
    keys = ",".join(slug for slug, _, _ in TGJU_KEYS)
    url = "https://api.tgju.org/v1/widget/tmp?keys=" + urllib.parse.quote(keys, safe=",")
    data = api_json(url)
    response = data.get("response", {}) if isinstance(data, dict) else {}
    items = response.get("indicators", []) if isinstance(response, dict) else []
    by_slug = {str(item.get("name", "")): item for item in items if isinstance(item, dict)}
    lines = ["<b>📊 قیمت بازار TGJU</b>"]
    found = 0
    for slug, label, _unit in TGJU_KEYS:
        item = by_slug.get(slug)
        if not item:
            lines.append(f"{html.escape(label)}: داده موجود نیست")
            continue
        found += 1
        price = html.escape(format_price(item, slug))
        lines.append(f"{html.escape(label)}  {price}")
        details = []
        if settings["show_change"]:
            details.append("تغییر " + format_change(item))
        if settings["show_time"]:
            details.append(format_time(item.get("t")))
        if details:
            lines.append("   ".join(html.escape(part) for part in details))
    if not found:
        raise RuntimeError("TGJU API returned no indicators")
    lines.append("<i>منبع: TGJU</i>")
    return "\n".join(lines)

def price_keyboard():
    return {"inline_keyboard": [
        [{"text": "↻ بروزرسانی قیمت‌ها", "callback_data": "refresh_prices"}],
        [{"text": "⚙️ تنظیمات", "callback_data": "open_settings"}],
    ]}

def settings_keyboard(chat_id):
    settings = settings_for(chat_id)
    change_label = ("✅ " if settings["show_change"] else "☐ ") + "نمایش درصد تغییر"
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
        f"درصد تغییر: {'روشن' if settings['show_change'] else 'خاموش'}\n"
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
            chat_id = None
            if callback:
                chat_id = callback.get("message", {}).get("chat", {}).get("id")
                telegram("answerCallbackQuery", {"callback_query_id": callback.get("id")})
                action = callback.get("data", "")
                if action == "open_settings":
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
