package com.reaaurelis.levimusic.bridge;

import android.content.ComponentName;
import android.content.Context;
import android.media.MediaMetadata;
import android.media.session.MediaController;
import android.media.session.MediaSessionManager;
import android.media.session.PlaybackState;
import android.os.SystemClock;
import android.service.notification.NotificationListenerService;
import android.util.Log;
import org.json.JSONObject;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.URLDecoder;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.SecureRandom;
import java.util.List;
import java.util.Locale;

public final class BridgeApi {
    private static final String TAG="LeviMusicBridge";
    private static final int PORT=38771;
    private volatile boolean running=false;
    private volatile ServerSocket serverSocket;
    private Thread listenerThread;
    private final ExecutorService clients=Executors.newFixedThreadPool(4);

    public static String getOrCreateToken(Context c) {
        android.content.SharedPreferences prefs=c.getSharedPreferences("bridge",Context.MODE_PRIVATE);
        String key=prefs.getString("token","");
        if(key.matches("[a-f0-9]{48}"))return key;
        byte[] bytes=new byte[24];
        new SecureRandom().nextBytes(bytes);
        StringBuilder str=new StringBuilder(48);
        for(byte b:bytes)str.append(String.format(Locale.ROOT,"%02x",b & 255));
        key=str.toString();
        prefs.edit().putString("token",key).apply();
        return key;
    }
    private static BridgeApi shared;
    private final Context context;
    private BridgeApi(Context app) { this.context=app.getApplicationContext(); }
    public static synchronized void ensureStarted(Context ctx) {
        if(shared!=null && shared.running)return;
        if(shared==null)shared=new BridgeApi(ctx);
        shared.start();
    }
    private synchronized void start() {
        if(running)return;
        running=true;
        context.getSharedPreferences("bridge",Context.MODE_PRIVATE).edit().putBoolean("service_ready",true).apply();
        listenerThread=new Thread(this::listen,"LeviMusicLoopback");
        listenerThread.setDaemon(true);
        listenerThread.start();
    }
    private void listen() {
        try(ServerSocket server=new ServerSocket(PORT,10,InetAddress.getByName("127.0.0.1"))){
            serverSocket=server;
            while(running){
                try {
                    final Socket client=server.accept();
                    clients.execute(()->{
                        try(Socket socket=client){
                            socket.setSoTimeout(35000);
                            socket.setTcpNoDelay(true);
                            serve(socket);
                        }catch(Exception e){if(running)Log.w(TAG,"Client failed",e);}
                    });
                } catch(Exception e){ if(running)Log.w(TAG,"Client failed",e); }
            }
        } catch(Exception e){
            Log.e(TAG,"Could not listen on loopback",e);
        } finally {
            running=false; serverSocket=null;
            context.getSharedPreferences("bridge",Context.MODE_PRIVATE).edit().putBoolean("service_ready",false).apply();
        }
    }
    private MediaController controller(){
        MediaSessionManager manager=(MediaSessionManager)context.getSystemService(Context.MEDIA_SESSION_SERVICE);
        if(manager==null)return null;
        try{
            List<MediaController> sessions=manager.getActiveSessions(new ComponentName(context,BridgeService.class));
            MediaController other=null;
            for(MediaController m:sessions){
                String id=m.getPackageName();
                if(!"com.spotify.music".equals(id)&&!"com.google.android.apps.youtube.music".equals(id))continue;
                PlaybackState state=m.getPlaybackState();
                if(state!=null && state.getState()==PlaybackState.STATE_PLAYING)return m;
                if(other==null)other=m;
            }
            return other;
        } catch(SecurityException e){Log.w(TAG,"Notification access denied",e);return null;}
    }
    private JSONObject state(){
        JSONObject own=PlayerService.getStatus();
        if(own!=null)return own;
        JSONObject o=new JSONObject();
        MediaController m=controller();
        try{
            if(m==null){
                o.put("available",false).put("provider","").put("playing",false);
                o.put("message","Open official Spotify or YouTube Music and play a song");
                return o;
            }
            MediaMetadata meta=m.getMetadata();
            PlaybackState playback=m.getPlaybackState();
            String id=m.getPackageName();
            String title=meta==null?"":meta.getString(MediaMetadata.METADATA_KEY_TITLE);
            String artist=meta==null?"":meta.getString(MediaMetadata.METADATA_KEY_ARTIST);
            String album=meta==null?"":meta.getString(MediaMetadata.METADATA_KEY_ALBUM);
            long duration=meta==null?0:meta.getLong(MediaMetadata.METADATA_KEY_DURATION);
            long position=playback==null?0:playback.getPosition();
            boolean isPlaying=playback!=null&&playback.getState()==PlaybackState.STATE_PLAYING;
            if(isPlaying&&position>=0){
                long delta=SystemClock.elapsedRealtime()-playback.getLastPositionUpdateTime();
                position+=Math.max(0,Math.round(delta*playback.getPlaybackSpeed()));
            }
            if(duration>0)position=Math.min(Math.max(position,0),duration);
            o.put("available",true)
             .put("provider",id.equals("com.spotify.music")?"Spotify":"YouTube Music")
             .put("title",title==null?"":title)
             .put("artist",artist==null?"":artist)
             .put("album",album==null?"":album)
             .put("playing",isPlaying)
             .put("durationMs",Math.max(0,duration))
             .put("positionMs",Math.max(0,position));
        } catch(Exception e){Log.w(TAG,"status",e);}
        return o;
    }
    private JSONObject control(String path){
        JSONObject o=new JSONObject();
        if(PlayerService.getStatus()!=null){
            String action=getParam(path,"action");
            long seek=0;
            try{seek=Long.parseLong(getParam(path,"position"));}catch(Exception ignored){}
            boolean ok=PlayerService.command(action,seek);
            try{return o.put("accepted",ok).put("note",ok?"Internal player command queued":"Unavailable");}
            catch(Exception ignored){return new JSONObject();}
        }
        try{
            MediaController m=controller();
            if(m==null)return o.put("accepted",false).put("error","No active music session");
            MediaController.TransportControls t=m.getTransportControls();
            String action=getParam(path,"action");
            switch(action){
                case "play": t.play();break;
                case "pause": t.pause();break;
                case "next": t.skipToNext();break;
                case "previous": t.skipToPrevious();break;
                case "seek": {
                    long n=Long.parseLong(getParam(path,"position"));
                    if(n<0||n>24*3600*1000L)throw new IllegalArgumentException("Seek out of range");
                    t.seekTo(n);break;
                }
                default: return o.put("accepted",false).put("error","Unknown action");
            }
            return o.put("accepted",true).put("note","Provider may reject restricted actions");
        }catch(Exception e){
            Log.w(TAG,"Music session action failed",e);
            try { return o.put("accepted",false).put("error","Action failed"); }
            catch(org.json.JSONException ignored) { return new JSONObject(); }
        }
    }
    private String getParam(String path,String key){
        String q=path.contains("?")?path.substring(path.indexOf('?')+1):"";
        for(String part:q.split("&")){
            int eq=part.indexOf('=');
            if(eq>0&&part.substring(0,eq).equals(key))return part.substring(eq+1);
        }
        return "";
    }
    private void serve(Socket socket) throws Exception {
        BufferedReader reader=new BufferedReader(new InputStreamReader(socket.getInputStream(),StandardCharsets.US_ASCII));
        String first=reader.readLine();
        if(first==null||first.length()>2048)return;
        String token="";
        String header;
        int headerCount=0;
        while((header=reader.readLine())!=null && !header.isEmpty() && headerCount++<40){
            if(header.length()>500)return;
            if(header.regionMatches(true,0,"X-LeviMusic-Token:",0,18))
                token=header.substring(18).trim();
        }
        String correct=getOrCreateToken(context);
        boolean authorized=MessageDigest.isEqual(token.getBytes(StandardCharsets.UTF_8),
                correct.getBytes(StandardCharsets.UTF_8));
        int status=authorized?200:403;
        JSONObject json;
        if(!authorized)json=new JSONObject().put("error","Pairing key required");
        else{
            String[] parts=first.split(" ");
            String path=parts.length>1?parts[1]:"";
            if(parts.length<3||!"GET".equals(parts[0])) {
                status=405;json=new JSONObject().put("error","GET only");
            }else if(path.equals("/v1/status")){
                PlayerService.minecraftHeartbeat();json=state();
            }
            else if(path.startsWith("/v1/beat")){
                String setting=getParam(path,"enabled");
                if("1".equals(setting)||"0".equals(setting))
                    PlayerService.setBeatEnabled("1".equals(setting));
                json=PlayerService.getBeatStatus(context);
            }
            else if(path.startsWith("/v1/immersive")){
                String environment=getParam(path,"environment");
                PlayerService.setEnvironment(environment);
                json=new JSONObject().put("accepted",true).put("environment",
                    "underwater".equals(environment)?"underwater":"cave".equals(environment)?"cave":"open");
            }
            else if(path.equals("/v1/game-exit")){
                json=new JSONObject().put("accepted",true);
                PlayerService.stopFromGame();
            }
            else if(path.equals("/v1/health")){
                PlayerService.minecraftHeartbeat();
                json=new JSONObject().put("ok",true).put("version","1.7.1-Beta4").put("api",5)
                    .put("engine","unified-auto");
            }
            else if(path.equals("/v1/queue")){
                JSONObject player=PlayerService.getStatus();
                String id=player==null?"":player.optString("id");
                json=MusicCatalog.queueJson(id);
            }
            else if(path.startsWith("/v1/queue/")){
                String op=path.substring("/v1/queue/".length()).split("[?]")[0];
                String id=getParam(path,"id");
                JSONObject player=PlayerService.getStatus();
                String now=player==null?"":player.optString("id");
                boolean accepted=false;
                String message="Unknown queue command";
                switch(op){
                    case "add":
                        accepted=MusicCatalog.enqueue(MusicCatalog.find(id));
                        message=accepted?"Added to Up Next":"Song already queued or unavailable";
                        break;
                    case "remove":
                        accepted=MusicCatalog.removeUpcoming(id,now);
                        if(accepted)PlayerService.requestRadioRefill();
                        message=accepted?"Removed from Up Next":"Cannot remove current song";
                        break;
                    case "play": {
                        MusicCatalog.Song selected=MusicCatalog.find(id);
                        accepted=selected!=null&&PlayerService.play(context,selected);
                        message=accepted?"Playing from Up Next":"Song no longer available";
                        break;
                    }
                    default:status=404;
                }
                json=new JSONObject().put("accepted",accepted).put("message",message);
            }
            else if(path.startsWith("/v1/control?"))json=control(path);
            else if(path.startsWith("/v1/search?")){
                String query=URLDecoder.decode(getParam(path,"query"),"UTF-8");
                int limit=query.startsWith("https://")?2048:160;
                if(query.length()>limit)query=query.substring(0,limit);
                json=MusicCatalog.searchJson(query);
            }
            else if(path.equals("/v1/capabilities"))
                json=new JSONObject().put("api",4).put("playlist",true).put("search",true)
                    .put("player",true).put("radio3",true)
                    .put("queueEditing",true).put("audioBeats",true)
                    .put("repeat",true).put("shufflePlaylist",true).put("immersiveSound",true)
                    .put("pairing","local-clipboard");
            else if(path.equals("/v1/playlists")||path.startsWith("/v1/playlists?")){
                OfflineLibrary.reconcilePlaylist(context);
                String filter=URLDecoder.decode(getParam(path,"query"),"UTF-8");
                json=PlaylistStore.list(context,filter);
            }
            else if(path.startsWith("/v1/lyrics?")){
                String title=URLDecoder.decode(getParam(path,"title"),"UTF-8");
                String artist=URLDecoder.decode(getParam(path,"artist"),"UTF-8");
                String id=getParam(path,"id");
                MusicCatalog.Song song=MusicCatalog.find(id);
                int duration=0;
                try{duration=Math.max(0,Integer.parseInt(getParam(path,"duration")));}
                catch(NumberFormatException ignored){}
                if(song!=null&&duration==0)duration=(int)Math.min(Integer.MAX_VALUE,song.durationMs);
                json=LyricsFinder.find(context,title,artist,duration,
                    song==null?"":song.url,LyricsStore.romanizationLanguage(context));
            }
            else if(path.startsWith("/v1/offline/save?")){
                MusicCatalog.Song song=MusicCatalog.find(getParam(path,"id"));
                try{json=new JSONObject().put("accepted",true)
                    .put("message",OfflineLibrary.save(context,song));}
                catch(Exception error){json=new JSONObject().put("accepted",false)
                    .put("message",error.getMessage()==null?"Download failed":error.getMessage());}
            }
            else if(path.startsWith("/v1/playlist/")){
                String op=path.substring("/v1/playlist/".length()).split("[?]")[0];
                String name=URLDecoder.decode(getParam(path,"name"),"UTF-8");
                String id=getParam(path,"id");
                if("Offline".equalsIgnoreCase(name))OfflineLibrary.reconcilePlaylist(context);
                switch(op){
                    case "create": json=PlaylistStore.create(context,name);break;
                    case "add": json=PlaylistStore.add(context,name,MusicCatalog.find(id));break;
                    case "tracks": json=PlaylistStore.tracks(context,name);break;
                    case "remove": json=PlaylistStore.remove(context,name,id);break;
                    case "delete": json=PlaylistStore.delete(context,name);break;
                    case "rename": json=PlaylistStore.rename(context,name,
                        URLDecoder.decode(getParam(path,"newName"),"UTF-8"));break;
                    case "import": {
                        try{
                            MusicCatalog.ImportedPlaylist imported=MusicCatalog.importPublicPlaylist(
                                URLDecoder.decode(getParam(path,"url"),"UTF-8"));
                            json=PlaylistStore.importSongs(context,imported.name,imported.songs);
                        }catch(Exception error){json=new JSONObject().put("accepted",false)
                            .put("message","Playlist import failed: "+error.getMessage());}
                        break;
                    }
                    case "play": {
                        int index=0;
                        try{index=Integer.parseInt(getParam(path,"index"));}catch(Exception ignored){}
                        json=PlaylistStore.play(context,name,index);break;
                    }
                    default: status=404;json=new JSONObject().put("error","Unknown playlist operation");
                }
            }
            else if(path.startsWith("/v1/play?")){
                String id=getParam(path,"id");
                MusicCatalog.Song song=MusicCatalog.find(id);
                if(song!=null)MusicCatalog.activateSearchQueue(song);
                boolean started=PlayerService.play(context,song);
                json=new JSONObject().put("accepted",started)
                    .put("message",started?"Playback requested":"Open LeviMusic app first or search again");
            }
            else{status=404;json=new JSONObject().put("error","Unknown endpoint");}
        }
        byte[] content=json.toString().getBytes(StandardCharsets.UTF_8);
        String headers="HTTP/1.1 "+status+" "+(status==200?"OK":"Error")+
                "\r\nContent-Type: application/json; charset=utf-8\r\nCache-Control: no-store"+
                "\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\nContent-Length: "+
                content.length+"\r\n\r\n";
        OutputStream os=socket.getOutputStream();
        os.write(headers.getBytes(StandardCharsets.US_ASCII));
        os.write(content);os.flush();
    }
}


