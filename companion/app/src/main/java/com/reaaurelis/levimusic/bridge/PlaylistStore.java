package com.reaaurelis.levimusic.bridge;

import android.content.Context;
import android.content.SharedPreferences;
import org.json.JSONArray;
import org.json.JSONObject;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/** Local-only persistent playlists, independent of the current search result page. */
public final class PlaylistStore {
    private static final String PREFS="levimusic_playlists";
    private static final String DATA="playlists_json";
    private static final int MAX_PLAYLISTS=40;
    private static final int MAX_TRACKS=300;
    private PlaylistStore(){}

    private static JSONArray read(Context context) {
        String data=context.getSharedPreferences(PREFS,Context.MODE_PRIVATE).getString(DATA,"[]");
        try{return new JSONArray(data);}catch(Exception e){return new JSONArray();}
    }
    private static void write(Context context,JSONArray playlists) {
        context.getSharedPreferences(PREFS,Context.MODE_PRIVATE)
            .edit().putString(DATA,playlists.toString()).apply();
    }
    private static String cleanName(String name) {
        if(name==null)return "";
        String normalized=name.trim().replaceAll("[\\x00-\\x1f]","");
        if(normalized.length()>48)normalized=normalized.substring(0,48);
        return normalized;
    }
    private static int find(JSONArray data,String name) {
        for(int i=0;i<data.length();i++){
            JSONObject playlist=data.optJSONObject(i);
            if(playlist!=null&&name.equalsIgnoreCase(playlist.optString("name")))return i;
        }
        return -1;
    }
    public static synchronized JSONObject create(Context context,String name) {
        name=cleanName(name);
        if(name.isEmpty())return result(false,"Enter a playlist name");
        JSONArray data=read(context);
        if(find(data,name)>=0)return result(false,"Playlist already exists");
        if(data.length()>=MAX_PLAYLISTS)return result(false,"Maximum 40 playlists");
        JSONObject item=new JSONObject();
        try{item.put("name",name).put("tracks",new JSONArray());}catch(Exception ignored){}
        data.put(item);write(context,data);
        return result(true,"Playlist created");
    }
    public static synchronized JSONObject add(Context context,String name,MusicCatalog.Song song) {
        if(song==null)return result(false,"Select a song first");
        JSONArray data=read(context);
        int index=find(data,cleanName(name));
        if(index<0)return result(false,"Playlist not found");
        JSONObject entry=data.optJSONObject(index);
        JSONArray tracks=entry.optJSONArray("tracks");
        if(tracks==null)tracks=new JSONArray();
        if(tracks.length()>=MAX_TRACKS)return result(false,"Playlist is full");
        for(int i=0;i<tracks.length();i++){
            JSONObject existing=tracks.optJSONObject(i);
            if(existing!=null&&song.id.equals(existing.optString("id")))
                return result(true,"Already in playlist");
        }
        tracks.put(song.json());
        try{entry.put("tracks",tracks);}catch(Exception ignored){}
        write(context,data);
        return result(true,"Added to playlist");
    }
    public static synchronized JSONObject remove(Context context,String name,String id) {
        JSONArray data=read(context);
        int index=find(data,cleanName(name));
        if(index<0)return result(false,"Playlist not found");
        JSONObject entry=data.optJSONObject(index);
        JSONArray original=entry.optJSONArray("tracks"), updated=new JSONArray();
        if(original!=null)for(int i=0;i<original.length();i++){
            JSONObject song=original.optJSONObject(i);
            if(song!=null&&!id.equals(song.optString("id")))updated.put(song);
        }
        try{entry.put("tracks",updated);}catch(Exception ignored){}
        write(context,data);
        return result(true,"Song removed");
    }
    public static synchronized JSONObject delete(Context context,String name) {
        JSONArray data=read(context), updated=new JSONArray();
        if(find(data,cleanName(name))<0)return result(false,"Playlist not found");
        for(int i=0;i<data.length();i++){
            JSONObject p=data.optJSONObject(i);
            if(p!=null&&!name.equalsIgnoreCase(p.optString("name")))updated.put(p);
        }
        write(context,updated);
        return result(true,"Playlist deleted");
    }
    public static synchronized JSONObject list(Context context,String filter) {
        JSONArray data=read(context), items=new JSONArray();
        String q=filter==null?"":filter.toLowerCase(Locale.ROOT).trim();
        for(int i=0;i<data.length();i++){
            JSONObject p=data.optJSONObject(i);
            if(p==null||!p.optString("name").toLowerCase(Locale.ROOT).contains(q))continue;
            JSONObject entry=new JSONObject();
            JSONArray songs=p.optJSONArray("tracks");
            try{entry.put("name",p.optString("name")).put("count",songs==null?0:songs.length());}
            catch(Exception ignored){}
            items.put(entry);
        }
        JSONObject result=result(true,"OK");
        try{result.put("playlists",items).put("count",items.length());}catch(Exception ignored){}
        return result;
    }
    public static synchronized List<MusicCatalog.Song> songs(Context context,String name) {
        List<MusicCatalog.Song> out=new ArrayList<>();
        JSONArray all=read(context);
        int index=find(all,cleanName(name));
        if(index<0)return out;
        JSONArray tracks=all.optJSONObject(index).optJSONArray("tracks");
        if(tracks==null)return out;
        for(int i=0;i<tracks.length();i++){
            JSONObject song=tracks.optJSONObject(i);
            if(song==null)continue;
            String id=song.optString("id","");
            if(!id.matches("[A-Za-z0-9_-]{11}"))continue;
            String url="https://www.youtube.com/watch?v="+id;
            out.add(new MusicCatalog.Song(id,song.optString("title"),
                song.optString("artist"),url,song.optString("art"),
                Math.max(0,song.optLong("durationMs"))));
        }
        return out;
    }
    public static synchronized JSONObject tracks(Context context,String name) {
        List<MusicCatalog.Song> tracks=songs(context,name);
        JSONArray items=new JSONArray();
        for(MusicCatalog.Song song:tracks)items.put(song.json());
        JSONObject result=result(true,"OK");
        try{result.put("name",cleanName(name)).put("tracks",items).put("count",items.length());}
        catch(Exception ignored){}
        return result;
    }

    /** Stable, explicit backup format for migration across APK signing changes. */
    public static synchronized String exportJson(Context context) {
        JSONObject root=new JSONObject();
        try{
            root.put("format","levimusic-playlists-1");
            root.put("playlists",read(context));
        }catch(Exception ignored){}
        return root.toString();
    }
    public static synchronized JSONObject importBackup(Context context,String json) {
        if(json==null || json.length()>4_000_000)
            return result(false,"Backup exceeds 4 MB limit");
        try {
            JSONObject root=new JSONObject(json);
            if(!"levimusic-playlists-1".equals(root.optString("format")))
                return result(false,"Unrecognized LeviMusic playlist backup format");
            JSONArray source=root.optJSONArray("playlists");
            if(source==null||source.length()>MAX_PLAYLISTS)
                return result(false,"Invalid playlist count");
            JSONArray clean=new JSONArray();
            java.util.HashSet<String> names=new java.util.HashSet<>();
            int totalSongs=0;
            for(int i=0;i<source.length();i++){
                JSONObject item=source.optJSONObject(i);
                if(item==null)return result(false,"Malformed playlist");
                String name=cleanName(item.optString("name"));
                if(name.isEmpty()||!names.add(name.toLowerCase(Locale.ROOT)))
                    return result(false,"Duplicate or invalid playlist name");
                JSONArray songs=item.optJSONArray("tracks");
                if(songs==null||songs.length()>MAX_TRACKS)
                    return result(false,"Playlist exceeds 300 songs");
                JSONArray valid=new JSONArray();
                for(int j=0;j<songs.length();j++){
                    JSONObject song=songs.optJSONObject(j);
                    if(song==null)return result(false,"Invalid song data");
                    String id=song.optString("id");
                    if(!id.matches("[A-Za-z0-9_-]{11}"))continue;
                    String title=song.optString("title");
                    String artist=song.optString("artist");
                    String art=song.optString("art");
                    if(title.length()>200||artist.length()>160||art.length()>600)
                        return result(false,"Invalid song metadata length");
                    if(!art.isEmpty()&&!art.startsWith("https://"))art="";
                    JSONObject cleanSong=new JSONObject();
                    cleanSong.put("id",id);
                    cleanSong.put("title",title);
                    cleanSong.put("artist",artist);
                    cleanSong.put("art",art);
                    cleanSong.put("durationMs",Math.min(Math.max(0,song.optLong("durationMs")),86400000L));
                    valid.put(cleanSong);
                    totalSongs++;
                }
                JSONObject p=new JSONObject();
                p.put("name",name).put("tracks",valid);
                clean.put(p);
            }
            write(context,clean);
            return result(true,"Imported "+clean.length()+" playlists / "+totalSongs+" tracks");
        } catch(Exception error) {
            return result(false,"Invalid JSON backup");
        }
    }

    public static synchronized JSONObject play(Context context,String name,int index) {
        List<MusicCatalog.Song> songs=songs(context,name);
        if(songs.isEmpty())return result(false,"Playlist is empty");
        if(index<0||index>=songs.size())return result(false,"Invalid track index");
        MusicCatalog.activatePlaylistQueue(songs);
        boolean accepted=PlayerService.play(context,songs.get(index));
        return result(accepted,accepted?"Playback requested":"Could not start player");
    }
    private static JSONObject result(boolean accepted,String message) {
        JSONObject reply=new JSONObject();
        try{reply.put("success",accepted).put("accepted",accepted).put("message",message);}
        catch(Exception ignored){}
        return reply;
    }
}
