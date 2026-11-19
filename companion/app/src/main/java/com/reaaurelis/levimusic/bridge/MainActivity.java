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
    private static final int BG=0xFF111216, SURFACE=0xFF1C1E23, SURFACE_ALT=0xFF282B30;
    private static final int PRIMARY=0xFFF4F4F6, SECONDARY=0xFFA0A3AB;
    private static final int SOFT=0xFF777B83;
    private final Handler handler=new Handler(Looper.getMainLooper());
    private final ExecutorService io=Executors.newSingleThreadExecutor();
    private final ExecutorService artIo=Executors.newFixedThreadPool(3);
    private TextView currentTitle,currentArtist,timeLabel,trackCount,empty,headerStatus;
    private LinearLayout list;
    private TextView libraryTitle;
    private boolean showingPlaylists=false;
    private LinearLayout mainArea, fullPlayer, miniPlayer, navigation;
    private TextView miniTitle, miniArtist, playerDetail;
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
        mainArea=column();
        root.addView(mainArea,new LinearLayout.LayoutParams(-1,0,1));

        LinearLayout listTitle=new LinearLayout(this);
        listTitle.setGravity(Gravity.CENTER_VERTICAL);
        libraryTitle=text("Discover",16,PRIMARY,true);
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
        LinearLayout.LayoutParams listTitleParams=new LinearLayout.LayoutParams(-1,dp(54));
        listTitleParams.topMargin=dp(10);
        mainArea.addView(listTitle,listTitleParams);

        ScrollView scroller=new ScrollView(this);
        scroller.setFillViewport(true);
        scroller.setVerticalScrollBarEnabled(false);
        list=column();
        list.setPadding(0,0,0,dp(22));
        scroller.addView(list);
        mainArea.addView(scroller,new LinearLayout.LayoutParams(-1,0,1));
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
        fullPlayer=player;
        root.addView(player,playerParams);

        LinearLayout row=new LinearLayout(this);
        row.setGravity(Gravity.CENTER_VERTICAL);
        currentCover=new ImageView(this);
        currentCover.setImageResource(R.drawable.ic_launcher);
        currentCover.setScaleType(ImageView.ScaleType.CENTER_CROP);
        currentCover.setBackground(bg(SURFACE_ALT,14));
        currentCover.setClipToOutline(true);
        add(row,currentCover,52,52);
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
        miniPlayer=new LinearLayout(this);
        miniPlayer.setGravity(Gravity.CENTER_VERTICAL);
        miniPlayer.setBackground(bg(SURFACE_ALT,18));
        miniPlayer.setPadding(dp(14),0,dp(8),0);
        LinearLayout labels=column();
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
            TextView button=text(i==0?"Home":i==1?"Playlists":"Settings",13,i==0?PRIMARY:SOFT,true);
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
        showingPlaylists=false;libraryTitle.setText("Discover");
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
        showingPlaylists=false;libraryTitle.setText("Discover");
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
        if(result!=RESULT_OK||data==null||data.getData()==null)return;
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
            item.setOnLongClickListener(v->{
                new AlertDialog.Builder(this).setTitle("Delete "+name+"?")
                    .setMessage("Remove this playlist? Songs are not deleted from the service.")
                    .setPositiveButton("Delete",(d,w)->{PlaylistStore.delete(this,name);showPlaylists();})
                    .setNegativeButton("Cancel",null).show();
                return true;
            });
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
            .setItems(new String[]{"Play now","Add to Up Next","Add to playlist"},(dialog,index)->{
                if(index==0){MusicCatalog.activateSearchQueue(song);PlayerService.play(this,song);}
                else if(index==1){
                    boolean added=MusicCatalog.enqueue(song);
                    headerStatus.setText(added?"Added to Up Next":"Already in Up Next");
                }else addToPlaylist(song);
            }).show();
    }
    private void selectTab(int tab){
        activeTab=tab;
        expandPlayer(false);
        searchInput.setVisibility(tab==0?View.VISIBLE:View.GONE);
        mainArea.setVisibility(View.VISIBLE);
        if(tab==0){render(lastSearch);libraryTitle.setText("Discover");}
        else if(tab==1)showPlaylists();
        else showSettingsTab();
        for(int i=0;i<navigation.getChildCount();i++)
            ((TextView)navigation.getChildAt(i)).setTextColor(i==tab?PRIMARY:SOFT);
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
        if(expanded){
            if(playerDetail==null){
                LinearLayout tabs=new LinearLayout(this);
                TextView queue=text("Up Next",15,PRIMARY,true);
                queue.setPadding(dp(14),dp(16),dp(14),dp(16));
                queue.setOnClickListener(v->showUpNext());
                tabs.addView(queue,new LinearLayout.LayoutParams(0,-2,1));
                TextView lyric=text("Lyrics",15,PRIMARY,true);
                lyric.setPadding(dp(14),dp(16),dp(14),dp(16));
                lyric.setOnClickListener(v->showPlayerLyrics());
                tabs.addView(lyric,new LinearLayout.LayoutParams(0,-2,1));
                add(fullPlayer,tabs,-1,-2);
                ScrollView scroll=new ScrollView(this);
                playerDetail=text("",15,SECONDARY,false);
                playerDetail.setPadding(dp(14),dp(12),dp(14),dp(12));
                scroll.addView(playerDetail);
                add(fullPlayer,scroll,-1,210);
                TextView close=text("⌄  Back to library",14,SECONDARY,true);
                close.setGravity(Gravity.CENTER);
                close.setOnClickListener(v->expandPlayer(false));
                add(fullPlayer,close,-1,46);
            }
            showUpNext();
        }
    }
    private void showUpNext(){
        JSONObject state=PlayerService.getStatus();
        String id=state==null?"":state.optString("id");
        org.json.JSONArray tracks=MusicCatalog.queueJson(id).optJSONArray("tracks");
        StringBuilder summary=new StringBuilder();
        if(tracks!=null)for(int i=0;i<tracks.length();i++){
            JSONObject song=tracks.optJSONObject(i);
            if(song!=null)summary.append(i==0?"NOW PLAYING  ":"NEXT  ")
                .append(song.optString("title")).append(" • ")
                .append(song.optString("artist")).append("\n\n");
        }
        playerDetail.setText(summary.length()==0?"Queue is empty":summary.toString());
    }
    private void showPlayerLyrics(){
        JSONObject state=PlayerService.getStatus();
        if(state==null||!state.optBoolean("available")){
            playerDetail.setText("Play a song to see its lyrics");return;
        }
        String title=state.optString("title"),artist=state.optString("artist");
        int duration=state.optInt("durationMs");
        MusicCatalog.Song song=MusicCatalog.find(state.optString("id"));
        playerDetail.setText("Loading lyrics…");
        io.execute(()->{
            JSONObject result=LyricsFinder.find(this,title,artist,duration,
                song==null?"":song.url,LyricsStore.romanizationLanguage(this));
            String raw=result.optString("syncedLyrics");
            final CharSequence display=raw.isEmpty()?"Lyrics unavailable. Import an LRC file in Settings.":
                LyricsRomanizer.richDisplay(raw,title,LyricsStore.romanizationLanguage(this),
                    LyricsStore.romanizeEnabled(this));
            runOnUiThread(()->playerDetail.setText(display));
        });
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
            }
            handler.postDelayed(this,450);
        }
    };
    private void loadArtInto(ImageView target,String url){
        if(url==null||!url.startsWith("https://"))return;
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
        String info="Connect the Minecraft mod to this app.\n\n"
             +"1. Notification Access is optional for the built-in music player.\n"
             +"2. Tap Pair Minecraft and then the PAIR control in game. No typing needed.\n"
             +"3. Start a song here and return to Minecraft.\n\n"
             +"PAIRING KEY:\n"+token+"\n\n"
             +"Notification Access is a sensitive system permission. Only enable it if you trust the app.";
        new AlertDialog.Builder(this)
            .setTitle("Minecraft connection")
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
    @Override public void onDestroy(){
        handler.removeCallbacks(poll);io.shutdownNow();artIo.shutdownNow();super.onDestroy();
    }
}


