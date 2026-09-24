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
    private static final int SLOT_COUNT = 5;
    private static final String[] DEFAULT_KEYS = {
            "crypto-tether-irr","price_dollar_rl","geram18","ime_fund_kahroba",
            "ime_fund_ayar","ons","oil_brent","bourse","sekee"
    };
    private static final int[] PRICE_IDS = {R.id.price1,R.id.price2,R.id.price3,R.id.price4,R.id.price5};
    private static final int[] PRICE_INLINE_IDS = {R.id.priceInline1,R.id.priceInline2,R.id.priceInline3,R.id.priceInline4,R.id.priceInline5};
    private static final int[] PRICE_VERTICAL_IDS = {R.id.priceVertical1,R.id.priceVertical2,R.id.priceVertical3,R.id.priceVertical4,R.id.priceVertical5};
    private static final int[] NAME_IDS = {R.id.name1,R.id.name2,R.id.name3,R.id.name4,R.id.name5};
    private static final int[] NAME_INLINE_IDS = {R.id.nameInline1,R.id.nameInline2,R.id.nameInline3,R.id.nameInline4,R.id.nameInline5};
    private static final int[] NAME_VERTICAL_IDS = {R.id.nameVertical1,R.id.nameVertical2,R.id.nameVertical3,R.id.nameVertical4,R.id.nameVertical5};
    private static final int[] PCT_IDS = {R.id.pct1,R.id.pct2,R.id.pct3,R.id.pct4,R.id.pct5};
    private static final int[] TIME_IDS = {R.id.time1,R.id.time2,R.id.time3,R.id.time4,R.id.time5};
    private static final int[] ROW_IDS = {R.id.row1,R.id.row2,R.id.row3,R.id.row4,R.id.row5};
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
        int bg=p.getInt("bgColor",Color.BLACK), muted=p.getInt("mutedColor",Color.LTGRAY), pad=p.getInt("padding",4), gap=p.getInt("rowSpace",0);
        float ps=p.getInt("priceSize",18), pct=p.getInt("pctSize",10), ts=p.getInt("timeSize",8), rs=p.getInt("refreshSize",7), ns=p.getInt("nameSize",8);
        boolean showPct=p.getBoolean("showPct",true), showTime=p.getBoolean("showTime",true), showRefresh=p.getBoolean("showRefresh",true), showNames=p.getBoolean("showNames",true);
        int namePosition=p.getInt("namePosition",0);
        v.setInt(R.id.root,"setBackgroundColor",bg); v.setViewPadding(R.id.root,pad,pad,pad,pad);
        for(int i=0;i<SLOT_COUNT;i++){
            v.setTextViewTextSize(PRICE_IDS[i],2,ps);
            v.setTextViewTextSize(PRICE_INLINE_IDS[i],2,ps);
            v.setTextViewTextSize(PRICE_VERTICAL_IDS[i],2,ps);
            v.setTextViewTextSize(NAME_IDS[i],2,ns);
            v.setTextViewTextSize(PCT_IDS[i],2,pct);
            v.setTextViewTextSize(TIME_IDS[i],2,ts);
            if(gap>0)v.setViewLayoutMargin(ROW_IDS[i],RemoteViews.MARGIN_BOTTOM,gap,android.util.TypedValue.COMPLEX_UNIT_DIP);
            v.setTextColor(NAME_IDS[i],muted); v.setTextColor(TIME_IDS[i],muted);
            v.setViewVisibility(NAME_IDS[i],showNames&&namePosition==0?View.VISIBLE:View.GONE);
            v.setViewVisibility(NAME_INLINE_IDS[i],showNames&&namePosition==1?View.VISIBLE:View.GONE);
            v.setViewVisibility(NAME_VERTICAL_IDS[i],showNames&&namePosition==2?View.VISIBLE:View.GONE);
            v.setViewVisibility(PRICE_IDS[i],namePosition==0?View.VISIBLE:View.GONE);
            v.setViewVisibility(PRICE_INLINE_IDS[i],namePosition==1?View.VISIBLE:View.GONE);
            v.setViewVisibility(PRICE_VERTICAL_IDS[i],namePosition==2?View.VISIBLE:View.GONE);
            v.setViewVisibility(PCT_IDS[i],showPct?View.VISIBLE:View.GONE);
            v.setViewVisibility(TIME_IDS[i],showTime?View.VISIBLE:View.GONE);
        }
        v.setTextViewTextSize(R.id.requestTime,2,rs);
        v.setTextColor(R.id.requestTime,muted);
        v.setViewVisibility(R.id.requestTime,showRefresh?View.VISIBLE:View.GONE);

        Intent r=new Intent(c,TgjuWidgetProvider.class).setAction(ACTION_REFRESH).setData(Uri.parse("tgju://refresh/"+id));
        PendingIntent rp=PendingIntent.getBroadcast(c,id,r,PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE);
        v.setOnClickPendingIntent(R.id.root,rp);

        Intent s=new Intent(c,WidgetSettingsActivity.class).setAction("com.mehdi.tgjuwidget.SETTINGS").setData(Uri.parse("tgju://settings/"+id)).putExtra(AppWidgetManager.EXTRA_APPWIDGET_ID,id);
        PendingIntent sp=PendingIntent.getActivity(c,id+10000,s,PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE);
        v.setOnClickPendingIntent(R.id.settingsButton,sp);
        v.setTextViewText(R.id.settingsButton,"⚙");
        return v;
    }

    private void refresh(Context c,int[] ids){
        if(ids==null||ids.length==0)return;
        new Thread(()->{
            android.content.SharedPreferences p=c.getSharedPreferences("widget_"+ids[0],Context.MODE_PRIVATE);
            String[] keys=new String[SLOT_COUNT];
            for(int i=0;i<SLOT_COUNT;i++)keys[i]=p.getString("key"+i,DEFAULT_KEYS[i]);
            Map<String,JSONObject> data=new HashMap<>();
            boolean success=false;
            String requestTime=now();
            try{
                success=fetchBatch(keys,data);
                if(!success) {
                    for(String k:keys) fetchOne(k,data);
                    success=hasAny(keys,data);
                }
            }catch(Exception ignored){}
            final boolean ok=success;
            new Handler(Looper.getMainLooper()).post(()->{
                AppWidgetManager m=AppWidgetManager.getInstance(c);
                for(int id:ids)update(c,m,id,data,requestTime,ok);
            });
        }).start();
    }

    private static boolean fetchBatch(String[] keys,Map<String,JSONObject> data){
        HttpURLConnection con=null;
        try{
            StringBuilder list=new StringBuilder();
            for(int i=0;i<keys.length;i++){if(i>0)list.append(',');list.append(keys[i]);}
            con=(HttpURLConnection)new URL("https://api.tgju.org/v1/widget/tmp?keys="+list).openConnection();
            con.setRequestMethod("GET"); con.setConnectTimeout(10000); con.setReadTimeout(10000); con.setUseCaches(false);
            con.setRequestProperty("Cache-Control","no-cache");
            if(con.getResponseCode()!=200)return false;
            String body=read(con);
            JSONObject root=new JSONObject(body);
            JSONObject response=root.optJSONObject("response");
            JSONArray a=response==null?null:response.optJSONArray("indicators");
            if(a==null)return false;
            for(int i=0;i<a.length();i++){JSONObject o=a.optJSONObject(i);if(o!=null)putAliases(data,o);}
            return hasAny(keys,data);
        }catch(Exception e){return false;}finally{if(con!=null)con.disconnect();}
    }

    private static void fetchOne(String key,Map<String,JSONObject> data){
        HttpURLConnection con=null;
        try{
            con=(HttpURLConnection)new URL("https://api.tgju.org/v1/widget/tmp?keys="+key).openConnection();
            con.setRequestMethod("GET"); con.setConnectTimeout(7000); con.setReadTimeout(7000); con.setUseCaches(false);
            con.setRequestProperty("Cache-Control","no-cache");
            if(con.getResponseCode()!=200)return;
            JSONObject root=new JSONObject(read(con));
            JSONObject response=root.optJSONObject("response");
            JSONArray a=response==null?null:response.optJSONArray("indicators");
            if(a!=null&&a.length()>0){JSONObject o=a.optJSONObject(0);if(o!=null)putAliases(data,o);}
        }catch(Exception ignored){}finally{if(con!=null)con.disconnect();}
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

    private static void putAliases(Map<String,JSONObject>d,JSONObject o){
        for(String f:new String[]{"key","slug","name","symbol","id","item_id"}){
            String x=o.optString(f,""); if(!x.isEmpty())d.put(x,o);
        }
    }

    private void update(Context c,AppWidgetManager m,int id,Map<String,JSONObject>d,String request,boolean ok){
        RemoteViews v=buildViews(c,id);
        android.content.SharedPreferences p=c.getSharedPreferences("widget_"+id,Context.MODE_PRIVATE);
        boolean en="en".equals(p.getString("lang","fa"));
        boolean showNames=p.getBoolean("showNames",true);
        for(int i=0;i<SLOT_COUNT;i++){
            String k=p.getString("key"+i,DEFAULT_KEYS[i]); JSONObject o=d.get(k);
            String price="—",pct="—",time="—"; int color=Color.LTGRAY;
            if(o!=null){
                price=price(o,k); double dp=o.optDouble("dp",Double.NaN); String dt=o.optString("dt","");
                if(!Double.isNaN(dp)){pct=percent(dp,en);color="high".equalsIgnoreCase(dt)?GREEN:"low".equalsIgnoreCase(dt)?RED:YELLOW;}
            }
            String name=showNames?nameFor(k,en):"";
            time=formatTimeOrDate(o==null?"—":o.optString("t","—"),p.getInt("dateFormat",0),en);
            v.setTextViewText(NAME_IDS[i],name);
            v.setTextViewText(NAME_INLINE_IDS[i],name);
            v.setTextViewText(NAME_VERTICAL_IDS[i],name);
            v.setTextColor(NAME_IDS[i],p.getInt("mutedColor",Color.LTGRAY));
            v.setTextColor(NAME_INLINE_IDS[i],p.getInt("mutedColor",Color.LTGRAY));
            v.setTextColor(NAME_VERTICAL_IDS[i],p.getInt("mutedColor",Color.LTGRAY));
            v.setTextViewText(PRICE_IDS[i],digits(price,en)); v.setTextColor(PRICE_IDS[i],color);
            v.setTextViewText(PRICE_INLINE_IDS[i],digits(price,en)); v.setTextColor(PRICE_INLINE_IDS[i],color);
            v.setTextViewText(PRICE_VERTICAL_IDS[i],digits(price,en)); v.setTextColor(PRICE_VERTICAL_IDS[i],color);
            v.setTextViewText(PCT_IDS[i],digits(pct,en)); v.setTextColor(PCT_IDS[i],color);
            v.setTextViewText(TIME_IDS[i],digits(time,en));
        }
        v.setTextViewText(R.id.requestTime,digits(request,en));
        v.setTextColor(R.id.requestTime,ok?p.getInt("mutedColor",Color.LTGRAY):RED);
        m.updateAppWidget(id,v);
    }

    private static String nameFor(String k,boolean en){
        if(en){
            if("crypto-tether-irr".equals(k))return "Tether";
            if("price_dollar_rl".equals(k))return "Dollar";
            if("geram18".equals(k))return "Gold 18";
            if("ime_fund_kahroba".equals(k))return "Kahroba";
            if("ime_fund_ayar".equals(k))return "Ayar";
            if("ons".equals(k))return "Ounce";
            if("oil_brent".equals(k))return "Brent";
            if("bourse".equals(k))return "Bourse";
            if("sekee".equals(k))return "Imami";
        }
        if("crypto-tether-irr".equals(k))return "تتر";
        if("price_dollar_rl".equals(k))return "دلار";
        if("geram18".equals(k))return "گرم ۱۸";
        if("ime_fund_kahroba".equals(k))return "کهربا";
        if("ime_fund_ayar".equals(k))return "عیار";
        if("ons".equals(k))return "انس";
        if("oil_brent".equals(k))return "برنت";
        if("bourse".equals(k))return "بورس";
        if("sekee".equals(k))return "امامی";
        return k;
    }

    private static String price(JSONObject o,String k){
        try{
            double n;
            if("ons".equals(k)||"oil_brent".equals(k)||"bourse".equals(k))
                n=Double.parseDouble(o.optString("p","0").replace(",",""));
            else if("crypto-tether-irr".equals(k)&&o.has("p_irr"))
                n=Double.parseDouble(o.optString("p_irr","0").replace(",",""))/10.0;
            else n=Double.parseDouble(o.optString("p","0").replace(",",""))/10.0;
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

    private static String formatTimeOrDate(String t,int fmt,boolean en){
        if(t==null||t.isEmpty()||"—".equals(t))return "—";
        if(t.contains(":")){String[] parts=t.trim().split("\\s+");String clock=parts[0];String[] hm=clock.split(":");if(hm.length>=2)return hm[0]+":"+hm[1];}
        if(!t.matches(".*[۰-۹0-9].*"))return t;
        if(t.contains(" ")&&!t.contains(":")){String[] parts=t.trim().split("\\s+");if(parts.length>=2){int day=parseNum(parts[0]);int month=month(parts[1]);if(fmt==5)return "";String d=String.format(Locale.US,"%02d",day),m=String.format(Locale.US,"%02d",month);switch(fmt){case 1:return d+" - "+m;case 2:return d+"."+m;case 3:return d+"/"+m+"/1405";case 4:return d+" "+parts[1];default:return d+"/"+m;}}}
        return t;
    }
    private static int parseNum(String s){try{String x=s.replace('۰','0').replace('۱','1').replace('۲','2').replace('۳','3').replace('۴','4').replace('۵','5').replace('۶','6').replace('۷','7').replace('۸','8').replace('۹','9').replaceAll("[^0-9]","");return Integer.parseInt(x);}catch(Exception e){return 0;}}
    private static int month(String s){String[]m={"فروردین","اردیبهشت","خرداد","تیر","مرداد","شهریور","مهر","آبان","آذر","دی","بهمن","اسفند"};for(int i=0;i<m.length;i++)if(s.contains(m[i]))return i+1;return 0;}
}
