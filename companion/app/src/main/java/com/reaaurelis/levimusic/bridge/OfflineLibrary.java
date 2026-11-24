package com.reaaurelis.levimusic.bridge;

import android.content.Context;
import org.json.JSONObject;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.concurrent.TimeUnit;
import okhttp3.OkHttpClient;
import okhttp3.Request;

/** Audio files are private to the Bridge; playlists keep the original video IDs. */
final class OfflineLibrary {
    private static final long LIMIT=100L*1024*1024;
    private static final OkHttpClient CLIENT=new OkHttpClient.Builder()
        .connectTimeout(15,TimeUnit.SECONDS).readTimeout(40,TimeUnit.SECONDS).build();
    private OfflineLibrary(){}
    private static File dir(Context ctx){return new File(ctx.getFilesDir(),"offline-music");}
    static File file(Context ctx,String id){
        if(id==null||!id.matches("[A-Za-z0-9_-]{11}"))return null;
        for(String ext:new String[]{".m4a",".webm",".mp3",".ogg",".aac"}){
            File candidate=new File(dir(ctx),id+ext);
            if(candidate.isFile())return candidate;
        }
        return new File(dir(ctx),id+".m4a");
    }
    static String save(Context ctx,MusicCatalog.Song song) throws Exception {
        if(song==null||file(ctx,song.id)==null)throw new IllegalArgumentException("Invalid song");
        File existing=file(ctx,song.id);
        if(existing.isFile()&&existing.length()>0){
            PlaylistStore.create(ctx,"Offline");PlaylistStore.add(ctx,"Offline",song);
            return "Already saved offline: "+song.title;
        }
        String source=MusicCatalog.resolveAudio(song);
        if(!source.startsWith("https://"))throw new java.io.IOException("No secure audio source");
        File temporary=new File(dir(ctx),song.id+".part");
        if(!dir(ctx).isDirectory()&&!dir(ctx).mkdirs())throw new java.io.IOException("Storage unavailable");
        String extension;
        try(okhttp3.Response response=CLIENT.newCall(new Request.Builder().url(source).build()).execute()){
            if(!response.isSuccessful()||response.body()==null)throw new java.io.IOException("Audio download failed");
            String mime=response.header("Content-Type","").toLowerCase(java.util.Locale.ROOT);
            if(mime.contains("webm")||source.contains("mime=audio%2Fwebm"))extension=".webm";
            else if(mime.contains("mpeg"))extension=".mp3";
            else if(mime.contains("ogg"))extension=".ogg";
            else if(mime.contains("aac"))extension=".aac";
            else if(mime.contains("mp4")||mime.contains("m4a")||
                source.contains("mime=audio%2Fmp4"))extension=".m4a";
            else throw new java.io.IOException("Unsupported audio format: "+mime);
            if(response.body().contentLength()>LIMIT)throw new java.io.IOException("Audio file too large");
            long total=0;
            try(InputStream input=response.body().byteStream();FileOutputStream out=new FileOutputStream(temporary)){
                byte[] bytes=new byte[16384];int count;
                while((count=input.read(bytes))!=-1){
                    total+=count;if(total>LIMIT)throw new java.io.IOException("Audio file too large");
                    out.write(bytes,0,count);
                }
                out.getFD().sync();
            }
            if(total==0)throw new java.io.IOException("Empty audio file");
        }catch(Exception error){temporary.delete();throw error;}
        File target=new File(dir(ctx),song.id+extension);
        Files.move(temporary.toPath(),target.toPath(),StandardCopyOption.REPLACE_EXISTING);
        try{
            JSONObject found=LyricsFinder.find(ctx,song.title,song.artist,(int)song.durationMs,
                song.url,LyricsStore.romanizationLanguage(ctx));
            String lrc=found.optString("syncedLyrics");
            if(!lrc.isEmpty())LyricsStore.save(ctx,song.title,song.artist,lrc);
        }catch(Exception ignored){}
        JSONObject created=PlaylistStore.create(ctx,"Offline");
        JSONObject added=PlaylistStore.add(ctx,"Offline",song);
        if(!added.optBoolean("accepted"))throw new java.io.IOException(added.optString("message"));
        return "Saved offline: "+song.title;
    }
}
