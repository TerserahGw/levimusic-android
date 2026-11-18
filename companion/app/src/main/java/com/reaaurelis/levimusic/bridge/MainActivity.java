package com.reaaurelis.levimusic.bridge;

import android.app.Activity;
import android.content.ClipboardManager;
import android.content.ClipData;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Bundle;
import android.provider.Settings;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

public final class MainActivity extends Activity {
    private TextView status;
    @Override public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().setStatusBarColor(Color.rgb(17, 20, 27));
        getWindow().setNavigationBarColor(Color.rgb(17, 20, 27));
        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(Color.rgb(17,20,27));
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(dp(26),dp(40),dp(26),dp(36));
        scroll.addView(col);
        label(col, "LeviMusic Bridge", 26, Color.WHITE, true);
        label(col, "Spotify & YouTube Music • Media Session", 14, 0xFFB9BDC6, false);
        label(col, "Control your official music app while Minecraft is running. Playback is performed by Spotify or YouTube Music, not this bridge.", 15, 0xFFE3E4E8, false);
        status=label(col, "", 15, 0xFF99D5B0, true);
        label(col, "1. Enable Notification Access for LeviMusic Bridge.\n2. Copy the pairing key below into Minecraft → LeviMusic settings → Bridge Key.\n3. Start a song using the official Spotify or YouTube Music app, then return to Minecraft.\n4. Use the in-game player to play/pause, skip and seek, subject to your provider's restrictions.", 15, 0xFFE5E5E5, false);
        Button permission=button(col,"Open Notification Access",() -> {
            startActivity(new Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS));
        });
        permission.setAllCaps(false);
        label(col, "PAIRING KEY • Local to this device", 12, 0xFF91959B, true);
        TextView secret=label(col, BridgeService.getOrCreateToken(this), 18, Color.WHITE, true);
        secret.setTextIsSelectable(true);
        button(col,"Copy pairing key",()->{
            ((ClipboardManager)getSystemService(CLIPBOARD_SERVICE))
                .setPrimaryClip(ClipData.newPlainText("LeviMusic pairing key",BridgeService.getOrCreateToken(this)));
        });
        label(col, "Security: the server accepts connections only on 127.0.0.1 with this pairing key. Do not share the key or place it in a public repository.", 13, 0xFFAAAFB8, false);
        label(col, "No streaming bypass, download or background playback unlock is included. Subscription, ads, song availability and skips remain controlled by the music service.", 13, 0xFFAAAFB8, false);
        setContentView(scroll);
        showState();
    }
    private int dp(int n){return (int)(getResources().getDisplayMetrics().density*n+.5f);}
    private TextView label(LinearLayout col,String text,int sp,int color,boolean bold){
        TextView t=new TextView(this);
        t.setText(text); t.setTextSize(sp);t.setTextColor(color);
        if(bold)t.setTypeface(Typeface.DEFAULT,Typeface.BOLD);
        t.setPadding(0,dp(7),0,dp(12));
        col.addView(t,new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,ViewGroup.LayoutParams.WRAP_CONTENT));
        return t;
    }
    private Button button(LinearLayout col,String text,Runnable action) {
        Button b=new Button(this);
        b.setText(text);b.setAllCaps(false);
        b.setOnClickListener(v->action.run());
        col.addView(b,new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,dp(55)));
        return b;
    }
    @Override protected void onResume(){super.onResume();showState();}
    private void showState(){
        if(status==null)return;
        boolean active=getSharedPreferences("bridge",MODE_PRIVATE).getBoolean("service_ready",false);
        status.setText(active?"● Bridge running • listening locally":"○ Bridge not active • enable Notification Access");
    }
}
