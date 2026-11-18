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
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.SecureRandom;
import java.util.List;
import java.util.Locale;

public final class BridgeService extends NotificationListenerService {
    private static final String TAG="LeviMusicBridge";
    private static final int PORT=38771;
    private volatile boolean running=false;
    private volatile ServerSocket serverSocket;
    private Thread listenerThread;

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
    @Override public void onListenerConnected(){
        super.onListenerConnected();
        if(running)return;
        running=true;
        getSharedPreferences("bridge",MODE_PRIVATE).edit().putBoolean("service_ready",true).apply();
        listenerThread=new Thread(this::listen,"LeviMusicLoopback");
        listenerThread.start();
    }
    @Override public void onListenerDisconnected(){
        stopListening();
        super.onListenerDisconnected();
    }
    @Override public void onDestroy(){
        stopListening();
        super.onDestroy();
    }
    private void stopListening(){
        running=false;
        getSharedPreferences("bridge",MODE_PRIVATE).edit().putBoolean("service_ready",false).apply();
        ServerSocket socket=serverSocket;
        if(socket!=null)try{socket.close();}catch(Exception ignored){}
    }
    private void listen() {
        try(ServerSocket server=new ServerSocket(PORT,10,InetAddress.getByName("127.0.0.1"))){
            serverSocket=server;
            while(running){
                try(Socket client=server.accept()){
                    client.setSoTimeout(1200);
                    client.setTcpNoDelay(true);
                    serve(client);
                } catch(Exception e){ if(running)Log.w(TAG,"Client failed",e); }
            }
        } catch(Exception e){
            Log.e(TAG,"Could not listen on loopback",e);
        } finally {
            running=false; serverSocket=null;
            getSharedPreferences("bridge",MODE_PRIVATE).edit().putBoolean("service_ready",false).apply();
        }
    }
    private MediaController controller(){
        MediaSessionManager manager=(MediaSessionManager)getSystemService(Context.MEDIA_SESSION_SERVICE);
        if(manager==null)return null;
        try{
            List<MediaController> sessions=manager.getActiveSessions(new ComponentName(this,BridgeService.class));
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
        if(first==null||first.length()>250)return;
        String token="";
        String header;
        int headerCount=0;
        while((header=reader.readLine())!=null && !header.isEmpty() && headerCount++<40){
            if(header.length()>500)return;
            if(header.regionMatches(true,0,"X-LeviMusic-Token:",0,18))
                token=header.substring(18).trim();
        }
        String correct=getOrCreateToken(this);
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
            }else if(path.equals("/v1/status"))json=state();
            else if(path.startsWith("/v1/control?"))json=control(path);
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
