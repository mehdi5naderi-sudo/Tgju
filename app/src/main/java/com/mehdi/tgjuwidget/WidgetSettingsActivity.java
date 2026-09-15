package com.mehdi.tgjuwidget;

import android.app.Activity;
import android.appwidget.AppWidgetManager;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.os.Bundle;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.Switch;
import android.widget.TextView;

import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

public class WidgetSettingsActivity extends Activity {
    private static final String[] KEYS = {"crypto-tether-irr", "price_dollar_rl", "geram18", "ons", "oil_brent", "ime_fund_kahroba", "ime_fund_ayar"};
    private static final String[] NAMES = {"تتر", "دلار", "گرم ۱۸", "انس", "نفت برنت", "کهربا", "عیار"};
    private int widgetId = AppWidgetManager.INVALID_APPWIDGET_ID;
    private boolean launchedFromIcon = false;
    private Spinner[] spinners = new Spinner[5];
    private EditText priceSize, pctSize, timeSize, refreshSize, rowSpace, padding, bgColor, mutedColor;
    private Spinner language, dateFormat;
    private Switch showPct, showTime, showRefresh;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        widgetId = getIntent().getIntExtra(AppWidgetManager.EXTRA_APPWIDGET_ID, AppWidgetManager.INVALID_APPWIDGET_ID);
        if (widgetId == AppWidgetManager.INVALID_APPWIDGET_ID) {
            launchedFromIcon = true;
            int[] ids = getWidgetIds();
            widgetId = ids.length > 0 ? ids[0] : 0;
        }
        setResult(RESULT_CANCELED);
        buildUi();
        load();
    }

    private int[] getWidgetIds() {
        AppWidgetManager manager = AppWidgetManager.getInstance(this);
        ComponentName provider = new ComponentName(this, TgjuWidgetProvider.class);
        return manager.getAppWidgetIds(provider);
    }

    private void buildUi() {
        ScrollView scroll = new ScrollView(this);
        LinearLayout root = new LinearLayout(this); root.setOrientation(LinearLayout.VERTICAL); root.setPadding(dp(16),dp(12),dp(16),dp(20));
        scroll.addView(root);
        TextView title = label("تنظیمات ویجت TGJU"); title.setTextSize(22); root.addView(title, lp());
        root.addView(label("شاخص‌ها و ترتیب"), lpTop());
        ArrayAdapter<String> adapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, NAMES);
        for (int i=0;i<5;i++) { spinners[i] = new Spinner(this); spinners[i].setAdapter(adapter); root.addView(spinners[i], lp()); }
        root.addView(label("اندازه فونت (sp)"), lpTop());
        priceSize = field(root,"مبلغ", "20"); pctSize = field(root,"درصد", "12"); timeSize = field(root,"ساعت/تاریخ", "10"); refreshSize = field(root,"متن رفرش", "8");
        root.addView(label("زبان کل ویجت"), lpTop());
        language = spinner(root, new String[]{"فارسی", "English"});
        root.addView(label("فرمت تاریخ بدون ساعت"), lpTop());
        dateFormat = spinner(root, new String[]{"23/06", "23 - 06", "23.06", "23/06/1405", "23 شهریور", "مخفی"});
        root.addView(label("نمایش اطلاعات"), lpTop());
        showPct = sw(root,"نمایش درصد تغییر",true); showTime = sw(root,"نمایش ساعت/تاریخ",true); showRefresh = sw(root,"نمایش زمان رفرش",true);
        root.addView(label("ظاهر"), lpTop());
        bgColor = field(root,"رنگ پس‌زمینه (HEX)", "#000000"); mutedColor = field(root,"رنگ متن ساعت/رفرش (HEX)", "#AAAAAA");
        rowSpace = field(root,"فاصله ردیف‌ها (dp)", "0"); padding = field(root,"فاصله داخلی ویجت (dp)", "5");
        root.addView(label("رنگ افزایش/کاهش ثابت است: سبز، قرمز، زرد"), lpTop());
        Button save = new Button(this); save.setText("ذخیره"); save.setOnClickListener(v -> save()); root.addView(save, lpTop());
        setContentView(scroll);
    }

    private Spinner spinner(LinearLayout root, String[] values) { Spinner s=new Spinner(this); s.setAdapter(new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,values)); root.addView(s,lp()); return s; }
    private Switch sw(LinearLayout root,String text,boolean val){ Switch s=new Switch(this); s.setText(text); s.setChecked(val); root.addView(s,lp()); return s; }
    private EditText field(LinearLayout root,String hint,String val){ TextView l=label(hint); root.addView(l,lp()); EditText e=new EditText(this); e.setSingleLine(true); e.setText(val); e.setSelectAllOnFocus(true); root.addView(e,lp()); return e; }
    private TextView label(String s){ TextView t=new TextView(this); t.setText(s); t.setTextSize(15); t.setTextColor(Color.DKGRAY); return t; }
    private LinearLayout.LayoutParams lp(){return new LinearLayout.LayoutParams(-1,-2);}
    private LinearLayout.LayoutParams lpTop(){LinearLayout.LayoutParams p=lp();p.topMargin=dp(12);return p;}
    private int dp(int v){return (int)(v*getResources().getDisplayMetrics().density+0.5f);}

    private String hexColor(android.content.SharedPreferences p, String key, int def) {
        String fallback = String.format(Locale.US, "#%06X", def & 0xFFFFFF);
        try {
            int value = p.getInt(key, def);
            return String.format(Locale.US, "#%06X", value & 0xFFFFFF);
        } catch (ClassCastException e) {
            try { return p.getString(key, fallback); } catch (Exception ignored) { return fallback; }
        }
    }

    private void load(){
        android.content.SharedPreferences p=getSharedPreferences("widget_"+widgetId,Context.MODE_PRIVATE);
        Map<String,Integer> idx=new HashMap<>(); for(int i=0;i<KEYS.length;i++)idx.put(KEYS[i],i);
        for(int i=0;i<5;i++) spinners[i].setSelection(idx.containsKey(p.getString("key"+i,KEYS[i]))?idx.get(p.getString("key"+i,KEYS[i])):i);
        priceSize.setText(String.valueOf(p.getInt("priceSize",20))); pctSize.setText(String.valueOf(p.getInt("pctSize",12))); timeSize.setText(String.valueOf(p.getInt("timeSize",10))); refreshSize.setText(String.valueOf(p.getInt("refreshSize",8)));
        language.setSelection(p.getString("lang","fa").equals("en")?1:0); dateFormat.setSelection(p.getInt("dateFormat",0));
        showPct.setChecked(p.getBoolean("showPct",true)); showTime.setChecked(p.getBoolean("showTime",true)); showRefresh.setChecked(p.getBoolean("showRefresh",true));
        bgColor.setText(hexColor(p, "bgColor", Color.BLACK)); mutedColor.setText(hexColor(p, "mutedColor", Color.LTGRAY)); rowSpace.setText(String.valueOf(p.getInt("rowSpace",0))); padding.setText(String.valueOf(p.getInt("padding",5)));
    }

    private int num(EditText e,int def,int min,int max){try{return Math.max(min,Math.min(max,Integer.parseInt(e.getText().toString().trim())));}catch(Exception x){return def;}}
    private int color(String s,int def){try{return Color.parseColor(s.trim());}catch(Exception e){return def;}}

    private void save(){
        android.content.SharedPreferences.Editor e=getSharedPreferences("widget_"+widgetId,Context.MODE_PRIVATE).edit();
        for(int i=0;i<5;i++) e.putString("key"+i,KEYS[spinners[i].getSelectedItemPosition()]);
        e.putInt("priceSize",num(priceSize,20,8,40)).putInt("pctSize",num(pctSize,12,6,24)).putInt("timeSize",num(timeSize,10,6,20)).putInt("refreshSize",num(refreshSize,8,5,18));
        e.putString("lang",language.getSelectedItemPosition()==1?"en":"fa").putInt("dateFormat",dateFormat.getSelectedItemPosition());
        e.putBoolean("showPct",showPct.isChecked()).putBoolean("showTime",showTime.isChecked()).putBoolean("showRefresh",showRefresh.isChecked());
        e.putInt("bgColor",color(bgColor.getText().toString(),Color.BLACK)).putInt("mutedColor",color(mutedColor.getText().toString(),Color.LTGRAY));
        e.putInt("rowSpace",num(rowSpace,0,0,12)).putInt("padding",num(padding,5,0,20)).apply();

        AppWidgetManager manager = AppWidgetManager.getInstance(this);
        if (launchedFromIcon) {
            // The launcher icon has no widget ID. Apply these settings to every existing widget.
            int[] ids = getWidgetIds();
            for (int id : ids) {
                if (id != widgetId) copySettings(widgetId, id);
                manager.updateAppWidget(id, TgjuWidgetProvider.buildViews(this, id));
            }
        } else if (widgetId != 0) {
            manager.updateAppWidget(widgetId,TgjuWidgetProvider.buildViews(this,widgetId));
        }
        Intent result=new Intent(); result.putExtra(AppWidgetManager.EXTRA_APPWIDGET_ID,widgetId); setResult(RESULT_OK,result); finish();
    }

    private void copySettings(int fromId, int toId) {
        android.content.SharedPreferences from = getSharedPreferences("widget_"+fromId, Context.MODE_PRIVATE);
        android.content.SharedPreferences.Editor to = getSharedPreferences("widget_"+toId, Context.MODE_PRIVATE).edit();
        for (int i=0;i<5;i++) to.putString("key"+i, from.getString("key"+i, KEYS[i]));
        to.putInt("priceSize", from.getInt("priceSize",20)).putInt("pctSize", from.getInt("pctSize",12)).putInt("timeSize", from.getInt("timeSize",10)).putInt("refreshSize", from.getInt("refreshSize",8));
        to.putString("lang", from.getString("lang","fa")).putInt("dateFormat", from.getInt("dateFormat",0));
        to.putBoolean("showPct", from.getBoolean("showPct",true)).putBoolean("showTime", from.getBoolean("showTime",true)).putBoolean("showRefresh", from.getBoolean("showRefresh",true));
        to.putInt("bgColor", from.getInt("bgColor",Color.BLACK)).putInt("mutedColor", from.getInt("mutedColor",Color.LTGRAY));
        to.putInt("rowSpace", from.getInt("rowSpace",0)).putInt("padding", from.getInt("padding",5)).apply();
    }
}
