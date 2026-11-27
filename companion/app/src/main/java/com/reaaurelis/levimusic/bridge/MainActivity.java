package com.reaaurelis.levimusic.bridge;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipboardManager;
import android.content.ClipData;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.widget.ImageView;
import java.io.InputStream;
import java.net.URL;
import java.util.LinkedHashMap;
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
    private static final int BG=0xFF10191E, SURFACE=0xFF1B292E, SURFACE_ALT=0xFF2B3C42;
    private static final int PRIMARY=0xFFF7FAFC, SECONDARY=0xFFB1C1C7;
    private static final int SOFT=0xFF80969E, ACCENT=0xFF91E9DB;
    private final Handler handler=new Handler(Looper.getMainLooper());
    private final ExecutorService io=Executors.newSingleThreadExecutor();
    private final ExecutorService artIo=Executors.newFixedThreadPool(3);
    private TextView currentTitle,currentArtist,timeLabel,trackCount,empty,headerStatus;
    private LinearLayout list;
    private TextView libraryTitle;
    private boolean showingPlaylists=false;
    private LinearLayout mainArea, fullPlayer, miniPlayer, navigation, queueList;
    private ScrollView queueScroll;
    private LyricKaraokePanel lyricPanel;
    private TextView miniTitle, miniArtist, queueTab, lyricsTab, playerEyebrow, heroNow;
    private ImageView largeCover,miniCover;
    private View searchBar,homeHero;
    private int detailTab=0;
    private String loadedLyricId="",requestedLyricId="",loadedQueueId="";
    private ImageButton miniPlay;
    private int activeTab=0;
    private java.util.List<MusicCatalog.Song> lastSearch=java.util.List.of();
    private EditText searchInput;
    private ImageButton playButton;
    private ImageView currentCover;
    private String currentCoverUrl="";
    private final LinkedHashMap<String,Bitmap> coverCache=new LinkedHashMap<String,Bitmap>(18,.75f,true){
        @Override protected boolean removeEldestEntry(java.util.Map.Entry<String,Bitmap> item){return size()>18;}
    };
    private SeekBar timeline;
    private boolean dragging=false,searching=false;
    private int currentDuration=0;
    private final Runnable lyricTicker=new Runnable(){
        @Override public void run(){
            if(fullPlayer!=null&&fullPlayer.getVisibility()==View.VISIBLE&&detailTab==1&&lyricPanel!=null){
                lyricPanel.update(PlayerService.getAccuratePosition());
            }
            handler.postDelayed(this,33);
        }
    };

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setStatusBarColor(BG);
        getWindow().setNavigationBarColor(BG);
        if(Build.VERSION.SDK_INT>=33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                !=PackageManager.PERMISSION_GRANTED)requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS},27);
        buildUI();
        BridgeApi.ensureStarted(getApplicationContext());
        Intent ready=new Intent(this,PlayerService.class).setAction(PlayerService.ACTION_READY);
        try { startForegroundService(ready); } catch(Exception ex){ android.util.Log.w("LeviMusic","Could not warm up media service",ex); }
        handler.post(poll);
        handler.post(lyricTicker);
        if(!getPreferences(MODE_PRIVATE).getBoolean("pair_tutorial_v170",false))
            handler.postDelayed(this::showPairingTutorial,450);
    }
    private int dp(float n){return (int)(getResources().getDisplayMetrics().density*n+.5f);}
    private GradientDrawable bg(int fill,float radius){
        GradientDrawable d=new GradientDrawable();
        d.setColor(fill);d.setCornerRadius(dp(radius));return d;
    }
    private GradientDrawable glow(int top,int bottom,float radius){
        GradientDrawable d=new GradientDrawable(GradientDrawable.Orientation.TL_BR,
            new int[]{top,bottom});d.setCornerRadius(dp(radius));return d;
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
        root.setPadding(dp(14),dp(8),dp(14),dp(9));

        LinearLayout header=new LinearLayout(this);
        header.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout titles=column();
        add(titles,text("LeviMusic - Aurelis Symphony",21,PRIMARY,true),-1,-2);
        TextView sub=text("UNIFIED BRIDGE & MUSIC ENGINE",9,ACCENT,true);
        sub.setLetterSpacing(.11f);
        sub.setPadding(0,dp(2),0,0);add(titles,sub,-1,-2);
        header.addView(titles,new LinearLayout.LayoutParams(0,-2,1));
        ImageButton settings=icon(R.drawable.ic_settings,42);
        settings.setBackground(bg(SURFACE,13));
        settings.setOnClickListener(v->showBridgeSettings());
        add(header,settings,42,42);
        add(root,header,-1,52);

        LinearLayout searchBox=new LinearLayout(this);
        searchBar=searchBox;
        searchBox.setGravity(Gravity.CENTER_VERTICAL);
        searchBox.setBackground(bg(SURFACE,19));
        searchBox.setPadding(dp(10),dp(5),dp(8),dp(5));
        searchInput=new EditText(this);
        searchInput.setSingleLine(true);
        searchInput.setTextSize(15);
        searchInput.setHintTextColor(SOFT);
        searchInput.setTextColor(PRIMARY);
        searchInput.setHint("Songs, artists, or moods…");
        searchInput.setPadding(dp(12),0,dp(10),0);
        searchInput.setBackgroundColor(Color.TRANSPARENT);
        searchInput.setImeOptions(EditorInfo.IME_ACTION_SEARCH);
        searchInput.setInputType(InputType.TYPE_CLASS_TEXT);
        searchBox.addView(searchInput,new LinearLayout.LayoutParams(0,dp(42),1));
        ImageButton go=icon(R.drawable.ic_search,40);
        go.setOnClickListener(v->search());
        add(searchBox,go,40,40);
        searchInput.setOnEditorActionListener((v,action,event)->{
            if(action==EditorInfo.IME_ACTION_SEARCH){search();return true;}return false;
        });
        LinearLayout.LayoutParams searchParams=new LinearLayout.LayoutParams(-1,dp(50));
        searchParams.topMargin=dp(8);
        root.addView(searchBox,searchParams);
        mainArea=column();
        root.addView(mainArea,new LinearLayout.LayoutParams(-1,0,1));

        LinearLayout hero=column();homeHero=hero;
        hero.setBackground(glow(0xFF315E5D,0xFF21353F,22));
        hero.setPadding(dp(16),dp(11),dp(15),dp(11));
        TextView eyebrow=text("✦  SOUNDTRACK FOR YOUR WORLD",10,ACCENT,true);
        eyebrow.setLetterSpacing(.11f);add(hero,eyebrow,-1,-2);
        heroNow=text("A new world, a new soundtrack.",17,PRIMARY,true);
        heroNow.setSingleLine(true);heroNow.setEllipsize(android.text.TextUtils.TruncateAt.END);
        heroNow.setPadding(0,dp(8),0,dp(2));add(hero,heroNow,-1,-2);
        TextView hint=text("Search for music or continue from the mini player",12,SECONDARY,false);
        add(hero,hint,-1,-2);
        hero.setOnClickListener(v->expandPlayer(true));
        LinearLayout.LayoutParams heroParams=new LinearLayout.LayoutParams(-1,-2);
        heroParams.topMargin=dp(8);mainArea.addView(hero,heroParams);

        LinearLayout listTitle=new LinearLayout(this);
        listTitle.setGravity(Gravity.CENTER_VERTICAL);
        libraryTitle=text("Discover music",18,PRIMARY,true);
        listTitle.addView(libraryTitle,new LinearLayout.LayoutParams(0,-2,1));
        TextView playlistTab=text("Playlists",13,PRIMARY,true);
        playlistTab.setPadding(dp(9),dp(10),dp(9),dp(10));
        playlistTab.setOnClickListener(v->showPlaylists());
        add(listTitle,playlistTab,-2,-2);
        TextView createTab=text("+ New",13,SECONDARY,true);
        createTab.setPadding(dp(8),dp(10),dp(8),dp(10));
        createTab.setOnClickListener(v->createPlaylist());
        add(listTitle,createTab,-2,-2);
        TextView lrcTab=text("LRC",12,SECONDARY,true);
        lrcTab.setPadding(dp(7),dp(10),dp(7),dp(10));
        lrcTab.setOnClickListener(v->importLyrics());
        add(listTitle,lrcTab,-2,-2);
        trackCount=text("",12,SECONDARY,false);
        add(listTitle,trackCount,-2,-2);
        LinearLayout.LayoutParams listTitleParams=new LinearLayout.LayoutParams(-1,dp(47));
        listTitleParams.topMargin=dp(5);
        mainArea.addView(listTitle,listTitleParams);

        ScrollView scroller=new ScrollView(this);
        scroller.setFillViewport(true);
        scroller.setVerticalScrollBarEnabled(false);
        list=column();
        list.setPadding(0,0,0,dp(22));
        scroller.addView(list);
        mainArea.addView(scroller,new LinearLayout.LayoutParams(-1,0,1));
        empty=text("Find a soundtrack for your Minecraft world.\nSearch above, then open the mini player for lyrics and Up Next.",
                14,SECONDARY,false);
        empty.setGravity(Gravity.CENTER);
        empty.setLineSpacing(dp(5),1.0f);
        empty.setPadding(dp(18),dp(45),dp(18),dp(22));
        add(list,empty,-1,-2);

        LinearLayout player=column();
        player.setBackground(glow(0xFF20393E,SURFACE,25));
        player.setPadding(dp(18),dp(15),dp(18),dp(12));
        LinearLayout.LayoutParams playerParams=new LinearLayout.LayoutParams(-1,0,1);
        playerParams.topMargin=dp(8);
        fullPlayer=player;
        root.addView(player,playerParams);

        LinearLayout row=new LinearLayout(this);
        row.setGravity(Gravity.CENTER_VERTICAL);
        currentCover=new ImageView(this);
        currentCover.setImageResource(R.drawable.ic_launcher);
        currentCover.setScaleType(ImageView.ScaleType.CENTER_CROP);
        currentCover.setBackground(bg(SURFACE_ALT,14));
        currentCover.setClipToOutline(true);
        add(row,currentCover,44,44);
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
        add(player,row,-1,49);

        playerEyebrow=text("NOW PLAYING  ·  BRIDGE",11,ACCENT,true);
        playerEyebrow.setLetterSpacing(.1f);
        playerEyebrow.setPadding(0,dp(14),0,dp(12));
        add(player,playerEyebrow,-1,-2);
        largeCover=new ImageView(this);
        largeCover.setImageResource(R.drawable.ic_launcher);
        largeCover.setScaleType(ImageView.ScaleType.CENTER_CROP);
        largeCover.setBackground(bg(SURFACE_ALT,20));
        largeCover.setClipToOutline(true);
        LinearLayout.LayoutParams artSize=new LinearLayout.LayoutParams(dp(164),dp(164));
        artSize.gravity=Gravity.CENTER_HORIZONTAL;artSize.bottomMargin=dp(14);
        player.addView(largeCover,artSize);

        timeline=new SeekBar(this);
        timeline.setMax(1000);
        timeline.setProgressTintList(android.content.res.ColorStateList.valueOf(ACCENT));
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
        playButton.setBackground(bg(0xFF397B75,16));
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

        headerStatus=text("Open Minecraft: Bridge pairs automatically; otherwise Aurelis fallback stays active",10,SOFT,false);
        headerStatus.setGravity(Gravity.CENTER);
        headerStatus.setPadding(0,dp(9),0,0);
        add(root,headerStatus,-1,25);
        miniPlayer=new LinearLayout(this);
        miniPlayer.setGravity(Gravity.CENTER_VERTICAL);
        miniPlayer.setBackground(glow(0xFF314A50,0xFF24363D,20));
        miniPlayer.setPadding(dp(14),0,dp(8),0);
        miniCover=new ImageView(this);
        miniCover.setImageResource(R.drawable.ic_launcher);
        miniCover.setScaleType(ImageView.ScaleType.CENTER_CROP);
        miniCover.setBackground(bg(SURFACE,11));miniCover.setClipToOutline(true);
        add(miniPlayer,miniCover,43,43);
        LinearLayout labels=column();
        labels.setPadding(dp(12),0,0,0);
        miniTitle=text("Nothing playing",14,PRIMARY,true);
        miniTitle.setSingleLine(true);
        miniTitle.setEllipsize(android.text.TextUtils.TruncateAt.END);
        miniArtist=text("Open player",11,SECONDARY,false);
        add(labels,miniTitle,-1,-2);add(labels,miniArtist,-1,-2);
        miniPlayer.addView(labels,new LinearLayout.LayoutParams(0,-2,1));
        miniPlay=icon(R.drawable.ic_play,46);
        miniPlay.setOnClickListener(v->{
            JSONObject status=PlayerService.getStatus();
            PlayerService.command(status!=null&&status.optBoolean("playing")?"pause":"play",0);
        });
        add(miniPlayer,miniPlay,48,48);
        miniPlayer.setOnClickListener(v->expandPlayer(true));
        root.addView(miniPlayer,new LinearLayout.LayoutParams(-1,dp(62)));
        navigation=new LinearLayout(this);
        for(int i=0;i<3;i++){
            final int tab=i;
            TextView button=text(i==0?"⌂  Home":i==1?"♫  Playlist":"⚙  Settings",13,i==0?ACCENT:SOFT,true);
            button.setGravity(Gravity.CENTER);
            button.setOnClickListener(v->selectTab(tab));
            navigation.addView(button,new LinearLayout.LayoutParams(0,dp(54),1));
        }
        root.addView(navigation,new LinearLayout.LayoutParams(-1,dp(54)));
        fullPlayer.setVisibility(View.GONE);
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
        showingPlaylists=false;libraryTitle.setText("Discover music");
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
        lastSearch=new java.util.ArrayList<>(songs);
        showingPlaylists=false;libraryTitle.setText("Discover music");
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
            ImageView thumbnail=new ImageView(this);
            thumbnail.setImageResource(R.drawable.ic_launcher);
            thumbnail.setScaleType(ImageView.ScaleType.CENTER_CROP);
            thumbnail.setBackground(bg(SURFACE_ALT,9));
            thumbnail.setClipToOutline(true);
            add(row,thumbnail,46,46);
            loadArtInto(thumbnail,song.art);
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
            TextView addSong=text("  ⋮  ",20,PRIMARY,true);
            addSong.setOnClickListener(v->songMenu(song));
            add(row,addSong,42,46);
            row.setOnClickListener(v->{
                MusicCatalog.activateSearchQueue(song);
                if(!PlayerService.play(this,song))headerStatus.setText("Start playback from the app first.");
                else headerStatus.setText("Connecting to stream...");
            });
            LinearLayout.LayoutParams params=new LinearLayout.LayoutParams(-1,dp(65));
            params.bottomMargin=dp(7);
            list.addView(row,params);
        }
    }

    private static final int REQUEST_LRC=1201;
    private static final int REQUEST_PLAYLIST_EXPORT=1202;
    private static final int REQUEST_PLAYLIST_IMPORT=1203;
    private static final int REQUEST_LOCAL_MUSIC=1204;

    private void importLocalMusic(){
        Intent intent=new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE,true);
        startActivityForResult(intent,REQUEST_LOCAL_MUSIC);
    }

    private void exportPlaylists(){
        Intent intent=new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("application/json");
        intent.putExtra(Intent.EXTRA_TITLE,"LeviMusic-playlists-backup.json");
        startActivityForResult(intent,REQUEST_PLAYLIST_EXPORT);
    }
    private void importPlaylists(){
        Intent intent=new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("application/json");
        startActivityForResult(intent,REQUEST_PLAYLIST_IMPORT);
    }

    private void importLyrics(){
        JSONObject current=PlayerService.getStatus();
        if(current==null||current.optString("title").isEmpty()){
            new AlertDialog.Builder(this).setTitle("Import synchronized lyrics")
                .setMessage("Play a song first. The selected LRC file will be matched to the current title and artist.")
                .setPositiveButton("OK",null).show();
            return;
        }
        Intent intent=new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("text/*");
        startActivityForResult(intent,REQUEST_LRC);
    }
    @Override protected void onActivityResult(int request,int result,Intent data){
        super.onActivityResult(request,result,data);
        if(result!=RESULT_OK||data==null)return;
        if(request==REQUEST_LOCAL_MUSIC){
            java.util.ArrayList<android.net.Uri> selected=new java.util.ArrayList<>();
            if(data.getClipData()!=null){
                for(int i=0;i<data.getClipData().getItemCount();i++)
                    selected.add(data.getClipData().getItemAt(i).getUri());
            }else if(data.getData()!=null)selected.add(data.getData());
            headerStatus.setText("Importing local music...");
            io.execute(()->{
                try{String message=OfflineLibrary.importFiles(this,selected);
                    handler.post(()->{headerStatus.setText(message);showPlaylists();});}
                catch(Exception error){handler.post(()->headerStatus.setText(
                    "Local import failed: "+error.getMessage()));}
            });
            return;
        }
        if(data.getData()==null)return;
        if(request==REQUEST_PLAYLIST_EXPORT){
            try(java.io.OutputStream out=getContentResolver().openOutputStream(data.getData())){
                if(out==null)throw new java.io.IOException("No output stream");
                out.write(PlaylistStore.exportJson(this).getBytes(java.nio.charset.StandardCharsets.UTF_8));
                out.flush();
                headerStatus.setText("Playlist backup saved successfully");
            }catch(Exception error){headerStatus.setText("Could not export playlists");}
            return;
        }
        if(request==REQUEST_PLAYLIST_IMPORT){
            try(java.io.InputStream in=getContentResolver().openInputStream(data.getData())){
                if(in==null)throw new java.io.IOException("No input stream");
                java.io.ByteArrayOutputStream output=new java.io.ByteArrayOutputStream();
                byte[] buffer=new byte[8192];
                int read;
                while((read=in.read(buffer))>=0){
                    if(output.size()+read>4_000_000)throw new java.io.IOException("Backup too large");
                    if(read>0)output.write(buffer,0,read);
                }
                String json=output.toString("UTF-8");
                JSONObject response=PlaylistStore.importBackup(this,json);
                headerStatus.setText(response.optString("message"));
                if(response.optBoolean("success"))showPlaylists();
            }catch(Exception error){headerStatus.setText("Unable to import playlist backup");}
            return;
        }
        if(request!=REQUEST_LRC)return;
        JSONObject current=PlayerService.getStatus();
        if(current==null)return;
        String title=current.optString("title"),artist=current.optString("artist");
        if(title.isEmpty())return;
        try(java.io.InputStream in=getContentResolver().openInputStream(data.getData())){
            if(in==null)return;
            java.io.ByteArrayOutputStream out=new java.io.ByteArrayOutputStream();
            byte[] buffer=new byte[4096];int count;
            while((count=in.read(buffer))!=-1){
                if(out.size()+count>250000)throw new java.io.IOException("LRC file too large");
                out.write(buffer,0,count);
            }
            String lrc=out.toString("UTF-8");
            boolean ok=LyricsStore.save(this,title,artist,lrc);
            headerStatus.setText(ok?"Synced LRC imported for "+title:"Invalid LRC timestamps");
        }catch(Exception ex){headerStatus.setText("Unable to import LRC file");}
    }
    private void createPlaylist(){
        EditText name=new EditText(this);
        name.setSingleLine(true);name.setHint("Playlist name");
        name.setFilters(new android.text.InputFilter[]{new android.text.InputFilter.LengthFilter(48)});
        name.setPadding(dp(18),dp(15),dp(18),dp(15));
        new AlertDialog.Builder(this).setTitle("Create playlist")
            .setView(name)
            .setPositiveButton("Create",(dialog,which)->{
                JSONObject result=PlaylistStore.create(this,name.getText().toString());
                headerStatus.setText(result.optString("message"));
                showPlaylists();
            }).setNegativeButton("Cancel",null).show();
    }
    private void importPublicPlaylist(){
        EditText url=new EditText(this);
        url.setSingleLine(true);url.setHint("https://music.youtube.com/playlist?list=...");
        url.setInputType(InputType.TYPE_CLASS_TEXT|InputType.TYPE_TEXT_VARIATION_URI);
        new AlertDialog.Builder(this).setTitle("Import public YouTube playlist")
            .setView(url).setNegativeButton("Cancel",null)
            .setPositiveButton("Import",(dialog,which)->{
                String link=url.getText().toString().trim();
                headerStatus.setText("Importing playlist...");
                io.execute(()->{
                    try{
                        MusicCatalog.ImportedPlaylist imported=MusicCatalog.importPublicPlaylist(link);
                        JSONObject result=PlaylistStore.importSongs(this,imported.name,imported.songs);
                        handler.post(()->{headerStatus.setText(result.optString("message"));showPlaylists();});
                    }catch(Exception error){
                        handler.post(()->headerStatus.setText("Import failed: "+error.getMessage()));
                    }
                });
            }).show();
    }
    private void editPlaylist(String name){
        new AlertDialog.Builder(this).setTitle(name)
            .setItems(new String[]{"Open","Rename","Delete"},(dialog,which)->{
                if(which==0){showPlaylistSongs(name);return;}
                if(which==1){
                    EditText input=new EditText(this);input.setSingleLine(true);input.setText(name);
                    input.setFilters(new android.text.InputFilter[]{new android.text.InputFilter.LengthFilter(48)});
                    new AlertDialog.Builder(this).setTitle("Rename playlist").setView(input)
                        .setNegativeButton("Cancel",null)
                        .setPositiveButton("Save",(d,w)->{
                            JSONObject result=PlaylistStore.rename(this,name,input.getText().toString());
                            headerStatus.setText(result.optString("message"));showPlaylists();
                        }).show();
                }else new AlertDialog.Builder(this).setTitle("Delete "+name+"?")
                    .setMessage("Remove this playlist? Songs are not deleted from the service.")
                    .setPositiveButton("Delete",(d,w)->{PlaylistStore.delete(this,name);showPlaylists();})
                    .setNegativeButton("Cancel",null).show();
            }).show();
    }
    private void showPlaylists(){
        showingPlaylists=true;libraryTitle.setText("My Playlists");
        trackCount.setText("");
        list.removeAllViews();
        LinearLayout backupControls=new LinearLayout(this);
        backupControls.setOrientation(LinearLayout.HORIZONTAL);
        backupControls.setGravity(Gravity.CENTER_VERTICAL);
        TextView save=text("Export backup",12,PRIMARY,true);
        save.setGravity(Gravity.CENTER);
        save.setBackground(bg(SURFACE,12));
        save.setOnClickListener(v->exportPlaylists());
        LinearLayout.LayoutParams saveParams=new LinearLayout.LayoutParams(0,dp(45),1);
        saveParams.setMargins(0,0,dp(7),0);
        backupControls.addView(save,saveParams);
        TextView restore=text("Import backup",12,PRIMARY,true);
        restore.setGravity(Gravity.CENTER);
        restore.setBackground(bg(SURFACE,12));
        restore.setOnClickListener(v->{
            new AlertDialog.Builder(this).setTitle("Restore playlist backup?")
                .setMessage("This replaces the playlists currently saved in LeviMusic. Export a backup first if needed.")
                .setPositiveButton("Choose file",(d,w)->importPlaylists())
                .setNegativeButton("Cancel",null).show();
        });
        backupControls.addView(restore,new LinearLayout.LayoutParams(0,dp(45),1));
        LinearLayout.LayoutParams backupMargin=new LinearLayout.LayoutParams(-1,dp(48));
        backupMargin.bottomMargin=dp(12);
        list.addView(backupControls,backupMargin);
        TextView importUrl=text("+ Import YouTube playlist URL",13,PRIMARY,true);
        importUrl.setPadding(dp(15),dp(15),dp(15),dp(15));
        importUrl.setBackground(bg(SURFACE,12));
        importUrl.setOnClickListener(v->importPublicPlaylist());
        LinearLayout.LayoutParams importMargin=new LinearLayout.LayoutParams(-1,-2);
        importMargin.bottomMargin=dp(12);list.addView(importUrl,importMargin);
        TextView importLocal=text("+ Import local audio and matching LRC",13,PRIMARY,true);
        importLocal.setPadding(dp(15),dp(15),dp(15),dp(15));
        importLocal.setBackground(bg(SURFACE,12));
        importLocal.setOnClickListener(v->importLocalMusic());
        LinearLayout.LayoutParams localMargin=new LinearLayout.LayoutParams(-1,-2);
        localMargin.bottomMargin=dp(12);list.addView(importLocal,localMargin);
        org.json.JSONArray items=PlaylistStore.list(this,"").optJSONArray("playlists");
        if(items==null||items.length()==0){
            TextView empty=text("No playlists yet. Tap + New to create one.",14,SECONDARY,false);
            empty.setPadding(dp(10),dp(34),dp(10),dp(16));add(list,empty,-1,-2);
            return;
        }
        for(int i=0;i<items.length();i++){
            JSONObject entry=items.optJSONObject(i);
            if(entry==null)continue;
            String name=entry.optString("name");
            TextView item=text("♫   "+name+"    ·    "+entry.optInt("count")+" songs",15,PRIMARY,true);
            item.setBackground(bg(SURFACE,14));
            item.setPadding(dp(18),dp(16),dp(12),dp(16));
            item.setOnClickListener(v->showPlaylistSongs(name));
            item.setOnLongClickListener(v->{editPlaylist(name);return true;});
            LinearLayout.LayoutParams row=new LinearLayout.LayoutParams(-1,-2);
            row.bottomMargin=dp(8);list.addView(item,row);
        }
    }
    private void showPlaylistSongs(String playlist){
        showingPlaylists=true;libraryTitle.setText(playlist);
        List<MusicCatalog.Song> songs=PlaylistStore.songs(this,playlist);
        trackCount.setText(songs.size()+" songs");
        list.removeAllViews();
        TextView back=text("‹  All playlists",13,SECONDARY,true);
        back.setPadding(dp(12),dp(10),0,dp(16));
        back.setOnClickListener(v->showPlaylists());add(list,back,-1,-2);
        for(int i=0;i<songs.size();i++){
            final int index=i;
            MusicCatalog.Song song=songs.get(i);
            LinearLayout row=new LinearLayout(this);
            row.setBackground(bg(SURFACE,12));
            row.setGravity(Gravity.CENTER_VERTICAL);
            TextView item=text(String.format(Locale.ROOT,"%02d",i+1)+"   "+song.title+
                "  ·  "+song.artist,13,PRIMARY,false);
            item.setSingleLine(true);item.setEllipsize(android.text.TextUtils.TruncateAt.END);
            row.addView(item,new LinearLayout.LayoutParams(0,dp(54),1));
            item.setGravity(Gravity.CENTER_VERTICAL);
            item.setPadding(dp(12),0,dp(6),0);
            TextView remove=text(" × ",18,SECONDARY,true);
            remove.setPadding(dp(10),dp(8),dp(10),dp(8));
            remove.setOnClickListener(v->{PlaylistStore.remove(this,playlist,song.id);showPlaylistSongs(playlist);});
            add(row,remove,48,54);
            row.setOnClickListener(v->{
                JSONObject response=PlaylistStore.play(this,playlist,index);
                headerStatus.setText(response.optString("message"));
            });
            LinearLayout.LayoutParams params=new LinearLayout.LayoutParams(-1,dp(54));
            params.bottomMargin=dp(7);list.addView(row,params);
        }
    }
    private void addToPlaylist(MusicCatalog.Song song){
        org.json.JSONArray items=PlaylistStore.list(this,"").optJSONArray("playlists");
        if(items==null||items.length()==0){
            new AlertDialog.Builder(this).setTitle("No playlists yet")
                .setMessage("Create a playlist first using + New.")
                .setPositiveButton("Create",(d,w)->createPlaylist())
                .setNegativeButton("Cancel",null).show();
            return;
        }
        String[] names=new String[items.length()];
        for(int i=0;i<items.length();i++){
            JSONObject entry=items.optJSONObject(i);
            names[i]=entry==null?"":entry.optString("name");
        }
        new AlertDialog.Builder(this).setTitle("Add to playlist")
            .setItems(names,(d,index)->{
                JSONObject result=PlaylistStore.add(this,names[index],song);
                headerStatus.setText(result.optString("message"));
            }).setNegativeButton("Cancel",null).show();
    }
    private void songMenu(MusicCatalog.Song song){
        new AlertDialog.Builder(this).setTitle(song.title)
            .setItems(new String[]{"Play now","Add to Up Next","Add to playlist","Save to Offline"},(dialog,index)->{
                if(index==0){MusicCatalog.activateSearchQueue(song);PlayerService.play(this,song);}
                else if(index==1){
                    boolean added=MusicCatalog.enqueue(song);
                    headerStatus.setText(added?"Added to Up Next":"Already in Up Next");
                }else if(index==2)addToPlaylist(song);
                else {
                    headerStatus.setText("Saving audio and lyrics...");
                    io.execute(()->{
                        try{String message=OfflineLibrary.save(this,song);
                            handler.post(()->headerStatus.setText(message));}
                        catch(Exception error){handler.post(()->headerStatus.setText(
                            "Offline save failed: "+error.getMessage()));}
                    });
                }
            }).show();
    }
    private void selectTab(int tab){
        activeTab=tab;
        expandPlayer(false);
        searchBar.setVisibility(tab==0?View.VISIBLE:View.GONE);
        mainArea.setVisibility(View.VISIBLE);
        homeHero.setVisibility(tab==0?View.VISIBLE:View.GONE);
        if(tab==0){render(lastSearch);libraryTitle.setText("Discover music");}
        else if(tab==1)showPlaylists();
        else showSettingsTab();
        for(int i=0;i<navigation.getChildCount();i++)
            ((TextView)navigation.getChildAt(i)).setTextColor(i==tab?ACCENT:SOFT);
    }
    private void showSettingsTab(){
        libraryTitle.setText("Settings");
        list.removeAllViews();
        android.widget.Switch roman=new android.widget.Switch(this);
        roman.setText("Romanize lyrics");
        roman.setTextColor(PRIMARY);
        roman.setTextSize(16);
        roman.setPadding(dp(14),dp(12),dp(14),dp(12));
        roman.setChecked(LyricsStore.romanizeEnabled(this));
        roman.setOnCheckedChangeListener((button,checked)->LyricsStore.setRomanize(this,checked));
        add(list,roman,-1,68);
        TextView language=text("Lyric language: "+LyricsStore.romanizationLanguage(this)+"  ›",14,PRIMARY,false);
        language.setPadding(dp(14),dp(12),dp(14),dp(12));
        language.setOnClickListener(v->new AlertDialog.Builder(this).setTitle("Romanization language")
            .setItems(new String[]{"Auto","Japanese","Chinese","Korean"},(dialog,index)->{
                LyricsStore.setRomanizationLanguage(this,new String[]{"auto","ja","zh","ko"}[index]);
                showSettingsTab();
            }).show());
        add(list,language,-1,50);
        TextView note=text("Original lyrics stay visible. Japanese kanji readings use a Japanese dictionary; choose Japanese for titles written only in kanji.",12,SECONDARY,false);
        note.setPadding(dp(14),0,dp(14),dp(20));
        add(list,note,-1,-2);
        TextView beats=text("Allow audio analysis for experimental 3D beats  ›",15,PRIMARY,true);
        beats.setPadding(dp(14),dp(18),dp(14),dp(18));
        beats.setOnClickListener(v->{
            if(checkSelfPermission(Manifest.permission.RECORD_AUDIO)!=PackageManager.PERMISSION_GRANTED)
                requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO},28);
            else new AlertDialog.Builder(this).setMessage("Audio analysis is ready for songs played in LeviMusic. Enable 3D Lyrics Beats in the Minecraft mod.")
                .setPositiveButton("OK",null).show();
        });
        add(list,beats,-1,72);
        TextView connection=text("Minecraft connection  ›",16,PRIMARY,true);
        connection.setPadding(dp(14),dp(18),dp(14),dp(18));
        connection.setBackground(bg(SURFACE,14));
        connection.setOnClickListener(v->showBridgeSettings());
        add(list,connection,-1,62);
        TextView importLrc=text("Import synchronized lyrics  ›",16,PRIMARY,true);
        importLrc.setPadding(dp(14),dp(18),dp(14),dp(18));
        importLrc.setOnClickListener(v->importLyrics());
        add(list,importLrc,-1,62);
    }
    private void expandPlayer(boolean expanded){
        mainArea.setVisibility(expanded?View.GONE:View.VISIBLE);
        fullPlayer.setVisibility(expanded?View.VISIBLE:View.GONE);
        miniPlayer.setVisibility(expanded?View.GONE:View.VISIBLE);
        navigation.setVisibility(expanded?View.GONE:View.VISIBLE);
        searchBar.setVisibility(expanded?View.GONE:activeTab==0?View.VISIBLE:View.GONE);
        if(expanded){
            if(queueList==null){
                LinearLayout tabs=new LinearLayout(this);
                tabs.setPadding(0,dp(14),0,dp(8));
                queueTab=text("♫  Up Next",15,PRIMARY,true);
                queueTab.setGravity(Gravity.CENTER);queueTab.setPadding(dp(6),dp(12),dp(6),dp(12));
                queueTab.setOnClickListener(v->showUpNext());
                tabs.addView(queueTab,new LinearLayout.LayoutParams(0,-2,1));
                lyricsTab=text("✦  Lyrics",15,SECONDARY,true);
                lyricsTab.setGravity(Gravity.CENTER);lyricsTab.setPadding(dp(6),dp(12),dp(6),dp(12));
                lyricsTab.setOnClickListener(v->showPlayerLyrics());
                tabs.addView(lyricsTab,new LinearLayout.LayoutParams(0,-2,1));
                add(fullPlayer,tabs,-1,-2);
                queueScroll=new ScrollView(this);
                queueScroll.setFillViewport(true);queueScroll.setVerticalScrollBarEnabled(false);
                queueList=column();queueList.setPadding(dp(4),dp(8),dp(4),dp(30));
                queueScroll.addView(queueList);
                fullPlayer.addView(queueScroll,new LinearLayout.LayoutParams(-1,0,1));
                lyricPanel=new LyricKaraokePanel(this);
                fullPlayer.addView(lyricPanel,new LinearLayout.LayoutParams(-1,0,1));
                lyricPanel.setVisibility(View.GONE);
                TextView close=text("⌄  Kembali ke beranda",14,SECONDARY,true);
                close.setGravity(Gravity.CENTER);
                close.setOnClickListener(v->expandPlayer(false));
                add(fullPlayer,close,-1,40);
            }
            if(detailTab==1)showPlayerLyrics();else showUpNext();
        }
    }
    private void setDetailTab(int tab){
        detailTab=tab;
        queueScroll.setVisibility(tab==0?View.VISIBLE:View.GONE);
        lyricPanel.setVisibility(tab==1?View.VISIBLE:View.GONE);
        queueTab.setTextColor(tab==0?ACCENT:SECONDARY);
        lyricsTab.setTextColor(tab==1?ACCENT:SECONDARY);
        queueTab.setBackground(bg(tab==0?SURFACE_ALT:SURFACE,14));
        lyricsTab.setBackground(bg(tab==1?SURFACE_ALT:SURFACE,14));
    }
    private void showUpNext(){
        if(queueList==null)return;
        setDetailTab(0);
        JSONObject state=PlayerService.getStatus();
        String id=state==null?"":state.optString("id");
        org.json.JSONArray tracks=MusicCatalog.queueJson(id).optJSONArray("tracks");
        queueList.removeAllViews();
        TextView eyebrow=text("ANTREAN PUTAR",11,ACCENT,true);
        eyebrow.setLetterSpacing(.12f);eyebrow.setPadding(dp(12),dp(8),0,dp(16));
        add(queueList,eyebrow,-1,-2);
        if(tracks!=null)for(int i=0;i<tracks.length();i++){
            JSONObject item=tracks.optJSONObject(i);
            if(item==null)continue;
            String trackId=item.optString("id");
            MusicCatalog.Song song=MusicCatalog.find(trackId);
            LinearLayout row=new LinearLayout(this);row.setGravity(Gravity.CENTER_VERTICAL);
            row.setBackground(bg(i==0?0xFF2D5052:SURFACE_ALT,16));
            row.setPadding(dp(12),dp(9),dp(10),dp(9));
            TextView order=text(i==0?"▶":String.format(Locale.ROOT,"%02d",i+1),13,ACCENT,true);
            order.setGravity(Gravity.CENTER);add(row,order,34,42);
            LinearLayout detail=column();detail.setPadding(dp(8),0,dp(4),0);
            TextView name=text(item.optString("title"),14,PRIMARY,true);
            name.setSingleLine(true);name.setEllipsize(android.text.TextUtils.TruncateAt.END);
            add(detail,name,-1,-2);
            TextView artist=text(item.optString("artist"),11,SECONDARY,false);
            artist.setSingleLine(true);artist.setEllipsize(android.text.TextUtils.TruncateAt.END);
            add(detail,artist,-1,-2);
            row.addView(detail,new LinearLayout.LayoutParams(0,-2,1));
            if(i>0){
                TextView remove=text("×",22,SECONDARY,false);remove.setGravity(Gravity.CENTER);
                remove.setOnClickListener(v->{MusicCatalog.removeUpcoming(trackId,id);
                    PlayerService.requestRadioRefill();showUpNext();});
                add(row,remove,38,42);
            }
            if(song!=null)row.setOnClickListener(v->{PlayerService.play(this,song);showUpNext();});
            LinearLayout.LayoutParams slot=new LinearLayout.LayoutParams(-1,-2);
            slot.bottomMargin=dp(8);queueList.addView(row,slot);
        }
        if(tracks==null||tracks.length()==0){
            TextView help=text("Up Next is empty. Search for a song and add it from the ⋮ menu.",14,SECONDARY,false);
            help.setPadding(dp(14),dp(20),dp(14),dp(20));add(queueList,help,-1,-2);
        }
        loadedQueueId=id;
    }
    private void showPlayerLyrics(){
        if(lyricPanel==null)return;
        setDetailTab(1);
        JSONObject state=PlayerService.getStatus();
        if(state==null||!state.optBoolean("available")){
            lyricPanel.showMessage("Play a song to view synchronized lyrics");return;
        }
        String id=state.optString("id");
        if(id.equals(loadedLyricId)||id.equals(requestedLyricId))return;
        String title=state.optString("title"),artist=state.optString("artist");
        int duration=state.optInt("durationMs");
        MusicCatalog.Song song=MusicCatalog.find(id);
        requestedLyricId=id;
        lyricPanel.showMessage("Finding synchronized lyrics…");
        io.execute(()->{
            JSONObject result=LyricsFinder.find(this,title,artist,duration,
                song==null?"":song.url,LyricsStore.romanizationLanguage(this));
            String raw=result.optString("syncedLyrics");
            String reading=raw.isEmpty()||!LyricsStore.romanizeEnabled(this)?"":
                LyricsRomanizer.lrc(raw,title,LyricsStore.romanizationLanguage(this));
            runOnUiThread(()->{
                if(!id.equals(requestedLyricId))return;
                loadedLyricId=id;requestedLyricId="";
                lyricPanel.setLyrics(raw,reading,result.optString("source"));
                lyricPanel.update(PlayerService.getAccuratePosition());
            });
        });
    }
    @Override public void onBackPressed(){
        if(fullPlayer.getVisibility()==View.VISIBLE){expandPlayer(false);return;}
        super.onBackPressed();
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
                miniTitle.setText(state.optString("title","Nothing playing"));
                miniArtist.setText(state.optString("artist",""));
                heroNow.setText(state.optString("title","A new world, a new soundtrack."));
                miniPlay.setImageResource(state.optBoolean("playing")?R.drawable.ic_pause:R.drawable.ic_play);
                boolean playing=state.optBoolean("playing");
                playButton.setImageResource(playing?R.drawable.ic_pause:R.drawable.ic_play);
                long position=state.optLong("positionMs"),duration=state.optLong("durationMs");
                currentDuration=(int)Math.min(Integer.MAX_VALUE,Math.max(0,duration));
                if(!dragging && duration>0){
                    timeline.setProgress((int)Math.min(1000,position*1000/duration));
                }
                timeLabel.setText(format(position)+" / "+format(duration));
                headerStatus.setText(state.optString("message",""));
                showCover(state.optString("art",""));
                String id=state.optString("id");
                if(fullPlayer.getVisibility()==View.VISIBLE){
                    if(detailTab==1&&!id.equals(loadedLyricId)&&!id.equals(requestedLyricId))
                        showPlayerLyrics();
                    else if(detailTab==0&&!id.equals(loadedQueueId))showUpNext();
                }
            }
            handler.postDelayed(this,450);
        }
    };
    private void loadArtInto(ImageView target,String url){
        if(url==null||!url.startsWith("https://")){
            target.setTag(null);target.setImageResource(R.drawable.ic_launcher);return;
        }
        if(url.equals(target.getTag()))return;
        target.setTag(url);
        Bitmap cached;
        synchronized(coverCache){cached=coverCache.get(url);}
        if(cached!=null){target.setImageBitmap(cached);return;}
        final String request=url;
        artIo.execute(()->{
            try{
                java.net.URLConnection connection=new URL(request).openConnection();
                connection.setConnectTimeout(4500);
                connection.setReadTimeout(4500);
                try(InputStream stream=connection.getInputStream()){
                    BitmapFactory.Options options=new BitmapFactory.Options();
                    options.inSampleSize=2;
                    Bitmap bitmap=BitmapFactory.decodeStream(stream,null,options);
                    if(bitmap==null)return;
                    synchronized(coverCache){coverCache.put(request,bitmap);}
                    runOnUiThread(()->{
                        if(target!=null && request.equals(target.getTag()))
                            target.setImageBitmap(bitmap);
                    });
                }
            }catch(Exception ignored){}
        });
    }
    private void showCover(String url){
        if(url==null)url="";
        if(url.equals(currentCoverUrl))return;
        currentCoverUrl=url;
        loadArtInto(largeCover,url);
        loadArtInto(miniCover,url);
        if(url.isEmpty()||!url.startsWith("https://")){
            currentCover.setImageResource(R.drawable.ic_launcher);
            return;
        }
        final String requested=url;
        Bitmap fromCache;
        synchronized(coverCache){fromCache=coverCache.get(requested);}
        if(fromCache!=null){currentCover.setImageBitmap(fromCache);return;}
        currentCover.setImageResource(R.drawable.ic_launcher);
        artIo.execute(()->{
            try{
                URL remote=new URL(requested);
                java.net.URLConnection connection=remote.openConnection();
                connection.setConnectTimeout(5500);
                connection.setReadTimeout(5500);
                try(InputStream stream=connection.getInputStream()){
                    BitmapFactory.Options options=new BitmapFactory.Options();
                    options.inSampleSize=2;
                    Bitmap bitmap=BitmapFactory.decodeStream(stream,null,options);
                    if(bitmap==null)return;
                    synchronized(coverCache){coverCache.put(requested,bitmap);}
                    runOnUiThread(()->{
                        if(requested.equals(currentCoverUrl)&&currentCover!=null)
                            currentCover.setImageBitmap(bitmap);
                    });
                }
            }catch(Exception ignored){}
        });
    }
    private void showBridgeSettings(){
        String token=BridgeService.getOrCreateToken(this);
        String info="Connect Minecraft to Aurelis Symphony.\n\n"
             +"1. Open this app once so the local Bridge is ready.\n"
             +"2. Enable LeviMusic 1.7.0 in LeviLauncher and enter a world.\n"
             +"3. Wait up to four seconds: the mod switches to Bridge automatically.\n"
             +"4. If the app is unavailable, the same mod returns to Aurelis fallback.\n\n"
             +"PAIRING KEY:\n"+token+"\n\n"
             +"Copy the key only for manual recovery. Notification Access is optional.";
        new AlertDialog.Builder(this)
            .setTitle("Pair Minecraft")
            .setMessage(info)
            .setPositiveButton("Pair Minecraft (copy)",(dialog,which)->{
                ((ClipboardManager)getSystemService(CLIPBOARD_SERVICE))
                    .setPrimaryClip(ClipData.newPlainText("LeviMusic bridge key",token));
            })
            .setNeutralButton("Notification Access",(dialog,which)->{
                startActivity(new Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS));
            })
            .setNegativeButton("Close",null)
            .show();
    }
    private void showPairingTutorial(){
        String info="One LeviPack now handles both modes.\n\n"
            +"• Without this app: Aurelis fallback searches and plays music directly.\n"
            +"• With this app open: Bridge is detected and selected automatically.\n"
            +"• The in-game AUTO / BRIDGE badge shows the active engine.\n"
            +"• Open Settings any time to copy the recovery key or grant optional Notification Access.";
        new AlertDialog.Builder(this)
            .setTitle("Quick pairing tutorial")
            .setMessage(info)
            .setPositiveButton("Got it",(dialog,which)->
                getPreferences(MODE_PRIVATE).edit().putBoolean("pair_tutorial_v170",true).apply())
            .setNeutralButton("Pairing details",(dialog,which)->showBridgeSettings())
            .setCancelable(false)
            .show();
    }
    @Override public void onDestroy(){
        handler.removeCallbacks(poll);handler.removeCallbacks(lyricTicker);
        io.shutdownNow();artIo.shutdownNow();super.onDestroy();
    }
}


