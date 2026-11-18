package com.reaaurelis.levimusic.bridge;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.media.AudioManager;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import androidx.media3.common.AudioAttributes;
import androidx.media3.common.C;
import androidx.media3.common.MediaItem;
import androidx.media3.common.MediaMetadata;
import androidx.media3.common.PlaybackException;
import androidx.media3.common.Player;
import androidx.media3.exoplayer.ExoPlayer;
import androidx.media3.session.MediaSession;
import androidx.media3.session.MediaSessionService;
import org.json.JSONObject;

import java.util.concurrent.ExecutorService;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.Executors;

public final class PlayerService extends MediaSessionService {
    private static final String TAG="LeviMusicPlayer";
    private static final String CHANNEL="levimusic_player";
    private static final int NOTIFICATION_ID=1030;
    public static final String ACTION_PLAY="com.reaaurelis.levimusic.PLAY";
    public static final String ACTION_READY="com.reaaurelis.levimusic.READY";
    private static volatile PlayerService active;
    private final Handler handler=new Handler(Looper.getMainLooper());
    private final ExecutorService resolver=Executors.newSingleThreadExecutor();
    private ExoPlayer player;
    private MediaSession session;
    private volatile MusicCatalog.Song song;
    private volatile long positionMs=0L,durationMs=0L;
    private volatile boolean playing=false,loading=false;
    private volatile String statusMessage="Choose a song";
    private final ArrayDeque<String> recentSongs=new ArrayDeque<>();
    private volatile boolean autoplay=true;
    private int navigationGeneration=0;
    private boolean endQueued=false;

    public static boolean isActive(){return active!=null;}
    public static JSONObject getStatus() {
        PlayerService current=active;
        if(current==null || current.song==null)return null;
        return current.makeStatus();
    }
    public static boolean play(Context ctx,MusicCatalog.Song s) {
        if(s==null)return false;
        PlayerService instance=active;
        if(instance!=null){
            instance.handler.post(()->instance.startTrack(s));
            return true;
        }
        Intent intent=new Intent(ctx,PlayerService.class);
        intent.setAction(ACTION_PLAY);
        intent.putExtra("track_id",s.id);
        try{
            if(Build.VERSION.SDK_INT>=26)ctx.startForegroundService(intent);
            else ctx.startService(intent);
            return true;
        }catch(Exception e){
            Log.e(TAG,"Cannot start playback from background",e);
            return false;
        }
    }
    public static boolean command(String action,long seek) {
        PlayerService instance=active;
        if(instance==null||instance.player==null)return false;
        instance.handler.post(()->{
            try{
                switch(action){
                    case "play": instance.player.play();break;
                    case "pause": instance.player.pause();break;
                    case "seek": instance.player.seekTo(Math.max(0,seek));break;
                    case "next": instance.advance(1);break;
                    case "autoplay_on": instance.autoplay=true;break;
                    case "autoplay_off": instance.autoplay=false;break;
                    case "previous": instance.advance(-1);break;
                    default:break;
                }
            }catch(RuntimeException e){Log.w(TAG,"Media control error",e);}
        });
        return true;
    }
    private void advance(int direction){
        MusicCatalog.Song current=song;
        if(current==null)return;
        MusicCatalog.Song neighbor=MusicCatalog.neighbor(current.id,direction);
        if(neighbor!=null){startTrack(neighbor);return;}
        if(direction<0 || MusicCatalog.isPlaylistQueue()) {
            statusMessage="No previous/next playlist track";
            return;
        }
        // End of the search queue: extend it from actual YouTube related items.
        final int generation=++navigationGeneration;
        final List<String> exclude=new ArrayList<>(recentSongs);
        statusMessage="Finding related music...";
        loading=true;
        resolver.execute(()->{
            MusicCatalog.Song recommended=null;
            try{recommended=MusicCatalog.recommendAfter(current,exclude);}
            catch(Exception error){Log.w(TAG,"Could not recommend next song",error);}
            final MusicCatalog.Song next=recommended;
            handler.post(()->{
                if(player==null || generation!=navigationGeneration || song!=current)return;
                loading=false;
                if(next==null){
                    statusMessage="No related track available; search for another song";
                    return;
                }
                MusicCatalog.appendRecommendation(next);
                startTrack(next);
            });
        });
    }
    @Override public void onCreate(){
        super.onCreate();
        active=this;
        BridgeApi.ensureStarted(getApplicationContext());
        NotificationManager notifications=(NotificationManager)getSystemService(NOTIFICATION_SERVICE);
        if(notifications!=null && Build.VERSION.SDK_INT>=26){
            NotificationChannel chan=new NotificationChannel(CHANNEL,"LeviMusic Playback",NotificationManager.IMPORTANCE_LOW);
            chan.setDescription("Playback controller while playing music");
            notifications.createNotificationChannel(chan);
        }
        AudioAttributes audio=new AudioAttributes.Builder()
                .setUsage(C.USAGE_MEDIA).setContentType(C.AUDIO_CONTENT_TYPE_MUSIC).build();
        player=new ExoPlayer.Builder(this).build();
        player.setAudioAttributes(audio,true);
        player.setHandleAudioBecomingNoisy(true);
        player.addListener(new Player.Listener(){
            @Override public void onIsPlayingChanged(boolean value){playing=value;}
            @Override public void onPlaybackStateChanged(int state){
                if(state==Player.STATE_READY)loading=false;
                if(state==Player.STATE_ENDED){
                    playing=false;loading=false;
                    if(autoplay && !endQueued){
                        endQueued=true;
                        handler.post(()->{
                            endQueued=false;
                            if(player!=null && player.getPlaybackState()==Player.STATE_ENDED)
                                advance(1);
                        });
                    }
                }
            }
            @Override public void onPlayerError(PlaybackException error){
                loading=false;playing=false;
                statusMessage="Playback failed; try another track";
                Log.e(TAG,"ExoPlayer failed",error);
            }
        });
        session=new MediaSession.Builder(this,player).setId("levimusic").build();
        handler.post(snapshot);
    }
    private final Runnable snapshot=new Runnable(){
        @Override public void run(){
            if(player!=null){
                try{
                    positionMs=Math.max(0,player.getCurrentPosition());
                    durationMs=Math.max(0,player.getDuration());
                    playing=player.isPlaying();
                }catch(RuntimeException ignored){}
                handler.postDelayed(this,400);
            }
        }
    };
    @Override public int onStartCommand(Intent intent,int flags,int startId){
        int original=super.onStartCommand(intent,flags,startId);
        if(intent!=null&&ACTION_READY.equals(intent.getAction())) {
            try{startForeground(NOTIFICATION_ID,makeNotification("Connected to Minecraft"));
            }catch(Exception e){Log.w(TAG,"Idle player foreground service unavailable",e);}
            return START_STICKY;
        }
        if(intent!=null&&ACTION_PLAY.equals(intent.getAction())){
            try{startForeground(NOTIFICATION_ID,makeNotification("Connecting to music..."));}
            catch(Exception e){Log.e(TAG,"Cannot begin foreground media service",e);}
            MusicCatalog.Song selected=MusicCatalog.find(intent.getStringExtra("track_id"));
            if(selected!=null)startTrack(selected);
            return START_STICKY;
        }
        return original;
    }
    private Notification makeNotification(String caption){
        Intent open=new Intent(this,MainActivity.class);
        PendingIntent pending=PendingIntent.getActivity(this,0,open,
                PendingIntent.FLAG_IMMUTABLE|PendingIntent.FLAG_UPDATE_CURRENT);
        return new Notification.Builder(this,CHANNEL)
                .setSmallIcon(R.drawable.ic_notification)
                .setContentTitle("LeviMusic")
                .setContentText(caption)
                .setContentIntent(pending)
                .setOngoing(true)
                .build();
    }
    private void startTrack(MusicCatalog.Song selected){
        if(selected==null||player==null)return;
        navigationGeneration++;
        if(recentSongs.isEmpty() || !selected.id.equals(recentSongs.peekLast())){
            recentSongs.addLast(selected.id);
            while(recentSongs.size()>16)recentSongs.removeFirst();
        }
        song=selected;
        loading=true;playing=false;positionMs=0L;durationMs=selected.durationMs;
        statusMessage="Resolving stream...";
        player.stop();
        resolver.execute(()->{
            try{
                String source=MusicCatalog.resolveAudio(selected);
                handler.post(()->{
                    if(player==null||song!=selected)return;
                    MediaMetadata.Builder meta=new MediaMetadata.Builder()
                            .setTitle(selected.title).setArtist(selected.artist);
                    if(selected.art!=null && selected.art.startsWith("https://")){
                        meta.setArtworkUri(Uri.parse(selected.art));
                    }
                    MediaItem item=new MediaItem.Builder()
                            .setMediaId(selected.id)
                            .setUri(Uri.parse(source))
                            .setMediaMetadata(meta.build()).build();
                    player.setMediaItem(item);
                    player.prepare();
                    player.play();
                    statusMessage="Streaming";
                });
            }catch(Exception error){
                Log.w(TAG,"Stream unavailable",error);
                handler.post(()->{
                    if(song==selected){
                        loading=false;playing=false;
                        statusMessage="Stream unavailable: "+(error.getMessage()==null?"try another song":error.getMessage());
                    }
                });
            }
        });
    }
    private JSONObject makeStatus(){
        JSONObject json=new JSONObject();
        MusicCatalog.Song current=song;
        try{
            json.put("available",current!=null)
                .put("provider","LeviMusic • YouTube (unofficial)")
                .put("art",current==null?"":current.art)
                .put("id",current==null?"":current.id)
                .put("title",current==null?"":current.title)
                .put("artist",current==null?"":current.artist)
                .put("playing",playing)
                .put("loading",loading)
                .put("autoplay",autoplay)
                .put("queueMode",MusicCatalog.isPlaylistQueue()?"playlist":"recommendations")
                .put("positionMs",positionMs)
                .put("durationMs",durationMs)
                .put("message",statusMessage);
        }catch(Exception ignored){}
        return json;
    }
    @Override public MediaSession onGetSession(MediaSession.ControllerInfo info){return session;}
    @Override public void onDestroy(){
        active=null;
        handler.removeCallbacks(snapshot);
        resolver.shutdownNow();
        if(session!=null){session.release();session=null;}
        if(player!=null){player.release();player=null;}
        super.onDestroy();
    }
}
