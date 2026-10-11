# ربات حسابدار شخصی تلگرام

ربات فارسی با سه دکمه: واریز، برداشت، گزارش. مبلغ‌ها به تومان هستند و بعد از هر تراکنش، مانده فعلی نمایش داده می‌شود. گزارش اکسل شامل گردش حساب با مانده در هر ردیف و برگه خلاصه است.

## متغیرهای محیطی در Render
- `TELEGRAM_BOT_TOKEN`: توکن BotFather (محرمانه)
- `DATABASE_URL`: آدرس PostgreSQL دائمی
- `WEBHOOK_SECRET`: یک رشته تصادفی محرمانه
- `RENDER_EXTERNAL_URL`: توسط Render به‌صورت خودکار تنظیم می‌شود

## دستورات Render
Build: `pip install -r hesabdar-bot/requirements.txt`
Start: `cd hesabdar-bot && gunicorn app:app --bind 0.0.0.0:$PORT`

**نکته:** برای حفظ سوابق مالی، حتماً `DATABASE_URL` باید به دیتابیس PostgreSQL با ماندگاری مناسب اشاره کند. از SQLite یا فایل محلی روی سرویس رایگان به‌عنوان تنها ذخیره‌سازی استفاده نکنید.
