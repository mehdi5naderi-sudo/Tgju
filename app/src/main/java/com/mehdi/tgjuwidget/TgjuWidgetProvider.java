package com.mehdi.tgjuwidget;

import android.app.PendingIntent;
import android.appwidget.AppWidgetManager;
import android.appwidget.AppWidgetProvider;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
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
    private static final int SLOT_COUNT = 9;
    private static final String[] DEFAULT_KEYS = {
            "crypto-tether-irr","price_dollar_rl","geram18","ime_fund_kahroba",
            "ime_fund_ayar","ons","oil_brent","gc30","sekee","crypto-bitcoin"
    };
    private static final int[] PRICE_IDS = {R.id.price1,R.id.price2,R.id.price3,R.id.price4,R.id.price5,R.id.price6,R.id.price7,R.id.price8,R.id.price9};
    private static final int[] PRICE_INLINE_IDS = {R.id.priceInline1,R.id.priceInline2,R.id.priceInline3,R.id.priceInline4,R.id.priceInline5,R.id.priceInline6,R.id.priceInline7,R.id.priceInline8,R.id.priceInline9};
    private static final int[] PRICE_VERTICAL_IDS = {R.id.priceVertical1,R.id.priceVertical2,R.id.priceVertical3,R.id.priceVertical4,R.id.priceVertical5,R.id.priceVertical6,R.id.priceVertical7,R.id.priceVertical8,R.id.priceVertical9};
    private static final int[] PRICE_RIGHT_IDS = {R.id.priceRight1,R.id.priceRight2,R.id.priceRight3,R.id.priceRight4,R.id.priceRight5,R.id.priceRight6,R.id.priceRight7,R.id.priceRight8,R.id.priceRight9};
    private static final int[] NAME_IDS = {R.id.name1,R.id.name2,R.id.name3,R.id.name4,R.id.name5,R.id.name6,R.id.name7,R.id.name8,R.id.name9};
    private static final int[] NAME_INLINE_IDS = {R.id.nameInline1,R.id.nameInline2,R.id.nameInline3,R.id.nameInline4,R.id.nameInline5,R.id.nameInline6,R.id.nameInline7,R.id.nameInline8,R.id.nameInline9};
    private static final int[] NAME_VERTICAL_IDS = {R.id.nameVertical1,R.id.nameVertical2,R.id.nameVertical3,R.id.nameVertical4,R.id.nameVertical5,R.id.nameVertical6,R.id.nameVertical7,R.id.nameVertical8,R.id.nameVertical9};
    private static final int[] NAME_RIGHT_IDS = {R.id.nameRight1,R.id.nameRight2,R.id.nameRight3,R.id.nameRight4,R.id.nameRight5,R.id.nameRight6,R.id.nameRight7,R.id.nameRight8,R.id.nameRight9};
    private static final int[] PCT_IDS = {R.id.pct1,R.id.pct2,R.id.pct3,R.id.pct4,R.id.pct5,R.id.pct6,R.id.pct7,R.id.pct8,R.id.pct9};
    private static final int[] TIME_IDS = {R.id.time1,R.id.time2,R.id.time3,R.id.time4,R.id.time5,R.id.time6,R.id.time7,R.id.time8,R.id.time9};
    private static final int[] META_IDS = {R.id.meta1,R.id.meta2,R.id.meta3,R.id.meta4,R.id.meta5,R.id.meta6,R.id.meta7,R.id.meta8,R.id.meta9};
    private static final int[] ROW_IDS = {R.id.row1,R.id.row2,R.id.row3,R.id.row4,R.id.row5,R.id.row6,R.id.row7,R.id.row8,R.id.row9};
    private static final int REFRESH_PROGRESS_ID = R.id.refreshProgress;
    private static final int GREEN=Color.rgb(85,200,120), RED=Color.rgb(239,102,102), YELLOW=Color.rgb(229,192,74);

    @Override public void onUpdate(Context c,AppWidgetManager m,int[] ids){
        for(int id:ids){m.updateAppWidget(id,buildViews(c,id));refresh(c,new int[]{id});}
    }

    @Override public void onReceive(Context c,Intent i){
        super.onReceive(c,i);
        if(ACTION_REFRESH.equals(i.getAction())){
            int[] ids=AppWidgetManager.getInstance(c).getAppWidgetIds(new ComponentName(c,TgjuWidgetProvider.class));
            refresh(c,ids);
        }
    }

    public static RemoteViews buildViews(Context c,int id){
        RemoteViews v=new RemoteViews(c.getPackageName(),R.layout.widget);
        android.content.SharedPreferences p=c.getSharedPreferences("widget_"+id,Context.MODE_PRIVATE);
        int bgBase=p.getInt("bgColor",Color.BLACK), bgAlpha=p.getInt("bgAlpha",100), metaGap=p.getInt("metaGap",0), bg=Color.argb((int)(Math.max(0,Math.min(100,bgAlpha))*2.55f),Color.red(bgBase),Color.green(bgBase),Color.blue(bgBase)), muted=p.getInt("mutedColor",Color.LTGRAY), pad=p.getInt("padding",4), gap=p.getInt("rowSpace",0);
        float ps=p.getInt("priceSize",18), pct=p.getInt("pctSize",10), ts=p.getInt("timeSize",8), rs=p.getInt("refreshSize",7), ns=p.getInt("nameSize",8);
        boolean showPct=p.getBoolean("showPct",true), showTime=p.getBoolean("showTime",true), showRefresh=p.getBoolean("showRefresh",true), showNames=p.getBoolean("showNames",true);
        int metaWidth=p.getInt("metaWidth",40);
        int namePosition=p.getInt("namePosition",1);
        int selectedCount=0; for(int i=0;i<SLOT_COUNT;i++) if(!p.getString("key"+i,i<5?DEFAULT_KEYS[i]:"").isEmpty()) selectedCount++;
        boolean scrollMode=selectedCount>6;
        v.setInt(R.id.root,"setBackgroundColor",bg); v.setViewPadding(R.id.root,pad,pad,pad,pad);
        v.setViewVisibility(R.id.rowsStatic,scrollMode?View.GONE:View.VISIBLE);
        v.setViewVisibility(R.id.rowsList,scrollMode?View.VISIBLE:View.GONE);
        if(android.os.Build.VERSION.SDK_INT>=31){
            for(int i=0;i<SLOT_COUNT;i++) v.setViewLayoutHeight(ROW_IDS[i],scrollMode?30:0,android.util.TypedValue.COMPLEX_UNIT_DIP);
        }
        for(int i=0;i<SLOT_COUNT;i++){
            String selectedKey=p.getString("key"+i,i<5?DEFAULT_KEYS[i]:"");
            v.setViewVisibility(ROW_IDS[i],selectedKey.isEmpty()?View.GONE:View.VISIBLE);
            v.setTextViewTextSize(PRICE_IDS[i],2,ps);
            v.setTextViewTextSize(PRICE_INLINE_IDS[i],2,ps);
            v.setTextViewTextSize(PRICE_VERTICAL_IDS[i],2,ps);
            v.setTextViewTextSize(PRICE_RIGHT_IDS[i],2,ps);
            v.setTextViewTextSize(NAME_IDS[i],2,ns);
            v.setTextViewTextSize(NAME_INLINE_IDS[i],2,ns);
            v.setTextViewTextSize(NAME_VERTICAL_IDS[i],2,ns);
            v.setTextViewTextSize(NAME_RIGHT_IDS[i],2,ns);
            v.setTextViewTextSize(PCT_IDS[i],2,pct);
            v.setTextViewTextSize(TIME_IDS[i],2,ts);
            if (android.os.Build.VERSION.SDK_INT >= 31) v.setViewLayoutWidth(META_IDS[i], metaWidth, android.util.TypedValue.COMPLEX_UNIT_DIP);
            if(gap>0)v.setViewLayoutMargin(ROW_IDS[i],RemoteViews.MARGIN_BOTTOM,gap,android.util.TypedValue.COMPLEX_UNIT_DIP);
            if(metaGap!=0)v.setViewLayoutMargin(PCT_IDS[i],RemoteViews.MARGIN_BOTTOM,metaGap,android.util.TypedValue.COMPLEX_UNIT_DIP);
            v.setTextColor(NAME_IDS[i],muted); v.setTextColor(NAME_RIGHT_IDS[i],muted); v.setTextColor(TIME_IDS[i],muted);
            v.setViewVisibility(NAME_IDS[i],View.GONE);
            v.setViewVisibility(NAME_INLINE_IDS[i],showNames&&namePosition==1?View.VISIBLE:View.GONE);
            v.setViewVisibility(NAME_VERTICAL_IDS[i],showNames&&namePosition==2?View.VISIBLE:View.GONE);
            v.setViewVisibility(NAME_RIGHT_IDS[i],showNames&&namePosition==3?View.VISIBLE:View.GONE);
            v.setViewVisibility(PRICE_IDS[i],View.GONE);
            v.setViewVisibility(PRICE_INLINE_IDS[i],namePosition==1?View.VISIBLE:View.GONE);
            v.setViewVisibility(PRICE_VERTICAL_IDS[i],namePosition==2?View.VISIBLE:View.GONE);
            v.setViewVisibility(PRICE_RIGHT_IDS[i],namePosition==3?View.VISIBLE:View.GONE);
            v.setViewVisibility(PCT_IDS[i],showPct?View.VISIBLE:View.GONE);
            v.setViewVisibility(TIME_IDS[i],showTime?View.VISIBLE:View.GONE);
        }
        // Up to six rows fill the available height. More than six rows scroll internally.
        // The layout itself defines the three columns for right-title mode:
        // value | percentage/time | fixed-width title.
        for(int i=0;i<SLOT_COUNT;i++){
            v.setInt(ROW_IDS[i],"setLayoutDirection",View.LAYOUT_DIRECTION_LTR);
            v.setInt(META_IDS[i],"setGravity",android.view.Gravity.CENTER_VERTICAL|android.view.Gravity.RIGHT);
            v.setInt(PCT_IDS[i],"setGravity",android.view.Gravity.RIGHT);
            v.setInt(NAME_RIGHT_IDS[i],"setGravity",android.view.Gravity.RIGHT|android.view.Gravity.CENTER_VERTICAL);
            v.setInt(TIME_IDS[i],"setGravity",android.view.Gravity.END);
        }
        v.setTextViewTextSize(R.id.requestTime,2,rs);
        v.setTextColor(R.id.requestTime,muted);
        v.setViewVisibility(R.id.requestTime,showRefresh?View.VISIBLE:View.GONE);

        Intent r=new Intent(c,TgjuWidgetProvider.class).setAction(ACTION_REFRESH).setData(Uri.parse("tgju://refresh/"+id));
        PendingIntent rp=PendingIntent.getBroadcast(c,id,r,PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE);
        v.setOnClickPendingIntent(R.id.root,rp);
        // In scroll mode the ListView consumes taps, so route row taps to the same refresh action.
        v.setPendingIntentTemplate(R.id.rowsList,rp);

        Intent s=new Intent(c,WidgetSettingsActivity.class).setAction("com.mehdi.tgjuwidget.SETTINGS").setData(Uri.parse("tgju://settings/"+id)).putExtra(AppWidgetManager.EXTRA_APPWIDGET_ID,id);
        PendingIntent sp=PendingIntent.getActivity(c,id+10000,s,PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE);
        v.setOnClickPendingIntent(R.id.settingsButton,sp);
        // The bottom timestamp opens settings; it must not inherit the root refresh action.
        v.setOnClickPendingIntent(R.id.requestTime,sp);
        v.setTextViewText(R.id.settingsButton,"⚙");
        setListItems(c,v,id,null);
        return v;
    }

    private void refresh(Context c,int[] ids){
        if(ids==null||ids.length==0)return;
        // Show an immediate, unmistakable loading state before the network request starts.
        AppWidgetManager manager=AppWidgetManager.getInstance(c);
        for(int id:ids){
            RemoteViews loading=buildViews(c,id);
            android.content.SharedPreferences prefs=c.getSharedPreferences("widget_"+id,Context.MODE_PRIVATE);
            boolean en="en".equals(prefs.getString("lang","fa"));
            loading.setViewVisibility(REFRESH_PROGRESS_ID,View.VISIBLE);
            loading.setTextViewText(R.id.requestTime,en?"⟳ Refreshing...":"⟳ در حال بروزرسانی...");
            loading.setTextColor(R.id.requestTime,Color.rgb(77,208,225));
            loading.setTextViewTextSize(R.id.requestTime,2,8);
            manager.updateAppWidget(id,loading);
        }
        new Thread(()->{
            android.content.SharedPreferences p=c.getSharedPreferences("widget_"+ids[0],Context.MODE_PRIVATE);
            String[] keys=new String[SLOT_COUNT];
            for(int i=0;i<SLOT_COUNT;i++)keys[i]=p.getString("key"+i,i<5?DEFAULT_KEYS[i]:"");
            Map<String,JSONObject> data=new HashMap<>();
            boolean success=false;
            String requestTime=now();
            try{
                success=fetchCurrent(keys,data);
            }catch(Exception ignored){}
            final boolean ok=success;
            new Handler(Looper.getMainLooper()).post(()->{
                AppWidgetManager m=AppWidgetManager.getInstance(c);
                for(int id:ids)update(c,m,id,data,requestTime,ok);
            });
        }).start();
    }

    private static boolean fetchCurrent(String[] keys,Map<String,JSONObject> data){
        // Keep the original TGJU source for the existing selections.
        HttpURLConnection con=null;
        try{
            URL url=new URL("https://call1.tgju.org/ajax.json?_="+System.currentTimeMillis());
            con=(HttpURLConnection)url.openConnection();
            con.setRequestMethod("GET");
            con.setConnectTimeout(10000);
            con.setReadTimeout(10000);
            con.setUseCaches(false);
            con.setRequestProperty("Accept","application/json");
            con.setRequestProperty("Cache-Control","no-cache, no-store, max-age=0");
            con.setRequestProperty("Pragma","no-cache");
            if(con.getResponseCode()==200){
                JSONObject root=new JSONObject(read(con));
                JSONObject current=root.optJSONObject("current");
                if(current!=null){
                    for(String key:keys){
                        if(key==null||key.isEmpty()||isTsetmcKey(key))continue;
                        JSONObject item=current.optJSONObject(key);
                        if(item!=null)data.put(key,item);
                    }
                }
            }
        }catch(Exception ignored){}finally{if(con!=null)con.disconnect();}

        // New alternatives use TSETMC; old TGJU selections remain unchanged.
        for(String key:keys){
            if(!isTsetmcKey(key))continue;
            try{
                JSONObject item;
                if("tsetmc_index_total".equals(key)){
                    item=fetchTsetmcIndex();
                }else{
                    String insCode="ime_fund_kahroba_tsetmc".equals(key)
                            ?"25559236668122210":"34144395039913458";
                    item=fetchTsetmcPrice(insCode);
                }
                if(item!=null)data.put(key,item);
            }catch(Exception ignored){}
        }
        return hasAny(keys,data);
    }

    private static boolean isTsetmcKey(String key){
        return "ime_fund_kahroba_tsetmc".equals(key)
                ||"ime_fund_ayar_tsetmc".equals(key)
                ||"tsetmc_index_total".equals(key);
    }

    private static JSONObject fetchTsetmcIndex()throws Exception{
        HttpURLConnection con=null;
        try{
            URL url=new URL("https://cdn.tsetmc.com/api/Index/GetIndexB1LastAll/SelectedIndexes/1?_="+System.currentTimeMillis());
            con=(HttpURLConnection)url.openConnection();
            con.setRequestMethod("GET");
            con.setConnectTimeout(15000);
            con.setReadTimeout(15000);
            con.setUseCaches(false);
            con.setInstanceFollowRedirects(true);
            con.setRequestProperty("Accept","application/json, text/plain, */*");
            con.setRequestProperty("User-Agent","Mozilla/5.0 (Linux; Android 13) AppleWebKit/537.36 Chrome/120.0.0.0 Mobile Safari/537.36");
            con.setRequestProperty("Referer","https://www.tsetmc.com/");
            con.setRequestProperty("Origin","https://www.tsetmc.com");
            con.setRequestProperty("Cache-Control","no-cache");
            if(con.getResponseCode()!=200)return null;
            JSONObject response=new JSONObject(read(con));
            JSONArray indexes=response.optJSONArray("indexB1");
            if(indexes==null)return null;
            for(int i=0;i<indexes.length();i++){
                JSONObject root=indexes.optJSONObject(i);
                if(root==null||!"32097828799138957".equals(root.optString("insCode","")))continue;
                double value=root.optDouble("xDrNivJIdx004",0);
                if(value<=0)return null;
                double change=root.optDouble("indexChange",0);
                double pct=root.optDouble("xVarIdxJRfV",0);
                JSONObject item=new JSONObject();
                item.put("p",String.valueOf(value));
                item.put("dp",Math.abs(pct));
                item.put("dt",change>0?"high":change<0?"low":"");
                int rawTime=root.optInt("hEven",0);
                String clock=String.format(Locale.US,"%06d",rawTime);
                item.put("t",clock.substring(0,2)+":"+clock.substring(2,4)+":"+clock.substring(4,6));
                item.put("indexChange",change);
                item.put("dEven",root.optInt("dEven",0));
                return item;
            }
            return null;
        }finally{if(con!=null)con.disconnect();}
    }

    private static JSONObject fetchTsetmcPrice(String insCode)throws Exception{
        HttpURLConnection con=null;
        try{
            URL url=new URL("https://cdn.tsetmc.com/api/ClosingPrice/GetClosingPriceInfo/"+insCode);
            con=(HttpURLConnection)url.openConnection();
            con.setRequestMethod("GET");
            con.setConnectTimeout(15000);
            con.setReadTimeout(15000);
            con.setUseCaches(false);
            con.setInstanceFollowRedirects(true);
            con.setRequestProperty("Accept","application/json, text/plain, */*");
            con.setRequestProperty("User-Agent","Mozilla/5.0 (Linux; Android 13) AppleWebKit/537.36 Chrome/120.0.0.0 Mobile Safari/537.36");
            con.setRequestProperty("Referer","https://www.tsetmc.com/");
            con.setRequestProperty("Origin","https://www.tsetmc.com");
            con.setRequestProperty("Cache-Control","no-cache");
            if(con.getResponseCode()!=200)return null;
            JSONObject response=new JSONObject(read(con));
            JSONObject root=response.optJSONObject("closingPriceInfo");
            if(root==null)root=response;
            long last=Math.round(root.optDouble("pDrCotVal",0));
            long yesterday=Math.round(root.optDouble("priceYesterday",0));
            if(last<=0)return null;
            double change=yesterday>0?((last-yesterday)*100.0/yesterday):0.0;
            JSONObject item=new JSONObject();
            item.put("p",String.valueOf(last));
            item.put("dp",Math.abs(change));
            item.put("dt",change>0?"high":change<0?"low":"");
            int day=root.optInt("dEven",0);
            int lastTradeDay=root.optInt("finalLastDate",0);
            boolean validDates=validTsetmcDate(day)&&validTsetmcDate(lastTradeDay);
            item.put("dEven",day);
            item.put("finalLastDate",lastTradeDay);
            item.put("stale",validDates&&day!=lastTradeDay);
            int rawTime=root.optInt("lastHEven",root.optInt("hEven",0));
            String clock=String.format(Locale.US,"%06d",rawTime);
            item.put("t",clock.substring(0,2)+":"+clock.substring(2,4)+":"+clock.substring(4,6));
            return item;
        }finally{if(con!=null)con.disconnect();}
    }

    private static String read(HttpURLConnection con)throws Exception{
        BufferedReader br=new BufferedReader(new InputStreamReader(con.getInputStream()));
        StringBuilder sb=new StringBuilder(); String line;
        while((line=br.readLine())!=null)sb.append(line);
        br.close(); return sb.toString();
    }

    private static boolean hasAny(String[] keys,Map<String,JSONObject> data){
        for(String k:keys)if(data.containsKey(k))return true;
        return false;
    }

    private void update(Context c,AppWidgetManager m,int id,Map<String,JSONObject>d,String request,boolean ok){
        RemoteViews v=buildViews(c,id);
        android.content.SharedPreferences p=c.getSharedPreferences("widget_"+id,Context.MODE_PRIVATE);
        boolean en="en".equals(p.getString("lang","fa"));
        boolean showNames=p.getBoolean("showNames",true);
        for(int i=0;i<SLOT_COUNT;i++){
            String k=p.getString("key"+i,i<5?DEFAULT_KEYS[i]:""); JSONObject o=k.isEmpty()?null:d.get(k);
            String price="—",pct="—",time="—"; int color=Color.LTGRAY; int timeTextColor=Color.LTGRAY;
            if(o!=null){
                price=price(o,k); double dp=o.optDouble("dp",Double.NaN); String dt=o.optString("dt","");
                if(!Double.isNaN(dp)){pct=percent(dp,en);color="high".equalsIgnoreCase(dt)?GREEN:"low".equalsIgnoreCase(dt)?RED:YELLOW;}
                boolean stale=isTsetmcKey(k)&&o.optBoolean("stale",false);
                if(stale)color=YELLOW;
                String rawTime=o.optString("t","—");
                time=formatTimeOrDate(rawTime,p.getInt("dateFormat",0),p.getInt("dateOrder",0),en);
                timeTextColor=stale?YELLOW:timeColor(rawTime,color);
                p.edit().putString("lastPrice"+i,price).putString("lastPct"+i,pct).putString("lastTime"+i,time)
                        .putInt("lastColor"+i,color).putInt("lastTimeColor"+i,timeTextColor).apply();
            } else {
                price=p.getString("lastPrice"+i,"—");
                pct=p.getString("lastPct"+i,"—");
                time=p.getString("lastTime"+i,"—");
                color=p.getInt("lastColor"+i,Color.LTGRAY);
                timeTextColor=p.getInt("lastTimeColor"+i,color);
            }
            String name=showNames&&!k.isEmpty()?nameFor(k,en):"";
            v.setTextViewText(NAME_IDS[i],name);
            v.setTextViewText(NAME_INLINE_IDS[i],name);
            v.setTextViewText(NAME_VERTICAL_IDS[i],name);
            v.setTextViewText(NAME_RIGHT_IDS[i],name);
            int nameColor=o!=null&&isTsetmcKey(k)&&o.optBoolean("stale",false)?YELLOW:p.getInt("mutedColor",Color.LTGRAY);
            v.setTextColor(NAME_IDS[i],nameColor);
            v.setTextColor(NAME_INLINE_IDS[i],nameColor);
            v.setTextColor(NAME_VERTICAL_IDS[i],nameColor);
            v.setTextColor(NAME_RIGHT_IDS[i],nameColor);
            v.setTextViewText(PRICE_IDS[i],digits(price,en)); v.setTextColor(PRICE_IDS[i],color);
            v.setTextViewText(PRICE_INLINE_IDS[i],digits(price,en)); v.setTextColor(PRICE_INLINE_IDS[i],color);
            v.setTextViewText(PRICE_VERTICAL_IDS[i],digits(price,en)); v.setTextColor(PRICE_VERTICAL_IDS[i],color);
            v.setTextViewText(PRICE_RIGHT_IDS[i],digits(price,en)); v.setTextColor(PRICE_RIGHT_IDS[i],color);
            v.setTextViewText(PCT_IDS[i],digits(pct,en)); v.setTextColor(PCT_IDS[i],timeTextColor);
            v.setTextViewText(TIME_IDS[i],digits(time,en)); v.setTextColor(TIME_IDS[i],timeTextColor);
        }
        v.setViewVisibility(REFRESH_PROGRESS_ID,View.GONE);
        String refreshLabel=request;
        if(ok){
            for(int i=0;i<SLOT_COUNT;i++){
                String key=p.getString("key"+i,i<5?DEFAULT_KEYS[i]:"");
                JSONObject item=d.get(key);
                if(isTsetmcKey(key)&&item!=null&&item.optBoolean("stale",false)){
                    String tradeDate=item.optString("finalLastDate","");
                    String formatted=formatTsetmcDate(tradeDate,p.getInt("dateFormat",0),p.getInt("dateOrder",0));
                    if(!formatted.isEmpty())refreshLabel=formatted;
                    else refreshLabel="";
                    break;
                }
            }
        }
        v.setTextViewText(R.id.requestTime,ok?digits(refreshLabel,en):(en?"Update failed":"خطا در بروزرسانی"));
        v.setTextColor(R.id.requestTime,ok?p.getInt("mutedColor",Color.LTGRAY):RED);
        v.setTextViewTextSize(R.id.requestTime,2,ok?p.getInt("refreshSize",7):8);
        setListItems(c,v,id,d);
        m.updateAppWidget(id,v);
    }

    private static String nameFor(String k,boolean en){
        if(en){
            if("crypto-tether-irr".equals(k))return "Tether";
            if("price_dollar_rl".equals(k))return "Dollar";
            if("geram18".equals(k))return "Gold 18";
            if("ime_fund_kahroba".equals(k)||"ime_fund_kahroba_tsetmc".equals(k))return "Kahroba";
            if("ime_fund_ayar".equals(k)||"ime_fund_ayar_tsetmc".equals(k))return "Ayar";
            if("ons".equals(k))return "Ounce";
            if("oil_brent".equals(k))return "Brent";
            if("gc30".equals(k))return "gc30";
            if("sekee".equals(k))return "Imami";
            if("crypto-bitcoin".equals(k))return "Bitcoin";
            if("tsetmc_index_total".equals(k))return "Total Index";
        }
        if("crypto-tether-irr".equals(k))return "تتر";
        if("price_dollar_rl".equals(k))return "دلار";
        if("geram18".equals(k))return "گرم ۱۸";
        if("ime_fund_kahroba".equals(k))return "کهربا";
        if("ime_fund_kahroba_tsetmc".equals(k))return "کهربا (TSETMC)";
        if("ime_fund_ayar".equals(k))return "عیار";
        if("ime_fund_ayar_tsetmc".equals(k))return "عیار (TSETMC)";
        if("ons".equals(k))return "انس";
        if("oil_brent".equals(k))return "برنت";
        if("gc30".equals(k))return "بورس";
        if("sekee".equals(k))return "امامی";
        if("crypto-bitcoin".equals(k))return "بیت‌کوین";
        if("tsetmc_index_total".equals(k))return "شاخص کل بورس";
        return k;
    }

    private static String price(JSONObject o,String k){
        try{
            double n;
            if("ons".equals(k)||"oil_brent".equals(k)||"gc30".equals(k)||"crypto-bitcoin".equals(k)||"tsetmc_index_total".equals(k))
                n=Double.parseDouble(o.optString("p","0").replace(",",""));
            else
                n=Double.parseDouble(o.optString("p","0").replace(",",""))/10.0;
            if("tsetmc_index_total".equals(k))return new DecimalFormat("#,##0").format(n);
            return new DecimalFormat("#,##0").format(n);
        }catch(Exception e){return "—";}
    }

    private static String percent(double p,boolean en){return String.format(Locale.US,"%.2f%%",Math.abs(p));}
    private static String now(){return new java.text.SimpleDateFormat("HH:mm",Locale.US).format(new java.util.Date());}

    private static String digits(String s,boolean en){
        if(s==null)return "—";
        if(en)return s.replace('۰','0').replace('۱','1').replace('۲','2').replace('۳','3').replace('۴','4').replace('۵','5').replace('۶','6').replace('۷','7').replace('۸','8').replace('۹','9').replace('٬',',').replace('٫','.').replace('٪','%');
        return s.replace('0','۰').replace('1','۱').replace('2','۲').replace('3','۳').replace('4','۴').replace('5','۵').replace('6','۶').replace('7','۷').replace('8','۸').replace('9','۹').replace(',', '٬').replace('.', '٫').replace('%','٪');
    }

    private static int timeColor(String rawTime,int changeColor){
        if(rawTime==null)return changeColor;
        String raw=rawTime.trim();
        if(raw.isEmpty()||"—".equals(raw))return changeColor;
        String normalized=raw.replace('۰','0').replace('۱','1').replace('۲','2').replace('۳','3')
                .replace('۴','4').replace('۵','5').replace('۶','6').replace('۷','7')
                .replace('۸','8').replace('۹','9');
        java.util.regex.Matcher m=java.util.regex.Pattern
                .compile("(?:^|\\s)(\\d{1,2}):(\\d{2})(?::(\\d{2}))?(?:\\s|$)")
                .matcher(normalized);
        if(!m.find())return changeColor;
        int h=Integer.parseInt(m.group(1)), min=Integer.parseInt(m.group(2));
        int sec=m.group(3)==null?0:Integer.parseInt(m.group(3));
        if(h>23||min>59||sec>59)return changeColor;
        java.util.Calendar changed=java.util.Calendar.getInstance();
        changed.set(java.util.Calendar.HOUR_OF_DAY,h);
        changed.set(java.util.Calendar.MINUTE,min);
        changed.set(java.util.Calendar.SECOND,sec);
        changed.set(java.util.Calendar.MILLISECOND,0);
        long age=System.currentTimeMillis()-changed.getTimeInMillis();
        return age>=0&&age<=5*60*1000?changeColor:Color.WHITE;
    }

    private static boolean validTsetmcDate(int value){
        String s=String.valueOf(value);
        if(s.length()!=8)return false;
        try{
            int y=Integer.parseInt(s.substring(0,4)),m=Integer.parseInt(s.substring(4,6)),d=Integer.parseInt(s.substring(6,8));
            java.util.GregorianCalendar c=new java.util.GregorianCalendar();
            c.setLenient(false);c.clear();c.set(y,m-1,d);c.getTime();
            return y>=1900&&m>=1&&m<=12&&d>=1;
        }catch(Exception e){return false;}
    }

    private static String formatTsetmcDate(String raw,int fmt,int dateOrder){
        if(fmt==5)return "";
        try{
            String value=raw==null?"":raw.trim();
            if(value.length()!=8)return "";
            int gy=Integer.parseInt(value.substring(0,4)),gm=Integer.parseInt(value.substring(4,6)),gd=Integer.parseInt(value.substring(6,8));
            if(!validTsetmcDate(Integer.parseInt(value)))return "";
            int[] gdm={0,31,59,90,120,151,181,212,243,273,304,334};
            int gy2=gm>2?gy+1:gy;
            int days=355666+365*gy+(gy2+3)/4-(gy2+99)/100+(gy2+399)/400+gd+gdm[gm-1];
            int jy=-1595+33*(days/12053);days%=12053;
            jy+=4*(days/1461);days%=1461;
            if(days>365){jy+=(days-1)/365;days=(days-1)%365;}
            int jm,jd;
            if(days<186){jm=1+days/31;jd=1+days%31;}
            else{jm=7+(days-186)/30;jd=1+(days-186)%30;}
            String d=String.format(Locale.US,"%02d",jd),m=String.format(Locale.US,"%02d",jm);
            switch(fmt){
                case 1:return dateOrder==1?d+" - "+m:m+"-"+d;
                case 2:return dateOrder==1?d+"."+m:m+"."+d;
                case 3:return jy+"/"+m+"/"+d;
                case 4:
                    String[] months={"فروردین","اردیبهشت","خرداد","تیر","مرداد","شهریور","مهر","آبان","آذر","دی","بهمن","اسفند"};
                    return d+" "+months[jm-1];
                default:return dateOrder==1?d+"/"+m:m+"/"+d;
            }
        }catch(Exception e){return "";}
    }

    private static String formatTimeOrDate(String t,int fmt,int dateOrder,boolean en){
        if(t==null||t.isEmpty()||"—".equals(t))return "—";
        if(t.contains(":")){String[] parts=t.trim().split("\\s+");String clock=parts[0];String[] hm=clock.split(":");if(hm.length>=2)return hm[0]+":"+hm[1];}
        if(!t.matches(".*[۰-۹0-9].*"))return t;
        if(t.contains(" ")&&!t.contains(":")){String[] parts=t.trim().split("\\s+");if(parts.length>=2){int day=parseNum(parts[0]);int month=month(parts[1]);if(fmt==5)return "";String d=String.format(Locale.US,"%02d",day),m=String.format(Locale.US,"%02d",month);switch(fmt){case 1:return "\u202A"+(dateOrder==1?d+" - "+m:m+"-"+d)+"\u202C";case 2:return "\u202A"+(dateOrder==1?d+"."+m:m+"."+d)+"\u202C";case 3:return "\u202A1405/"+m+"/"+d+"\u202C";case 4:return d+" "+parts[1];default:return "\u202A"+(dateOrder==1?d+"/"+m:m+"/"+d)+"\u202C";}}}
        return t;
    }
    private static int parseNum(String s){try{String x=s.replace('۰','0').replace('۱','1').replace('۲','2').replace('۳','3').replace('۴','4').replace('۵','5').replace('۶','6').replace('۷','7').replace('۸','8').replace('۹','9').replaceAll("[^0-9]","");return Integer.parseInt(x);}catch(Exception e){return 0;}}
    private static int month(String s){String[]m={"فروردین","اردیبهشت","خرداد","تیر","مرداد","شهریور","مهر","آبان","آذر","دی","بهمن","اسفند"};for(int i=0;i<m.length;i++)if(s.contains(m[i]))return i+1;return 0;}
    // Native RemoteViews collection: ListView is supported in app widgets and scrolls internally.
    private static void setListItems(Context c,RemoteViews widget,int id,Map<String,JSONObject> data){
        if(android.os.Build.VERSION.SDK_INT<31)return;
        android.content.SharedPreferences p=c.getSharedPreferences("widget_"+id,Context.MODE_PRIVATE);
        int count=0;
        for(int i=0;i<SLOT_COUNT;i++)if(!p.getString("key"+i,i<5?DEFAULT_KEYS[i]:"").isEmpty())count++;
        if(count<=6)return;
        boolean en="en".equals(p.getString("lang","fa"));
        boolean showNames=p.getBoolean("showNames",true),showPct=p.getBoolean("showPct",true),showTime=p.getBoolean("showTime",true);
        int namePosition=p.getInt("namePosition",1),muted=p.getInt("mutedColor",Color.LTGRAY),metaWidth=p.getInt("metaWidth",40);
        float ps=p.getInt("priceSize",18),pctSize=p.getInt("pctSize",10),ts=p.getInt("timeSize",8),ns=p.getInt("nameSize",8);
        RemoteViews.RemoteCollectionItems.Builder builder=new RemoteViews.RemoteCollectionItems.Builder();
        long itemId=0;
        for(int i=0;i<SLOT_COUNT;i++){
            String k=p.getString("key"+i,i<5?DEFAULT_KEYS[i]:"");
            if(k.isEmpty())continue;
            JSONObject o=data==null?null:data.get(k);
            String price=p.getString("lastPrice"+i,"—"),pct=p.getString("lastPct"+i,"—"),time=p.getString("lastTime"+i,"—");
            int valueColor=p.getInt("lastColor"+i,Color.LTGRAY),timeTextColor=p.getInt("lastTimeColor"+i,valueColor);
            if(o!=null){
                price=price(o,k);double dp=o.optDouble("dp",Double.NaN);String dt=o.optString("dt","");
                if(!Double.isNaN(dp)){pct=percent(dp,en);valueColor="high".equalsIgnoreCase(dt)?GREEN:"low".equalsIgnoreCase(dt)?RED:YELLOW;}
                boolean stale=isTsetmcKey(k)&&o.optBoolean("stale",false);
                if(stale)valueColor=YELLOW;
                String rawTime=o.optString("t","—");time=formatTimeOrDate(rawTime,p.getInt("dateFormat",0),p.getInt("dateOrder",0),en);timeTextColor=stale?YELLOW:timeColor(rawTime,valueColor);
                p.edit().putString("lastPrice"+i,price).putString("lastPct"+i,pct).putString("lastTime"+i,time).putInt("lastColor"+i,valueColor).putInt("lastTimeColor"+i,timeTextColor).apply();
            }
            RemoteViews row=new RemoteViews(c.getPackageName(),R.layout.widget_item);
            row.setOnClickFillInIntent(R.id.itemRow,new Intent());
            row.setViewLayoutHeight(R.id.itemRow,30,android.util.TypedValue.COMPLEX_UNIT_DIP);
            row.setTextViewTextSize(R.id.price,2,ps);row.setTextViewTextSize(R.id.priceInline,2,ps);row.setTextViewTextSize(R.id.priceVertical,2,ps);row.setTextViewTextSize(R.id.priceRight,2,ps);
            row.setTextViewTextSize(R.id.name,2,ns);row.setTextViewTextSize(R.id.nameInline,2,ns);row.setTextViewTextSize(R.id.nameVertical,2,ns);row.setTextViewTextSize(R.id.nameRight,2,ns);
            row.setTextViewTextSize(R.id.pct,2,pctSize);row.setTextViewTextSize(R.id.time,2,ts);
            row.setViewLayoutWidth(R.id.meta,metaWidth,android.util.TypedValue.COMPLEX_UNIT_DIP);
            int rowGap=p.getInt("rowSpace",0), metaGap=p.getInt("metaGap",0);
            if(rowGap>0)row.setViewLayoutMargin(R.id.itemRow,RemoteViews.MARGIN_BOTTOM,rowGap,android.util.TypedValue.COMPLEX_UNIT_DIP);
            if(metaGap!=0)row.setViewLayoutMargin(R.id.pct,RemoteViews.MARGIN_BOTTOM,metaGap,android.util.TypedValue.COMPLEX_UNIT_DIP);
            String name=showNames?nameFor(k,en):"";
            row.setTextViewText(R.id.name,name);row.setTextViewText(R.id.nameInline,name);row.setTextViewText(R.id.nameVertical,name);row.setTextViewText(R.id.nameRight,name);
            int nameColor=o!=null&&isTsetmcKey(k)&&o.optBoolean("stale",false)?YELLOW:muted;
            row.setTextColor(R.id.name,nameColor);row.setTextColor(R.id.nameInline,nameColor);row.setTextColor(R.id.nameVertical,nameColor);row.setTextColor(R.id.nameRight,nameColor);row.setTextColor(R.id.time,timeTextColor);
            row.setTextViewText(R.id.price,digits(price,en));row.setTextColor(R.id.price,valueColor);
            row.setTextViewText(R.id.priceInline,digits(price,en));row.setTextColor(R.id.priceInline,valueColor);
            row.setTextViewText(R.id.priceVertical,digits(price,en));row.setTextColor(R.id.priceVertical,valueColor);
            row.setTextViewText(R.id.priceRight,digits(price,en));row.setTextColor(R.id.priceRight,valueColor);
            row.setTextViewText(R.id.pct,digits(pct,en));row.setTextColor(R.id.pct,timeTextColor);row.setTextViewText(R.id.time,digits(time,en));
            row.setViewVisibility(R.id.name,View.GONE);
            row.setViewVisibility(R.id.nameInline,showNames&&namePosition==1?View.VISIBLE:View.GONE);
            row.setViewVisibility(R.id.nameVertical,showNames&&namePosition==2?View.VISIBLE:View.GONE);
            row.setViewVisibility(R.id.nameRight,showNames&&namePosition==3?View.VISIBLE:View.GONE);
            row.setViewVisibility(R.id.price,namePosition==0?View.VISIBLE:View.GONE);
            row.setViewVisibility(R.id.priceInline,namePosition==1?View.VISIBLE:View.GONE);
            row.setViewVisibility(R.id.priceVertical,namePosition==2?View.VISIBLE:View.GONE);
            row.setViewVisibility(R.id.priceRight,namePosition==3?View.VISIBLE:View.GONE);
            row.setViewVisibility(R.id.pct,showPct?View.VISIBLE:View.GONE);row.setViewVisibility(R.id.time,showTime?View.VISIBLE:View.GONE);
            row.setInt(R.id.itemRow,"setLayoutDirection",View.LAYOUT_DIRECTION_LTR);
            row.setInt(R.id.meta,"setGravity",android.view.Gravity.CENTER_VERTICAL|android.view.Gravity.RIGHT);
            row.setInt(R.id.pct,"setGravity",android.view.Gravity.RIGHT);row.setInt(R.id.nameRight,"setGravity",android.view.Gravity.RIGHT|android.view.Gravity.CENTER_VERTICAL);row.setInt(R.id.time,"setGravity",android.view.Gravity.END);
            builder.addItem(itemId++,row);
        }
        widget.setRemoteAdapter(R.id.rowsList,builder.build());
    }

}
