package com.reaaurelis.levimusic.bridge;

import android.content.Context;
import org.json.JSONObject;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.Locale;

/** User-supplied local LRC files: exact track/artist fallback when LRCLIB has no synced match. */
public final class LyricsStore {
    private static final String PREFS="levimusic_local_lyrics";
    private LyricsStore(){}
    private static String key(String title,String artist){
        String normalized=((title==null?"":title)+"|"+(artist==null?"":artist))
            .toLowerCase(Locale.ROOT).replaceAll("\\s+"," ").trim();
        try{
            byte[] bytes=MessageDigest.getInstance("SHA-256")
                .digest(normalized.getBytes(StandardCharsets.UTF_8));
            StringBuilder sb=new StringBuilder(64);
            for(byte b:bytes)sb.append(String.format(Locale.ROOT,"%02x",b&255));
            return sb.toString();
        }catch(Exception ignored){return normalized;}
    }
    public static boolean save(Context context,String title,String artist,String lrc){
        if(title==null||title.isEmpty()||lrc==null||lrc.length()>250000)return false;
        if(!lrc.matches("(?s).*\\[\\d{1,3}:\\d{2}.*"))return false;
        return context.getSharedPreferences(PREFS,Context.MODE_PRIVATE).edit()
            .putString(key(title,artist),lrc).commit();
    }
    public static String get(Context context,String title,String artist){
        return context.getSharedPreferences(PREFS,Context.MODE_PRIVATE)
            .getString(key(title,artist),"");
    }
    public static JSONObject response(Context context,String title,String artist){
        JSONObject result=new JSONObject();
        String lines=get(context,title,artist);
        try{result.put("success",!lines.isEmpty()).put("source","local LRC")
            .put("syncedLyrics",lines);}catch(Exception ignored){}
        return result;
    }
}
