import json
import os
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler

TGJU_KEYS = [
    ("crypto-tether-irr", "تتر", "تومان"),
    ("price_dollar_rl", "دلار", "تومان"),
    ("geram18", "گرم ۱۸", "تومان"),
    ("sekee", "امامی", "تومان"),
    ("ime_fund_kahroba", "کهربا", "تومان"),
    ("ime_fund_ayar", "عیار", "تومان"),
    ("ons", "انس", "USD"),
    ("oil_brent", "نفت برنت", "USD"),
    ("gc30", "شاخص بورس", ""),
]

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
    if slug == "gc30":
        value = numeric(item.get("p"))
    elif slug in ("ons", "oil_brent"):
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
    direction = str(item.get("dt", "")).lower()
    pct = numeric(item.get("dp"))
    if direction == "high":
        symbol = "🟢 ▲"
    elif direction == "low":
        symbol = "🔴 ▼"
    elif pct is not None and pct > 0:
        symbol = "🟢 ▲"
    elif pct is not None and pct < 0:
        symbol = "🔴 ▼"
    else:
        symbol = "🟡 ●"
    pct_text = f"{abs(pct):.2f}%" if pct is not None else "—"
    return f"{symbol} {fa_digits(pct_text)}"

def fetch_prices():
    keys = ",".join(slug for slug, _, _ in TGJU_KEYS)
    url = "https://api.tgju.org/v1/widget/tmp?keys=" + urllib.parse.quote(keys, safe=",")
    data = api_json(url)
    response = data.get("response", {}) if isinstance(data, dict) else {}
    items = response.get("indicators", []) if isinstance(response, dict) else []
    by_slug = {str(item.get("name", "")): item for item in items if isinstance(item, dict)}
    lines = ["<b>📊 قیمت بازار TGJU</b>", ""]
    found = 0
    for slug, label, unit in TGJU_KEYS:
        item = by_slug.get(slug)
        if not item:
            lines.append(f"{label}: داده موجود نیست")
            continue
        found += 1
        price = format_price(item, slug)
        unit_text = f" {unit}" if unit else ""
        change = format_change(item)
        timestamp = item.get("t") or "—"
        lines.append(f"<b>{label}</b>  <code>{price}{unit_text}</code>")
        lines.append(f"{change}   <i>{timestamp}</i>")
        lines.append("")
    if not found:
        raise RuntimeError("TGJU API returned no indicators")
    lines.append("<i>منبع: TGJU | دریافت تازه در زمان درخواست</i>")
    return "\n".join(lines)

def reply(chat_id, text, keyboard=True):
    markup = {"inline_keyboard": [[{"text": "↻ بروزرسانی قیمت‌ها", "callback_data": "refresh_prices"}]]} if keyboard else None
    payload = {"chat_id": chat_id, "text": text, "parse_mode": "HTML", "disable_web_page_preview": True}
    if markup:
        payload["reply_markup"] = markup
    return telegram("sendMessage", payload)

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
            elif message:
                chat_id = message.get("chat", {}).get("id")
            if chat_id is not None:
                if callback or (message and str(message.get("text", "")).strip().lower() in ("/start", "/price", "/prices", "قیمت", "قیمت‌ها", "قیمت ها", "بروزرسانی")):
                    try:
                        reply(chat_id, fetch_prices())
                    except Exception:
                        reply(chat_id, "⚠️ دریافت قیمت‌ها از TGJU ناموفق بود. کمی بعد دوباره تلاش کنید.", keyboard=False)
                elif message:
                    reply(chat_id, "برای دریافت قیمت‌های تازه، دستور /price را بفرستید.")
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b"ok")
        except Exception:
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b"ok")

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == "/__repair":
            secret = os.environ.get("TGJU_WEBHOOK_SECRET", "")
            if not secret or not os.environ.get("TGJU_BOT_TOKEN"):
                body = json.dumps({"ok": False, "error": "Missing required environment variables"}).encode("utf-8")
                self.send_response(500)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.end_headers()
                self.wfile.write(body)
                return
            try:
                registered = telegram("setWebhook", {
                    "url": "https://tgju-telegram-bot-aflh.onrender.com/",
                    "secret_token": secret
                })
                info = telegram("getWebhookInfo", {})
                details = info.get("result", {}) if isinstance(info, dict) else {}
                try:
                    fetch_prices()
                    tgju_ok = True
                except Exception:
                    tgju_ok = False
                result = {
                    "ok": bool(registered.get("ok")) and bool(info.get("ok")),
                    "webhook_registered": bool(registered.get("result")),
                    "webhook_url": details.get("url", ""),
                    "pending_update_count": details.get("pending_update_count"),
                    "last_error_message": details.get("last_error_message"),
                    "tgju_api_ok": tgju_ok
                }
                body = json.dumps(result, ensure_ascii=False).encode("utf-8")
                self.send_response(200 if result["ok"] else 502)
            except Exception as exc:
                body = json.dumps({"ok": False, "error": type(exc).__name__}).encode("utf-8")
                self.send_response(502)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
            return
        if parsed.path == "/__diag":
            query = urllib.parse.parse_qs(parsed.query)
            secret = os.environ.get("TGJU_WEBHOOK_SECRET", "")
            if not secret or query.get("key", [""])[0] != secret:
                self.send_response(404)
                self.end_headers()
                return
            result = {"token_configured": bool(os.environ.get("TGJU_BOT_TOKEN")), "secret_configured": bool(secret)}
            try:
                info = telegram("getWebhookInfo", {})
                result["telegram_api_ok"] = bool(info.get("ok"))
                data = info.get("result", {}) if isinstance(info, dict) else {}
                result["webhook_url"] = data.get("url", "")
                result["pending_update_count"] = data.get("pending_update_count")
                result["last_error_date"] = data.get("last_error_date")
                result["last_error_message"] = data.get("last_error_message")
                result["allowed_updates"] = data.get("allowed_updates")
            except Exception as exc:
                result["telegram_api_ok"] = False
                result["telegram_error"] = type(exc).__name__
            try:
                prices = fetch_prices()
                result["tgju_api_ok"] = True
                result["price_lines"] = len(prices.splitlines())
            except Exception as exc:
                result["tgju_api_ok"] = False
                result["tgju_error"] = type(exc).__name__
            body = json.dumps(result, ensure_ascii=False).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
            return
        self.send_response(200)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.end_headers()
        self.wfile.write("TGJU Telegram bot webhook is running".encode("utf-8"))
