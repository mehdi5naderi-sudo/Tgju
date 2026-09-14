package com.mehdi.tgjuwidget;

import android.app.PendingIntent;
import android.appwidget.AppWidgetManager;
import android.appwidget.AppWidgetProvider;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.os.Handler;
import android.os.Looper;
import android.widget.RemoteViews;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.text.DecimalFormat;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

public class TgjuWidgetProvider extends AppWidgetProvider {
    private static final String ACTION_REFRESH = "com.mehdi.tgjuwidget.REFRESH";
    private static final String API = "https://api.tgju.org/v1/widget/tmp?keys=crypto-tether-irr,price_dollar_rl,geram18,ons,oil_brent";
    private static final String[] KEYS = {"crypto-tether-irr", "price_dollar_rl", "geram18", "ons", "oil_brent"};
    private static final int[] PRICE_IDS = {R.id.price1,R.id.price2,R.id.price3,R.id.price4,R.id.price5};
    private static final int[] PCT_IDS = {R.id.pct1,R.id.pct2,R.id.pct3,R.id.pct4,R.id.pct5};
    private static final int[] TIME_IDS = {R.id.time1,R.id.time2,R.id.time3,R.id.time4,R.id.time5};

    @Override public void onUpdate(Context context, AppWidgetManager manager, int[] ids) {
        for (int id : ids) setClick(context, manager, id);
        refresh(context, ids);
    }

    @Override public void onReceive(Context context, Intent intent) {
        super.onReceive(context, intent);
        if (ACTION_REFRESH.equals(intent.getAction())) {
            int[] ids = AppWidgetManager.getInstance(context).getAppWidgetIds(new ComponentName(context, TgjuWidgetProvider.class));
            refresh(context, ids);
        }
    }

    private void setClick(Context context, AppWidgetManager manager, int id) {
        RemoteViews views = new RemoteViews(context.getPackageName(), R.layout.widget);
        Intent i = new Intent(context, TgjuWidgetProvider.class).setAction(ACTION_REFRESH);
        PendingIntent pi = PendingIntent.getBroadcast(context, id, i, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        views.setOnClickPendingIntent(R.id.root, pi);
        manager.updateAppWidget(id, views);
    }

    private void refresh(Context context, int[] ids) {
        if (ids == null || ids.length == 0) return;
        new Thread(() -> {
            Map<String, JSONObject> data = new HashMap<>();
            String requestTime = now();
            try {
                HttpURLConnection c = (HttpURLConnection) new URL(API).openConnection();
                c.setRequestMethod("GET"); c.setConnectTimeout(10000); c.setReadTimeout(10000); c.setUseCaches(false);
                BufferedReader br = new BufferedReader(new InputStreamReader(c.getInputStream()));
                StringBuilder sb = new StringBuilder(); String line;
                while ((line = br.readLine()) != null) sb.append(line);
                br.close(); c.disconnect();
                JSONObject root = new JSONObject(sb.toString());
                JSONArray arr = root.optJSONObject("response") == null ? null : root.optJSONObject("response").optJSONArray("indicators");
                if (arr != null) for (int i=0;i<arr.length();i++) {
                    JSONObject o = arr.optJSONObject(i); if (o == null) continue;
                    String key = o.optString("name", o.optString("key", o.optString("slug", "")));
                    data.put(key, o);
                }
            } catch (Exception ignored) { }
            final Map<String, JSONObject> result = data;
            new Handler(Looper.getMainLooper()).post(() -> {
                AppWidgetManager manager = AppWidgetManager.getInstance(context);
                for (int id : ids) update(context, manager, id, result, requestTime);
            });
        }).start();
    }

    private void update(Context context, AppWidgetManager manager, int id, Map<String, JSONObject> data, String requestTime) {
        RemoteViews v = new RemoteViews(context.getPackageName(), R.layout.widget);
        Intent i = new Intent(context, TgjuWidgetProvider.class).setAction(ACTION_REFRESH);
        PendingIntent pi = PendingIntent.getBroadcast(context, id, i, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        v.setOnClickPendingIntent(R.id.root, pi);
        for (int n=0;n<5;n++) {
            JSONObject o = data.get(KEYS[n]);
            String price = "—", pct = "—", time = "—";
            int color = Color.LTGRAY;
            if (o != null) {
                price = price(o, KEYS[n]);
                double p = o.optDouble("dp", Double.NaN);
                if (!Double.isNaN(p)) {
                    pct = percent(p);
                    color = p > 0 ? Color.rgb(85,200,120) : p < 0 ? Color.rgb(239,102,102) : Color.LTGRAY;
                }
                time = fa(o.optString("t", "—"));
            }
            v.setTextViewText(PRICE_IDS[n], fa(price));
            v.setTextViewText(PCT_IDS[n], fa(pct));
            v.setTextViewText(TIME_IDS[n], time);
            v.setTextColor(PCT_IDS[n], color);
        }
        v.setTextViewText(R.id.requestTime, "درخواست رفرش: " + fa(requestTime));
        manager.updateAppWidget(id, v);
    }

    private static String price(JSONObject o, String key) {
        try {
            double number;
            if ("ons".equals(key) || "oil_brent".equals(key)) number = Double.parseDouble(o.optString("p", "0").replace(",", ""));
            else if ("crypto-tether-irr".equals(key) && o.has("p_irr")) number = Double.parseDouble(o.optString("p_irr", "0").replace(",", "")) / 10.0;
            else number = Double.parseDouble(o.optString("p", "0").replace(",", "")) / 10.0;
            DecimalFormat f = new DecimalFormat(("ons".equals(key)||"oil_brent".equals(key)) ? "#,##0.00" : "#,##0");
            return f.format(number);
        } catch (Exception e) { return "—"; }
    }

    private static String percent(double p) { return String.format(Locale.US, "%.2f%%", Math.abs(p)); }
    private static String now() { return new java.text.SimpleDateFormat("HH:mm", Locale.US).format(new java.util.Date()); }
    private static String fa(String s) { return s == null ? "—" : s.replace('0','۰').replace('1','۱').replace('2','۲').replace('3','۳').replace('4','۴').replace('5','۵').replace('6','۶').replace('7','۷').replace('8','۸').replace('9','۹'); }
}
