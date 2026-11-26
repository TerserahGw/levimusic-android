package com.reaaurelis.levimusic.bridge;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.media.AudioManager;
import android.media.audiofx.Visualizer;
import android.media.audiofx.Equalizer;
import android.media.audiofx.PresetReverb;
import android.Manifest;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;

import androidx.media3.common.AudioAttributes;
import androidx.media3.common.AuxEffectInfo;
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
    private final ExecutorService radioWorker=Executors.newSingleThreadExecutor();
    private ExoPlayer player;
    private Visualizer beatVisualizer;
    private int beatSession=-1;
    private volatile float beatValue=0f;
    private volatile float beatRaw=0f;
    private volatile long beatCount=0L;
    private long lastBeatPulseAt=0L;
    private float beatAverage=.04f;
    private float bassAverage=.04f;
    private volatile boolean beatCaptured=false;
    private long beatRetryAt=0;
    private volatile boolean beatEnabled=false;
    private MediaSession session;
    private volatile MusicCatalog.Song song;
    private volatile long positionMs=0L,durationMs=0L;
    private volatile boolean playing=false,loading=false;
    private volatile String statusMessage="Choose a song";
    private final ArrayDeque<String> recentSongs=new ArrayDeque<>();
    private volatile boolean autoplay=true;
    private volatile String repeatMode="all";
    private volatile String immersiveEnvironment="open";
    private Equalizer immersiveEq;
    private PresetReverb immersiveReverb;
    private int immersiveSession=-1;
    private String appliedImmersiveEnvironment="open";
    private int navigationGeneration=0;
    private boolean endQueued=false;
    private boolean radioFilling=false, pendingNext=false;

    public static boolean isActive(){return active!=null;}
    public static void setEnvironment(String value){
        PlayerService current=active;
        if(current==null)return;
        String safe="underwater".equals(value)?"underwater":"cave".equals(value)?"cave":"open";
        current.handler.post(()->{current.immersiveEnvironment=safe;current.applyImmersiveSound();});
    }
    private void releaseImmersiveSound(){
        if(immersiveEq!=null){try{immersiveEq.release();}catch(RuntimeException ignored){}immersiveEq=null;}
        if(immersiveReverb!=null){try{immersiveReverb.release();}catch(RuntimeException ignored){}immersiveReverb=null;}
        immersiveSession=-1;
        appliedImmersiveEnvironment="open";
        if(player!=null)try{player.setAuxEffectInfo(new AuxEffectInfo(0,0f));}catch(RuntimeException ignored){}
    }
    private static short clampBandLevel(int value,short[] range){
        return (short)Math.max(range[0],Math.min(range[1],value));
    }
    private void applyImmersiveSound(){
        if(player==null)return;
        if("open".equals(immersiveEnvironment)){releaseImmersiveSound();return;}
        int session=player.getAudioSessionId();if(session<=0)return;
        if(session!=immersiveSession){
            releaseImmersiveSound();
            try{
                immersiveEq=new Equalizer(0,session);immersiveEq.setEnabled(true);
                immersiveReverb=new PresetReverb(0,0);
                immersiveReverb.setEnabled(true);
                immersiveSession=session;
            }catch(RuntimeException error){releaseImmersiveSound();Log.w(TAG,"Immersive audio unavailable",error);return;}
        }
        boolean changed=!immersiveEnvironment.equals(appliedImmersiveEnvironment);
        if(immersiveReverb!=null&&changed)try{
            boolean underwater="underwater".equals(immersiveEnvironment);
            immersiveReverb.setPreset(underwater?PresetReverb.PRESET_SMALLROOM:PresetReverb.PRESET_LARGEHALL);
            immersiveReverb.setEnabled(true);
            player.setAuxEffectInfo(new AuxEffectInfo(immersiveReverb.getId(),underwater?.12f:.86f));
        }catch(RuntimeException error){Log.w(TAG,"Immersive reverb update failed",error);}
        if(immersiveEq!=null)try{
            short bands=immersiveEq.getNumberOfBands();
            short[] range=immersiveEq.getBandLevelRange();
            for(short band=0;band<bands;band++){
                short level=0;
                if("underwater".equals(immersiveEnvironment))
                    level=clampBandLevel(band<Math.max(1,bands/3)?260:-1450,range);
                else level=clampBandLevel(band<Math.max(1,bands/3)?180:-260,range);
                immersiveEq.setBandLevel(band,level);
            }
            immersiveEq.setEnabled(true);
            appliedImmersiveEnvironment=immersiveEnvironment;
        }catch(RuntimeException error){Log.w(TAG,"Immersive equalizer update failed",error);}
    }
    public static void setBeatEnabled(boolean enabled){
        PlayerService current=active;
        if(current!=null&&current.beatEnabled==enabled)return;
        if(current!=null)current.handler.post(()->{
            current.beatEnabled=enabled;
            if(!enabled)current.releaseBeat();
        });
    }
    public static JSONObject getBeatStatus(Context context){
        PlayerService current=active;
        boolean permitted=context.checkSelfPermission(Manifest.permission.RECORD_AUDIO)==PackageManager.PERMISSION_GRANTED;
        JSONObject result=new JSONObject();
        try{result.put("enabled",current!=null&&current.beatEnabled)
            .put("available",current!=null&&current.beatCaptured&&current.playing&&permitted)
            .put("permissionRequired",!permitted)
            .put("strength",current==null?0:current.beatValue)
            .put("beatCount",current==null?0:current.beatCount)
            .put("rawLevel",current==null?0:current.beatRaw)
            .put("reason",!permitted?"grant RECORD_AUDIO in LeviMusic Settings":
                current==null?"open LeviMusic player":
                !current.playing?"play a song in LeviMusic":
                !current.beatCaptured?"waiting for audio capture":"audio connected");}
        catch(Exception ignored){}
        return result;
    }
    private void releaseBeat(){
        beatValue=0f;beatRaw=0f;beatCaptured=false;beatSession=-1;
        if(beatVisualizer!=null){
            try{beatVisualizer.setEnabled(false);beatVisualizer.release();}catch(RuntimeException ignored){}
            beatVisualizer=null;
        }
    }
    private void captureBeat(){
        if(!beatEnabled||player==null||!player.isPlaying()
            ||checkSelfPermission(Manifest.permission.RECORD_AUDIO)!=PackageManager.PERMISSION_GRANTED){
            beatValue=0f;beatCaptured=false;return;
        }
        if(SystemClock.uptimeMillis()<beatRetryAt)return;
        int session=player.getAudioSessionId();
        if(session<=0){beatValue=0f;return;}
        if(beatVisualizer==null||beatSession!=session){
            releaseBeat();
            try{
                beatVisualizer=new Visualizer(session);
                beatVisualizer.setCaptureSize(Visualizer.getCaptureSizeRange()[0]);
                beatVisualizer.setEnabled(true);beatSession=session;
            }catch(RuntimeException ex){
                releaseBeat();beatRetryAt=SystemClock.uptimeMillis()+3000;
                Log.w(TAG,"Audio visualizer unavailable",ex);return;
            }
        }
        try{
            byte[] wave=new byte[beatVisualizer.getCaptureSize()];
            if(beatVisualizer.getWaveForm(wave)!=Visualizer.SUCCESS)return;
            // Waveform amplitude is real audio data from this player's session.
            // Smooth the release so short transients remain visible for a frame.
            double sum=0;
            for(byte sample:wave){int centered=(sample&255)-128;sum+=centered*centered;}
            float rms=(float)Math.sqrt(sum/wave.length)/55f;
            beatRaw=rms;beatCaptured=true;
            byte[] fft=new byte[wave.length];
            float bass=0f;
            if(beatVisualizer.getFft(fft)==Visualizer.SUCCESS){
                int bins=Math.min(10,fft.length/2-1);
                for(int i=1;i<=bins;i++){
                    int real=fft[i*2],imag=fft[i*2+1];
                    bass+=(float)Math.sqrt(real*real+imag*imag);
                }
                bass=bins>0?bass/(bins*80f):0f;
            }
            // Adapt to quiet and loud tracks: a fixed RMS divisor made the
            // previous release almost invisible on normalized streams.
            beatAverage=beatAverage*.965f+rms*.035f;
            bassAverage=bassAverage*.94f+bass*.06f;
            float onset=Math.max(0f,(rms-beatAverage)/(beatAverage+.025f));
            float bassHit=Math.max(0f,(bass-bassAverage)/(bassAverage+.025f));
            long tick=SystemClock.uptimeMillis();
            if((onset>.34f||bassHit>.36f)&&tick-lastBeatPulseAt>220){
                ++beatCount;lastBeatPulseAt=tick;
            }
            float visible=Math.min(1f,Math.max(onset*1.8f,bassHit*1.4f)
                +Math.min(.38f,rms*1.3f));
            beatValue=Math.max(visible,beatValue*.77f);
        }catch(RuntimeException ex){
            releaseBeat();beatRetryAt=SystemClock.uptimeMillis()+3000;
            Log.w(TAG,"Audio capture interrupted",ex);
        }
    }
    public static JSONObject getStatus() {
        PlayerService current=active;
        if(current==null || current.song==null)return null;
        return current.makeStatus();
    }
    /** Read on the main thread for frame-accurate lyric highlighting between status snapshots. */
    public static long getAccuratePosition() {
        PlayerService current=active;
        if(current==null)return 0;
        try{return current.player==null?current.positionMs:
            Math.max(0,current.player.getCurrentPosition());}
        catch(RuntimeException ignored){return current.positionMs;}
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
                    case "next": instance.advance(1,false);break;
                    case "autoplay_on": instance.autoplay=true;break;
                    case "autoplay_off": instance.autoplay=false;break;
                    case "previous": instance.advance(-1,false);break;
                    case "repeat_cycle":
                        instance.repeatMode="all".equals(instance.repeatMode)?"one":
                                "one".equals(instance.repeatMode)?"off":"all";break;
                    case "shuffle_toggle": {
                        String id=instance.song==null?"":instance.song.id;
                        MusicCatalog.setShuffle(!MusicCatalog.isShuffle(),id);break;
                    }
                    default:break;
                }
            }catch(RuntimeException e){Log.w(TAG,"Media control error",e);}
        });
        return true;
    }
    public static void requestRadioRefill(){
        PlayerService instance=active;
        if(instance!=null)instance.handler.post(instance::fillRadio);
    }
    private void advance(int direction,boolean automatic){
        MusicCatalog.Song current=song;
        if(current==null)return;
        if(automatic&&"one".equals(repeatMode)){startTrack(current);return;}
        boolean wrap=MusicCatalog.isPlaylistQueue()||"all".equals(repeatMode);
        MusicCatalog.Song next=MusicCatalog.neighbor(current.id,direction,wrap);
        if(next!=null){startTrack(next);return;}
        if(direction<0 || MusicCatalog.isPlaylistQueue()){
            statusMessage="No previous or next playlist track";
            return;
        }
        pendingNext=true;
        statusMessage="Finding a recommended song...";
        fillRadio();
    }
    /**
     * Keep the current song + exactly three visible upcoming radio suggestions.
     * The displayed queue is independent of search-result order.
     */
    private void fillRadio(){
        MusicCatalog.Song current=song;
        if(current==null || MusicCatalog.isPlaylistQueue() || radioFilling)return;
        int required=3-MusicCatalog.upcomingCount(current.id);
        if(required<=0){
            if(pendingNext){pendingNext=false;advance(1,false);}
            return;
        }
        radioFilling=true;
        final int generation=navigationGeneration;
        final List<String> excluded=MusicCatalog.excludedIds(new ArrayList<>(recentSongs));
        radioWorker.execute(()->{
            List<MusicCatalog.Song> suggestions=List.of();
            try{suggestions=MusicCatalog.recommendMany(current,excluded,required);}
            catch(Exception e){Log.w(TAG,"Recommendation batch failed",e);}
            final List<MusicCatalog.Song> batch=suggestions;
            handler.post(()->{
                radioFilling=false;
                if(player==null || song!=current || navigationGeneration!=generation) {
                    // An old batch must never overwrite an entirely new radio.
                    if(song!=null && !MusicCatalog.isPlaylistQueue())fillRadio();
                    return;
                }
                for(MusicCatalog.Song suggestion:batch)MusicCatalog.appendRecommendation(suggestion);
                if(pendingNext){
                    pendingNext=false;
                    MusicCatalog.Song next=MusicCatalog.neighbor(current.id,1);
                    if(next!=null){startTrack(next);return;}
                    statusMessage="Recommendations unavailable. Choose a new song.";
                }
                // Do not loop forever when upstream returned fewer than requested.
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
                                advance(1,true);
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
                    captureBeat();
                    applyImmersiveSound();
                }catch(RuntimeException ignored){}
                handler.postDelayed(this,beatEnabled?90:400);
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
        pendingNext=false;
        loading=true;playing=false;positionMs=0L;durationMs=selected.durationMs;
        if(!MusicCatalog.isPlaylistQueue())fillRadio();
        statusMessage="Resolving stream...";
        player.stop();
        resolver.execute(()->{
            try{
                java.io.File offline=OfflineLibrary.file(this,selected.id);
                String source=offline!=null&&offline.isFile()&&offline.length()>0
                    ?Uri.fromFile(offline).toString():MusicCatalog.resolveAudio(selected);
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
                    statusMessage=source.startsWith("file:")?"Playing offline":"Streaming";
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
                .put("repeatMode",repeatMode)
                .put("shuffle",MusicCatalog.isShuffle())
                .put("immersiveEnvironment",immersiveEnvironment)
                .put("queueMode",MusicCatalog.isPlaylistQueue()?"playlist":"radio")
                .put("nextCount",MusicCatalog.upcomingCount(current==null?"":current.id))
                .put("positionMs",positionMs)
                .put("durationMs",durationMs)
                .put("message",statusMessage);
        }catch(Exception ignored){}
        return json;
    }
    @Override public MediaSession onGetSession(MediaSession.ControllerInfo info){return session;}
    @Override public void onDestroy(){
        active=null;
        releaseBeat();
        releaseImmersiveSound();
        handler.removeCallbacks(snapshot);
        resolver.shutdownNow();
        radioWorker.shutdownNow();
        if(session!=null){session.release();session=null;}
        if(player!=null){player.release();player=null;}
        super.onDestroy();
    }
}

