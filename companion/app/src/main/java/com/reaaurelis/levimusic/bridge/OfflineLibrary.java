package com.reaaurelis.levimusic.bridge;

import android.content.Context;
import android.net.Uri;
import android.database.Cursor;
import android.provider.OpenableColumns;
import org.json.JSONObject;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.concurrent.TimeUnit;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.HashSet;
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
        for(String ext:new String[]{".m4a",".webm",".mp3",".ogg",".aac",".opus",".flac",".wav",".mp4"}){
            File candidate=new File(dir(ctx),id+ext);
            if(candidate.isFile())return candidate;
        }
        return new File(dir(ctx),id+".m4a");
    }
    /**
     * Rebuild the automatic Offline playlist from audio that is really stored
     * by the Bridge. This keeps the local HTTP API and Minecraft in sync even
     * after an interrupted playlist write or an upgrade from an older APK.
     */
    static synchronized int reconcilePlaylist(Context ctx){
        File folder=dir(ctx);
        if(!folder.isDirectory())return 0;
        HashSet<String> known=new HashSet<>();
        for(MusicCatalog.Song song:PlaylistStore.songs(ctx,"Offline"))known.add(song.id);
        File[] files=folder.listFiles();
        if(files==null)return known.size();
        ArrayList<File> audio=new ArrayList<>();
        for(File candidate:files){
            if(candidate==null||!candidate.isFile()||candidate.length()<=0)continue;
            String name=candidate.getName();
            int dot=name.lastIndexOf('.');
            if(dot<=0)continue;
            String id=name.substring(0,dot),ext=name.substring(dot).toLowerCase(Locale.ROOT);
            if(id.matches("[A-Za-z0-9_-]{11}")&&
                ext.matches("\\.(m4a|webm|mp3|ogg|aac|opus|flac|wav|mp4)"))audio.add(candidate);
        }
        if(audio.isEmpty())return known.size();
        PlaylistStore.create(ctx,"Offline");
        for(File candidate:audio){
            String name=candidate.getName();
            String id=name.substring(0,name.lastIndexOf('.'));
            if(known.add(id))PlaylistStore.add(ctx,"Offline",new MusicCatalog.Song(
                id,id,"Local music","","",0));
        }
        return known.size();
    }
    static String save(Context ctx,MusicCatalog.Song song) throws Exception {
        if(song==null||file(ctx,song.id)==null)throw new IllegalArgumentException("Invalid song");
        File existing=file(ctx,song.id);
        if(existing.isFile()&&existing.length()>0){
            PlaylistStore.create(ctx,"Offline");PlaylistStore.add(ctx,"Offline",song);
            return "Already saved offline: "+song.title;
        }
        File temporary=new File(dir(ctx),song.id+".part");
        if(!dir(ctx).isDirectory()&&!dir(ctx).mkdirs())throw new java.io.IOException("Storage unavailable");
        String extension=null;Exception downloadFailure=null;
        try{
            String source=MusicCatalog.resolveAudio(song);
            if(!source.startsWith("https://"))throw new java.io.IOException("No secure audio source");
            extension=downloadSource(source,temporary);
        }catch(Exception error){temporary.delete();downloadFailure=error;}
        if(extension==null){
            try{extension=downloadSource(aurelisSource(song.id),temporary);}
            catch(Exception error){temporary.delete();downloadFailure=error;}
        }
        if(extension==null)throw new java.io.IOException("Unable to download audio: "+
            (downloadFailure==null?"no playable source":downloadFailure.getMessage()),downloadFailure);
        File target=new File(dir(ctx),song.id+extension);
        Files.move(temporary.toPath(),target.toPath(),StandardCopyOption.REPLACE_EXISTING);
        try{
            JSONObject found=LyricsFinder.find(ctx,song.title,song.artist,(int)song.durationMs,
                song.url,LyricsStore.romanizationLanguage(ctx));
            String lrc=found.optString("syncedLyrics");
            if(!lrc.isEmpty())LyricsStore.save(ctx,song.title,song.artist,lrc);
        }catch(Exception ignored){}
        PlaylistStore.create(ctx,"Offline");
        JSONObject added=PlaylistStore.add(ctx,"Offline",song);
        if(!added.optBoolean("accepted"))throw new java.io.IOException(added.optString("message"));
        return "Saved offline: "+song.title;
    }

    private static String aurelisSource(String id) throws Exception {
        String api="https://aurelis-symphony.go.altare.global/api/v2/playback/"+id;
        try(okhttp3.Response response=CLIENT.newCall(new Request.Builder().url(api).build()).execute()){
            if(!response.isSuccessful()||response.body()==null)throw new java.io.IOException("Aurelis playback unavailable");
            JSONObject playback=new JSONObject(response.body().string());
            String path=playback.optString("streamUrl");
            if((path.startsWith("/api/v2/audio/")||path.startsWith("/demo/"))&&
                !path.contains("..")&&!path.contains("?")&&!path.contains("#"))
                return "https://aurelis-symphony.go.altare.global"+path;
        }
        throw new java.io.IOException("Aurelis did not provide an audio URL");
    }

    private static String downloadSource(String source,File temporary) throws Exception {
        String extension;
        try(okhttp3.Response response=CLIENT.newCall(new Request.Builder().url(source).build()).execute()){
            if(!response.isSuccessful()||response.body()==null)
                throw new java.io.IOException("Audio HTTP "+response.code());
            String mime=response.header("Content-Type","").toLowerCase(java.util.Locale.ROOT);
            if(response.body().contentLength()>LIMIT)throw new java.io.IOException("Audio file too large");
            long total=0;
            try(InputStream input=response.body().byteStream()){
                byte[] signature=new byte[32];int bytesRead=0;
                while(bytesRead<signature.length){
                    int count=input.read(signature,bytesRead,signature.length-bytesRead);
                    if(count<0)break;
                    if(count==0)break;
                    bytesRead+=count;
                }
                extension=AudioFormat.extension(signature,bytesRead,mime);
                if(extension.isEmpty())throw new java.io.IOException("Response is not a supported audio file ("+mime+")");
                try(FileOutputStream out=new FileOutputStream(temporary)){
                out.write(signature,0,bytesRead);total=bytesRead;
                byte[] bytes=new byte[16384];int count;
                while((count=input.read(bytes))!=-1){
                    total+=count;if(total>LIMIT)throw new java.io.IOException("Audio file too large");
                    out.write(bytes,0,count);
                }
                out.getFD().sync();
                }
            }
            if(total==0)throw new java.io.IOException("Empty audio file");
        }
        return extension;
    }

    private static String displayName(Context context,Uri uri){
        try(Cursor cursor=context.getContentResolver().query(uri,
            new String[]{OpenableColumns.DISPLAY_NAME},null,null,null)){
            if(cursor!=null&&cursor.moveToFirst())return cursor.getString(0);
        }catch(Exception ignored){}
        return "";
    }
    private static boolean importLrc(Context ctx,Uri reading,String title,String artist) throws Exception {
        try(InputStream input=ctx.getContentResolver().openInputStream(reading)){
            if(input==null)throw new java.io.IOException("Cannot read LRC file");
            java.io.ByteArrayOutputStream content=new java.io.ByteArrayOutputStream();
            byte[] bytes=new byte[4096];int n;
            while((n=input.read(bytes))!=-1){
                if(content.size()+n>250000)throw new java.io.IOException("LRC too large");
                content.write(bytes,0,n);
            }
            return LyricsStore.save(ctx,title,artist,content.toString("UTF-8"));
        }
    }
    /** The Android document picker supplies audio and optional same-stem LRC together. */
    static String importFiles(Context ctx,List<Uri> files) throws Exception {
        if(files==null||files.isEmpty()||files.size()>60)throw new java.io.IOException("Choose audio and optional LRC files");
        HashMap<String,Uri> readings=new HashMap<>();
        ArrayList<Uri> audio=new ArrayList<>();
        ArrayList<String> names=new ArrayList<>();
        for(Uri uri:files){
            String name=displayName(ctx,uri);
            int dot=name.lastIndexOf('.');
            if(dot<=0||name.length()>180)continue;
            String ext=name.substring(dot).toLowerCase(Locale.ROOT);
            String stem=name.substring(0,dot).toLowerCase(Locale.ROOT);
            if(ext.equals(".lrc"))readings.put(stem,uri);
            else if(ext.matches("\\.(mp3|m4a|aac|ogg|opus|flac|wav|mp4|webm)")){
                audio.add(uri);names.add(name);
            }
        }
        if(audio.isEmpty()){
            int importedLrc=0;
            for(MusicCatalog.Song song:PlaylistStore.songs(ctx,"Offline")){
                Uri reading=readings.get(song.title.toLowerCase(Locale.ROOT));
                if(reading!=null&&importLrc(ctx,reading,song.title,song.artist))importedLrc++;
            }
            if(importedLrc==0)throw new java.io.IOException("Choose audio or an LRC matching a song in Offline");
            return "Imported synchronized lyrics for "+importedLrc+" offline songs";
        }
        if(!dir(ctx).isDirectory()&&!dir(ctx).mkdirs())throw new java.io.IOException("Storage unavailable");
        int imported=0,withLyrics=0;
        for(int i=0;i<audio.size();i++){
            String name=names.get(i),extension=name.substring(name.lastIndexOf('.')).toLowerCase(Locale.ROOT);
            String title=name.substring(0,name.lastIndexOf('.'));
            String id=java.util.UUID.randomUUID().toString().replace("-","").substring(0,11);
            File temporary=new File(dir(ctx),id+".part");
            long total=0;
            try(InputStream input=ctx.getContentResolver().openInputStream(audio.get(i));
                FileOutputStream output=new FileOutputStream(temporary)){
                if(input==null)throw new java.io.IOException("Cannot read "+name);
                byte[] bytes=new byte[16384];int n;
                while((n=input.read(bytes))!=-1){
                    total+=n;if(total>LIMIT)throw new java.io.IOException("File too large: "+name);
                    output.write(bytes,0,n);
                }
                output.getFD().sync();
            }catch(Exception error){temporary.delete();throw error;}
            if(total==0){temporary.delete();throw new java.io.IOException("Empty audio file: "+name);}
            File target=new File(dir(ctx),id+extension);
            Files.move(temporary.toPath(),target.toPath(),StandardCopyOption.REPLACE_EXISTING);
            Uri reading=readings.get(title.toLowerCase(Locale.ROOT));
            if(reading!=null&&importLrc(ctx,reading,title,"Local music"))withLyrics++;
            PlaylistStore.create(ctx,"Offline");
            JSONObject added=PlaylistStore.add(ctx,"Offline",new MusicCatalog.Song(id,title,
                "Local music","","",0));
            if(!added.optBoolean("accepted"))throw new java.io.IOException(added.optString("message"));
            imported++;
        }
        return "Imported "+imported+" local songs ("+withLyrics+" synchronized LRC files)";
    }
}
