import os
import io
import secrets
import logging
from datetime import datetime
from zoneinfo import ZoneInfo

from flask import Flask, request, abort
import requests
from openpyxl import Workbook
from openpyxl.styles import Font, PatternFill, Alignment
from openpyxl.utils import get_column_letter
import psycopg
from psycopg.rows import dict_row

logging.basicConfig(level=logging.INFO)
TOKEN = os.environ["TELEGRAM_BOT_TOKEN"]
DATABASE_URL = os.environ["DATABASE_URL"]
PUBLIC_URL = os.environ.get("RENDER_EXTERNAL_URL", "").rstrip("/")
WEBHOOK_SECRET = os.environ.get("WEBHOOK_SECRET") or secrets.token_urlsafe(32)
BOT_API = f"https://api.telegram.org/bot{TOKEN}"
TEHRAN = ZoneInfo("Asia/Tehran")
app = Flask(__name__)

def db():
    return psycopg.connect(DATABASE_URL, row_factory=dict_row)

def init_db():
    with db() as conn:
        conn.execute("""
            CREATE TABLE IF NOT EXISTS transactions (
                id BIGSERIAL PRIMARY KEY,
                chat_id BIGINT NOT NULL,
                kind TEXT NOT NULL CHECK (kind IN ('deposit','withdrawal')),
                amount BIGINT NOT NULL CHECK (amount > 0),
                created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
            )
        """)

def tg(method, payload=None, files=None):
    r = requests.post(f"{BOT_API}/{method}", data=payload or {}, files=files, timeout=35)
    r.raise_for_status()
    data = r.json()
    if not data.get("ok"):
        raise RuntimeError(f"Telegram API error: {data}")
    return data["result"]

def money(n):
    return f"{int(n):,}".translate(str.maketrans("0123456789", "۰۱۲۳۴۵۶۷۸۹"))

def keyboard():
    return {"keyboard": [[{"text":"🟢 واریز"}, {"text":"🔴 برداشت"}],
                         [{"text":"📊 گزارش"}]],
            "resize_keyboard": True, "is_persistent": True}

def send(chat_id, text, markup=True):
    payload = {"chat_id": chat_id, "text": text}
    if markup:
        import json
        payload["reply_markup"] = json.dumps(keyboard(), ensure_ascii=False)
    tg("sendMessage", payload)

def summary(chat_id):
    with db() as conn:
        row = conn.execute("""
          SELECT
            COALESCE(SUM(amount) FILTER (WHERE kind='deposit'),0) AS deposits,
            COALESCE(SUM(amount) FILTER (WHERE kind='withdrawal'),0) AS withdrawals,
            COUNT(*) AS count
          FROM transactions WHERE chat_id=%s
        """, (chat_id,)).fetchone()
    row = dict(row)
    row["balance"] = int(row["deposits"]) - int(row["withdrawals"])
    return row

def status_text(chat_id, prefix=""):
    s = summary(chat_id)
    return (f"{prefix}📒 وضعیت فعلی حساب\n\n"
            f"🟢 مجموع واریزها: {money(s['deposits'])} تومان\n"
            f"🔴 مجموع برداشت‌ها: {money(s['withdrawals'])} تومان\n"
            f"💰 مانده فعلی: {money(s['balance'])} تومان\n"
            f"🧾 تعداد تراکنش‌ها: {money(s['count'])}")

def start_amount(chat_id, kind):
    # Store pending input in a small database table to survive restarts.
    with db() as conn:
        conn.execute("""
          CREATE TABLE IF NOT EXISTS pending (
            chat_id BIGINT PRIMARY KEY, kind TEXT NOT NULL, updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
          )
        """)
        conn.execute("""
          INSERT INTO pending(chat_id,kind) VALUES(%s,%s)
          ON CONFLICT(chat_id) DO UPDATE SET kind=EXCLUDED.kind, updated_at=NOW()
        """, (chat_id, kind))
    prompt = "مبلغ واریزی را به تومان وارد کن:" if kind == "deposit" else "مبلغ برداشت را به تومان وارد کن:"
    send(chat_id, prompt)

def handle_message(message):
    chat = message.get("chat", {})
    chat_id = chat.get("id")
    if not chat_id:
        return
    # Private chats only.
    if chat.get("type") != "private":
        send(chat_id, "برای حفظ حریم خصوصی، لطفاً ربات را در گفت‌وگوی خصوصی استفاده کن.")
        return
    text = (message.get("text") or "").strip()
    if text in ("/start", "/menu"):
        with db() as conn:
            conn.execute("INSERT INTO users(chat_id) VALUES(%s) ON CONFLICT DO NOTHING", (chat_id,))
        send(chat_id, "سلام! به ربات حسابدار شخصی خوش آمدی.\nاز منوی زیر یکی را انتخاب کن.\n\n" + status_text(chat_id, ""))
        return
    if text == "🟢 واریز":
        start_amount(chat_id, "deposit"); return
    if text == "🔴 برداشت":
        start_amount(chat_id, "withdrawal"); return
    if text == "📊 گزارش":
        send_report(chat_id); return

    with db() as conn:
        pending = conn.execute("SELECT kind FROM pending WHERE chat_id=%s", (chat_id,)).fetchone()
    if not pending:
        send(chat_id, "از سه دکمه منو استفاده کن: واریز، برداشت یا گزارش.")
        return
    normalized = text.translate(str.maketrans("۰۱۲۳۴۵۶۷۸۹٠١٢٣٤٥٦٧٨٩", "01234567890123456789"))
    normalized = normalized.replace(",", "").replace("٬", "").replace(" ", "").replace("تومان", "").replace("ریال", "")
    try:
        amount = int(normalized)
        if amount <= 0 or amount > 10**15:
            raise ValueError()
    except ValueError:
        send(chat_id, "مبلغ معتبر نیست. فقط عدد مثبت به تومان وارد کن؛ مثلاً ۲۵۰۰۰۰.")
        return
    kind = pending["kind"]
    with db() as conn:
        conn.execute("INSERT INTO transactions(chat_id,kind,amount) VALUES(%s,%s,%s)", (chat_id, kind, amount))
        conn.execute("DELETE FROM pending WHERE chat_id=%s", (chat_id,))
    label = "واریز" if kind == "deposit" else "برداشت"
    send(chat_id, status_text(chat_id, f"✅ {label} {money(amount)} تومان ثبت شد.\n\n"))

def send_report(chat_id):
    with db() as conn:
        rows = conn.execute("""
          SELECT id, kind, amount, created_at
          FROM transactions WHERE chat_id=%s ORDER BY created_at, id
        """, (chat_id,)).fetchall()
    wb = Workbook()
    ws = wb.active
    ws.title = "گردش حساب"
    ws.append(["تاریخ و ساعت", "نوع تراکنش", "واریز (تومان)", "برداشت (تومان)", "مانده (تومان)"])
    balance = 0
    for row in rows:
        amount = int(row["amount"])
        is_dep = row["kind"] == "deposit"
        balance += amount if is_dep else -amount
        dt = row["created_at"].astimezone(TEHRAN).replace(tzinfo=None)
        ws.append([dt, "واریز" if is_dep else "برداشت", amount if is_dep else 0, 0 if is_dep else amount, balance])
    for cell in ws[1]:
        cell.font = Font(bold=True, color="FFFFFF")
        cell.fill = PatternFill("solid", fgColor="244062")
        cell.alignment = Alignment(horizontal="center")
    for row in ws.iter_rows(min_row=2):
        row[0].number_format = "yyyy/mm/dd hh:mm"
        for cell in row[2:]:
            cell.number_format = '#,##0'
    widths = [22, 18, 22, 22, 22]
    for i, width in enumerate(widths, 1):
        ws.column_dimensions[get_column_letter(i)].width = width
    ws.freeze_panes = "A2"
    ws.auto_filter.ref = ws.dimensions

    summary_ws = wb.create_sheet("خلاصه حساب")
    s = summary(chat_id)
    summary_ws.append(["عنوان", "مبلغ"])
    summary_ws.append(["مجموع واریزها (تومان)", int(s["deposits"])])
    summary_ws.append(["مجموع برداشت‌ها (تومان)", int(s["withdrawals"])])
    summary_ws.append(["مانده فعلی (تومان)", int(s["balance"])])
    summary_ws.append(["تعداد تراکنش‌ها", int(s["count"])])
    for cell in summary_ws[1]:
        cell.font = Font(bold=True, color="FFFFFF")
        cell.fill = PatternFill("solid", fgColor="244062")
    summary_ws.column_dimensions["A"].width = 32
    summary_ws.column_dimensions["B"].width = 24
    for row in summary_ws.iter_rows(min_row=2, min_col=2, max_col=2):
        row[0].number_format = '#,##0'
    out = io.BytesIO()
    wb.save(out)
    out.seek(0)
    filename = "gozaresh_hesab.xlsx"
    tg("sendDocument", {"chat_id": chat_id, "caption": status_text(chat_id)}, files={"document": (filename, out.getvalue(), "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet")})

@app.get("/")
def home():
    return "Hesabdar bot is running", 200

@app.get("/health")
def health():
    return {"ok": True}, 200

@app.post("/telegram/" + os.environ.get("WEBHOOK_SECRET", "unset"))
def webhook():
    # Route is additionally protected by a random secret path set as an env var.
    data = request.get_json(silent=True) or {}
    try:
        if data.get("message"):
            handle_message(data["message"])
    except Exception:
        logging.exception("Update handling failed")
    return {"ok": True}, 200

def register_webhook():
    if not PUBLIC_URL:
        logging.warning("RENDER_EXTERNAL_URL is missing; webhook not registered.")
        return
    tg("setWebhook", {"url": f"{PUBLIC_URL}/telegram/{WEBHOOK_SECRET}", "drop_pending_updates": "false"})

with app.app_context():
    # Users table lets /start register users; transaction data is isolated per chat.
    with db() as conn:
        conn.execute("CREATE TABLE IF NOT EXISTS users (chat_id BIGINT PRIMARY KEY, created_at TIMESTAMPTZ NOT NULL DEFAULT NOW())")
        conn.execute("""
            CREATE TABLE IF NOT EXISTS pending (
              chat_id BIGINT PRIMARY KEY, kind TEXT NOT NULL, updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
            )
        """)
    init_db()
    register_webhook()
