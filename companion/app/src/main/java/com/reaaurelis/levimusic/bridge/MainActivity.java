package com.reaaurelis.levimusic.bridge;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipboardManager;
import android.content.ClipData;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import org.json.JSONObject;

import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.Locale;

public final class MainActivity extends Activity {
    private static final int BG=0xFF111216, SURFACE=0xFF1C1E23, SURFACE_ALT=0xFF282B30;
    private static final int PRIMARY=0xFFF4F4F6, SECONDARY=0xFFA0A3AB;
    private static final int SOFT=0xFF777B83;
    private final Handler handler=new Handler(Looper.getMainLooper());
    private final ExecutorService io=Executors.newSingleThreadExecutor();
    private TextView currentTitle,currentArtist,timeLabel,trackCount,empty,headerStatus;
    private LinearLayout list;
    private EditText searchInput;
    private ImageButton playButton;
    private SeekBar timeline;
    private boolean dragging=false,searching=false;
    private int currentDuration=0;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setStatusBarColor(BG);
        getWindow().setNavigationBarColor(BG);
        if(Build.VERSION.SDK_INT>=33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                !=PackageManager.PERMISSION_GRANTED)requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS},27);
        buildUI();
        handler.post(poll);
    }
    private int dp(float n){return (int)(getResources().getDisplayMetrics().density*n+.5f);}
    private GradientDrawable bg(int fill,float radius){
        GradientDrawable d=new GradientDrawable();
        d.setColor(fill);d.setCornerRadius(dp(radius));return d;
    }
    private TextView text(String str,int sp,int color,boolean bold){
        TextView t=new TextView(this);
        t.setText(str);t.setTextSize(sp);t.setTextColor(color);
        t.setFontFeatureSettings("kern");
        if(bold)t.setTypeface(Typeface.create("sans-serif-medium",Typeface.NORMAL));
        else t.setTypeface(Typeface.create("sans-serif",Typeface.NORMAL));
        return t;
    }
    private LinearLayout column(){
        LinearLayout c=new LinearLayout(this);
        c.setOrientation(LinearLayout.VERTICAL);return c;
    }
    private void add(LinearLayout parent,View v,int w,int h){
        parent.addView(v,new LinearLayout.LayoutParams(w<0?w:dp(w),h<0?h:dp(h)));
    }
    private void buildUI(){
        LinearLayout root=column();
        root.setBackgroundColor(BG);
        root.setPadding(dp(22),dp(13),dp(22),dp(16));

        LinearLayout header=new LinearLayout(this);
        header.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout titles=column();
        add(titles,text("LeviMusic",25,PRIMARY,true),-1,-2);
        TextView sub=text("Listen while you play",12,SECONDARY,false);
        sub.setPadding(0,dp(2),0,0);add(titles,sub,-1,-2);
        header.addView(titles,new LinearLayout.LayoutParams(0,-2,1));
        ImageButton settings=icon(R.drawable.ic_settings,42);
        settings.setBackground(bg(SURFACE,13));
        settings.setOnClickListener(v->showBridgeSettings());
        add(header,settings,42,42);
        add(root,header,-1,61);

        LinearLayout searchBar=new LinearLayout(this);
        searchBar.setGravity(Gravity.CENTER_VERTICAL);
        searchBar.setBackground(bg(SURFACE,17));
        searchBar.setPadding(dp(10),dp(5),dp(8),dp(5));
        searchInput=new EditText(this);
        searchInput.setSingleLine(true);
        searchInput.setTextSize(15);
        searchInput.setHintTextColor(SOFT);
        searchInput.setTextColor(PRIMARY);
        searchInput.setHint("Search songs or artists");
        searchInput.setPadding(dp(12),0,dp(10),0);
        searchInput.setBackgroundColor(Color.TRANSPARENT);
        searchInput.setImeOptions(EditorInfo.IME_ACTION_SEARCH);
        searchInput.setInputType(InputType.TYPE_CLASS_TEXT);
        searchBar.addView(searchInput,new LinearLayout.LayoutParams(0,dp(42),1));
        ImageButton go=icon(R.drawable.ic_search,40);
        go.setOnClickListener(v->search());
        add(searchBar,go,40,40);
        searchInput.setOnEditorActionListener((v,action,event)->{
            if(action==EditorInfo.IME_ACTION_SEARCH){search();return true;}return false;
        });
        LinearLayout.LayoutParams searchParams=new LinearLayout.LayoutParams(-1,dp(56));
        searchParams.topMargin=dp(14);
        root.addView(searchBar,searchParams);

        LinearLayout listTitle=new LinearLayout(this);
        listTitle.setGravity(Gravity.CENTER_VERTICAL);
        TextView label=text("Discover",16,PRIMARY,true);
        listTitle.addView(label,new LinearLayout.LayoutParams(0,-2,1));
        trackCount=text("",12,SECONDARY,false);
        add(listTitle,trackCount,-2,-2);
        LinearLayout.LayoutParams listTitleParams=new LinearLayout.LayoutParams(-1,dp(54));
        listTitleParams.topMargin=dp(10);
        root.addView(listTitle,listTitleParams);

        ScrollView scroller=new ScrollView(this);
        scroller.setFillViewport(true);
        scroller.setVerticalScrollBarEnabled(false);
        list=column();
        list.setPadding(0,0,0,dp(22));
        scroller.addView(list);
        root.addView(scroller,new LinearLayout.LayoutParams(-1,0,1));
        empty=text("Search for your next song.\nYour music will keep playing while you explore Minecraft.",
                14,SECONDARY,false);
        empty.setGravity(Gravity.CENTER);
        empty.setLineSpacing(dp(5),1.0f);
        empty.setPadding(dp(18),dp(45),dp(18),dp(22));
        add(list,empty,-1,-2);

        LinearLayout player=column();
        player.setBackground(bg(SURFACE,22));
        player.setPadding(dp(18),dp(15),dp(18),dp(12));
        LinearLayout.LayoutParams playerParams=new LinearLayout.LayoutParams(-1,-2);
        playerParams.topMargin=dp(8);
        root.addView(player,playerParams);

        LinearLayout row=new LinearLayout(this);
        row.setGravity(Gravity.CENTER_VERTICAL);
        TextView album=text("♫",27,PRIMARY,true);
        album.setGravity(Gravity.CENTER);
        album.setBackground(bg(SURFACE_ALT,14));
        add(row,album,52,52);
        LinearLayout now=column();
        now.setPadding(dp(14),0,0,0);
        currentTitle=text("Nothing playing",16,PRIMARY,true);
        currentTitle.setSingleLine(true);currentTitle.setEllipsize(android.text.TextUtils.TruncateAt.END);
        add(now,currentTitle,-1,-2);
        currentArtist=text("Choose a song to begin",12,SECONDARY,false);
        currentArtist.setPadding(0,dp(3),0,0);
        currentArtist.setSingleLine(true);currentArtist.setEllipsize(android.text.TextUtils.TruncateAt.END);
        add(now,currentArtist,-1,-2);
        row.addView(now,new LinearLayout.LayoutParams(0,-2,1));
        add(player,row,-1,55);

        timeline=new SeekBar(this);
        timeline.setMax(1000);
        timeline.setProgressTintList(android.content.res.ColorStateList.valueOf(PRIMARY));
        timeline.setProgressBackgroundTintList(android.content.res.ColorStateList.valueOf(0xFF3D4048));
        timeline.setThumbTintList(android.content.res.ColorStateList.valueOf(PRIMARY));
        timeline.setPadding(dp(1),dp(5),dp(1),dp(0));
        timeline.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){
            @Override public void onStartTrackingTouch(SeekBar bar){dragging=true;}
            @Override public void onStopTrackingTouch(SeekBar bar){
                dragging=false;
                if(currentDuration>0)PlayerService.command("seek",
                    (long)currentDuration*bar.getProgress()/1000L);
            }
            @Override public void onProgressChanged(SeekBar bar,int progress,boolean fromUser){}
        });
        add(player,timeline,-1,27);
        timeLabel=text("0:00  /  0:00",11,SECONDARY,false);
        timeLabel.setGravity(Gravity.END);
        add(player,timeLabel,-1,20);

        LinearLayout controls=new LinearLayout(this);
        controls.setGravity(Gravity.CENTER);
        ImageButton previous=icon(R.drawable.ic_skip_back,48);
        previous.setOnClickListener(v->PlayerService.command("previous",0));
        add(controls,previous,49,47);
        playButton=icon(R.drawable.ic_play,50);
        playButton.setBackground(bg(0xFF383C43,16));
        LinearLayout.LayoutParams mainPlay=new LinearLayout.LayoutParams(dp(70),dp(53));
        mainPlay.leftMargin=dp(15);mainPlay.rightMargin=dp(15);
        controls.addView(playButton,mainPlay);
        playButton.setOnClickListener(v->{
            JSONObject status=PlayerService.getStatus();
            PlayerService.command(status!=null&&status.optBoolean("playing")?"pause":"play",0);
        });
        ImageButton next=icon(R.drawable.ic_skip_forward,48);
        next.setOnClickListener(v->PlayerService.command("next",0));
        add(controls,next,49,47);
        add(player,controls,-1,58);

        headerStatus=text("Search & play • ExoPlayer",10,SOFT,false);
        headerStatus.setGravity(Gravity.CENTER);
        headerStatus.setPadding(0,dp(9),0,0);
        add(root,headerStatus,-1,25);
        setContentView(root);
    }
    private ImageButton icon(int resource,int size){
        ImageButton b=new ImageButton(this);
        b.setImageResource(resource);
        b.setColorFilter(PRIMARY);
        b.setBackgroundColor(Color.TRANSPARENT);
        b.setScaleType(ImageButton.ScaleType.CENTER_INSIDE);
        b.setPadding(dp(10),dp(10),dp(10),dp(10));
        return b;
    }
    private void search(){
        String query=searchInput.getText().toString().trim();
        if(query.isEmpty()||searching)return;
        searching=true;
        trackCount.setText("Searching...");
        list.removeAllViews();
        TextView msg=text("Finding music...",14,SECONDARY,false);
        msg.setPadding(dp(12),dp(35),0,dp(35));
        add(list,msg,-1,-2);
        io.execute(()->{
            try{
                List<MusicCatalog.Song> result=MusicCatalog.search(query);
                runOnUiThread(()->render(result));
            }catch(Exception e){
                runOnUiThread(()->{
                    searching=false;
                    trackCount.setText("Unavailable");
                    list.removeAllViews();
                    TextView error=text("Search failed. Try another title or check your connection.",13,SECONDARY,false);
                    error.setPadding(0,dp(24),0,dp(24));
                    add(list,error,-1,-2);
                });
            }
        });
    }
    private void render(List<MusicCatalog.Song> songs){
        searching=false;
        trackCount.setText(songs.size()+" results");
        list.removeAllViews();
        if(songs.isEmpty()){
            add(list,text("No matching songs",14,SECONDARY,false),-1,-2);return;
        }
        int i=0;
        for(MusicCatalog.Song song:songs){
            final int number=++i;
            LinearLayout row=new LinearLayout(this);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setBackground(bg(SURFACE,13));
            row.setPadding(dp(14),dp(4),dp(12),dp(4));
            TextView num=text(String.format(Locale.ROOT,"%02d",number),11,SOFT,true);
            num.setGravity(Gravity.CENTER);
            add(row,num,32,50);
            LinearLayout details=column();
            details.setPadding(dp(8),0,dp(8),0);
            TextView title=text(song.title,14,PRIMARY,true);
            title.setSingleLine(true);title.setEllipsize(android.text.TextUtils.TruncateAt.END);
            add(details,title,-1,-2);
            TextView artist=text(song.artist==null?"":song.artist,11,SECONDARY,false);
            artist.setSingleLine(true);artist.setEllipsize(android.text.TextUtils.TruncateAt.END);
            artist.setPadding(0,dp(3),0,0);
            add(details,artist,-1,-2);
            row.addView(details,new LinearLayout.LayoutParams(0,-2,1));
            TextView length=text(format(song.durationMs),11,SOFT,false);
            add(row,length,-2,-2);
            row.setOnClickListener(v->{
                if(!PlayerService.play(this,song))headerStatus.setText("Start playback from the app first.");
                else headerStatus.setText("Connecting to stream...");
            });
            LinearLayout.LayoutParams params=new LinearLayout.LayoutParams(-1,dp(65));
            params.bottomMargin=dp(7);
            list.addView(row,params);
        }
    }
    private static String format(long ms){
        long seconds=Math.max(0,ms/1000);
        return String.format(Locale.ROOT,"%d:%02d",seconds/60,seconds%60);
    }
    private final Runnable poll=new Runnable(){
        @Override public void run(){
            JSONObject state=PlayerService.getStatus();
            if(state!=null){
                currentTitle.setText(state.optString("title","Nothing playing"));
                currentArtist.setText(state.optString("artist",""));
                boolean playing=state.optBoolean("playing");
                playButton.setImageResource(playing?R.drawable.ic_pause:R.drawable.ic_play);
                long position=state.optLong("positionMs"),duration=state.optLong("durationMs");
                currentDuration=(int)Math.min(Integer.MAX_VALUE,Math.max(0,duration));
                if(!dragging && duration>0){
                    timeline.setProgress((int)Math.min(1000,position*1000/duration));
                }
                timeLabel.setText(format(position)+" / "+format(duration));
                headerStatus.setText(state.optString("message",""));
            }
            handler.postDelayed(this,450);
        }
    };
    private void showBridgeSettings(){
        String token=BridgeService.getOrCreateToken(this);
        String info="Connect the Minecraft mod to this app.\n\n"
             +"1. Enable Notification Access for LeviMusic.\n"
             +"2. Copy the key below into the LeviMusic Bridge Key setting in Minecraft.\n"
             +"3. Start a song here and return to Minecraft.\n\n"
             +"PAIRING KEY:\n"+token+"\n\n"
             +"Notification Access is a sensitive system permission. Only enable it if you trust the app.";
        new AlertDialog.Builder(this)
            .setTitle("Minecraft connection")
            .setMessage(info)
            .setPositiveButton("Copy key",(dialog,which)->{
                ((ClipboardManager)getSystemService(CLIPBOARD_SERVICE))
                    .setPrimaryClip(ClipData.newPlainText("LeviMusic bridge key",token));
            })
            .setNeutralButton("Notification Access",(dialog,which)->{
                startActivity(new Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS));
            })
            .setNegativeButton("Close",null)
            .show();
    }
    @Override public void onDestroy(){
        handler.removeCallbacks(poll);io.shutdownNow();super.onDestroy();
    }
}
