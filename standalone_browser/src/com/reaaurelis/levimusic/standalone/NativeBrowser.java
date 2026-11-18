package com.reaaurelis.levimusic.standalone;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.text.InputType;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.Map;

/**
 * Experimental Android WebView browser hosted IN the Minecraft Activity.
 * This is NOT a separate app or audio-extraction engine.
 * Full video/web content stays visible, playback requires direct user action.
 * It does not bypass DRM, ads or background playback restrictions.
 *
 * The ActivityThread lookup is a version-sensitive hidden API, so the browser
 * is opt-in and reports failures without terminating the game.
 */
public final class NativeBrowser {
    private static final String TAG="LeviMusicWebView";
    private static FrameLayout overlay;
    private static WebView web;

    private NativeBrowser(){}

    public static void open() {
        final Activity activity=findCurrentActivity();
        if(activity==null) {
            Log.w(TAG,"No active Minecraft Activity. Cannot attach WebView; no game hooks touched.");
            return;
        }
        activity.runOnUiThread(()->{
            try{show(activity);}
            catch(Throwable t){Log.e(TAG,"Embedded WebView failed",t);close();}
        });
    }

    private static Activity findCurrentActivity() {
        try {
            Class<?> threadClass=Class.forName("android.app.ActivityThread");
            Method current=threadClass.getDeclaredMethod("currentActivityThread");
            current.setAccessible(true);
            Object thread=current.invoke(null);
            if(thread==null)return null;
            Field activities=threadClass.getDeclaredField("mActivities");
            activities.setAccessible(true);
            Object records=activities.get(thread);
            if(!(records instanceof Map))return null;
            Activity candidate=null;
            for(Object record:((Map<?,?>)records).values()){
                if(record==null)continue;
                Field field=record.getClass().getDeclaredField("activity");
                field.setAccessible(true);
                Object value=field.get(record);
                if(!(value instanceof Activity))continue;
                Activity activity=(Activity)value;
                if(activity.isFinishing()||activity.isDestroyed())continue;
                if(activity.getWindow()!=null && activity.getWindow().getDecorView().hasWindowFocus())return activity;
                candidate=activity;
            }
            return candidate;
        } catch(Throwable error){
            Log.w(TAG,"ActivityThread reflection is unavailable on this Android/launcher version",error);
            return null;
        }
    }

    private static GradientDrawable rounded(int color,float dp,Activity a){
        GradientDrawable drawable=new GradientDrawable();
        drawable.setColor(color);drawable.setCornerRadius(dp*a.getResources().getDisplayMetrics().density);
        return drawable;
    }

    private static TextView button(Activity a,String label,Runnable onClick){
        TextView view=new TextView(a);
        view.setText(label);view.setTextSize(14);view.setTypeface(Typeface.DEFAULT,Typeface.BOLD);
        view.setTextColor(Color.WHITE);view.setGravity(Gravity.CENTER);
        view.setBackground(rounded(0xFF343842,9,a));
        view.setOnClickListener(v->onClick.run());
        return view;
    }
    private static int dp(Activity a,float v){
        return (int)(a.getResources().getDisplayMetrics().density*v+0.5f);
    }

    private static void show(Activity activity){
        if(overlay!=null) {
            overlay.setVisibility(View.VISIBLE);
            return;
        }
        View rootView=activity.getWindow().getDecorView();
        if(!(rootView instanceof ViewGroup)){
            Log.e(TAG,"Minecraft decor view is not a ViewGroup");
            return;
        }
        final ViewGroup decor=(ViewGroup)rootView;

        FrameLayout container=new FrameLayout(activity);
        container.setBackgroundColor(0xDD000000);
        container.setClickable(true);
        container.setFocusable(true);

        LinearLayout panel=new LinearLayout(activity);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setBackground(rounded(0xFF15181E,16,activity));
        panel.setPadding(dp(activity,8),dp(activity,7),dp(activity,8),dp(activity,8));

        LinearLayout bar=new LinearLayout(activity);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setPadding(0,0,0,dp(activity,8));

        final WebView browser=new WebView(activity);
        browser.setBackgroundColor(0xFF121317);
        browser.setWebChromeClient(new WebChromeClient());
        browser.setWebViewClient(new WebViewClient());
        WebSettings settings=browser.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setMediaPlaybackRequiresUserGesture(true);
        settings.setLoadsImagesAutomatically(true);
        settings.setAllowFileAccess(false);
        settings.setAllowContentAccess(false);
        settings.setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        settings.setJavaScriptCanOpenWindowsAutomatically(false);

        TextView back=button(activity,"‹",()->{if(browser.canGoBack())browser.goBack();});
        TextView forward=button(activity,"›",()->{if(browser.canGoForward())browser.goForward();});
        TextView refresh=button(activity,"↻",()->browser.reload());
        TextView close=button(activity,"×",NativeBrowser::close);

        EditText url=new EditText(activity);
        url.setSingleLine(true);
        url.setTextSize(13);
        url.setTextColor(Color.WHITE);
        url.setHintTextColor(0xFFABB1BA);
        url.setHint("Search or enter HTTPS address");
        url.setInputType(InputType.TYPE_CLASS_TEXT|InputType.TYPE_TEXT_VARIATION_URI);
        url.setPadding(dp(activity,10),0,dp(activity,10),0);
        url.setBackground(rounded(0xFF272B33,8,activity));
        url.setImeOptions(android.view.inputmethod.EditorInfo.IME_ACTION_GO);
        url.setOnEditorActionListener((v,action,event)->{
            String value=url.getText().toString().trim();
            if(value.isEmpty())return true;
            String dest=value.contains(" ") || !value.contains(".")
                ? "https://www.google.com/search?q="+android.net.Uri.encode(value)
                : (value.startsWith("https://")?value:
                    "https://"+value.replaceFirst("^http://",""));
            // Only navigate secure HTTPS pages.
            if(dest.startsWith("https://"))browser.loadUrl(dest);
            return true;
        });

        LinearLayout.LayoutParams small=new LinearLayout.LayoutParams(dp(activity,39),dp(activity,40));
        small.setMargins(0,0,dp(activity,5),0);
        bar.addView(back,small);
        bar.addView(forward,new LinearLayout.LayoutParams(dp(activity,39),dp(activity,40)));
        bar.addView(refresh,new LinearLayout.LayoutParams(dp(activity,39),dp(activity,40)));
        LinearLayout.LayoutParams address=new LinearLayout.LayoutParams(0,dp(activity,40),1);
        address.setMargins(dp(activity,5),0,dp(activity,5),0);
        bar.addView(url,address);
        bar.addView(close,new LinearLayout.LayoutParams(dp(activity,42),dp(activity,40)));

        panel.addView(bar,new LinearLayout.LayoutParams(-1,dp(activity,48)));
        panel.addView(browser,new LinearLayout.LayoutParams(-1,0,1));
        FrameLayout.LayoutParams p=new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,ViewGroup.LayoutParams.MATCH_PARENT);
        p.setMargins(dp(activity,20),dp(activity,28),dp(activity,20),dp(activity,28));
        container.addView(panel,p);
        decor.addView(container,new ViewGroup.LayoutParams(-1,-1));
        overlay=container;web=browser;
        browser.loadUrl("https://m.youtube.com");
        Log.i(TAG,"Embedded web browser opened; video/website must remain visible.");
    }
    public static void close(){
        WebView browser=web;
        FrameLayout container=overlay;
        web=null;overlay=null;
        if(container==null)return;
        // Close is invoked from the browser's Android UI thread.
        if(container.getParent() instanceof ViewGroup)
            ((ViewGroup)container.getParent()).removeView(container);
        if(browser!=null){
            browser.stopLoading();
            browser.loadUrl("about:blank");
            browser.destroy();
        }
    }
}
