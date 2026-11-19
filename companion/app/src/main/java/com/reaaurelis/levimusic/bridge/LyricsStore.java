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
    public static boolean romanizeEnabled(Context context){
        return context.getSharedPreferences(PREFS,Context.MODE_PRIVATE).getBoolean("romanize",true);
    }
    public static void setRomanize(Context context,boolean enabled){
        context.getSharedPreferences(PREFS,Context.MODE_PRIVATE).edit()
            .putBoolean("romanize",enabled).apply();
    }
    public static String display(Context context,String lines,boolean enabled){
        if(!enabled||lines==null||lines.isEmpty())return lines;
        try{
            android.icu.text.Transliterator latin=android.icu.text.Transliterator
                .getInstance("Any-Latin; Latin-ASCII");
            StringBuilder out=new StringBuilder();
            for(String line:lines.split("\n",-1)){
                int marker=line.lastIndexOf(']');
                if(marker>=0 && line.startsWith("[")){
                    out.append(line,0,marker+1);
                    out.append(latin.transliterate(line.substring(marker+1)));
                }else out.append(latin.transliterate(line));
                out.append('\n');
            }
            return out.toString();
        }catch(Exception ignored){return lines;}
    }

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
        return response(context,title,artist,romanizeEnabled(context));
    }
    public static JSONObject response(Context context,String title,String artist,boolean romanize){
        JSONObject result=new JSONObject();
        String lines=display(context,get(context,title,artist),romanize);
        try{result.put("success",!lines.isEmpty()).put("source","local LRC")
            .put("syncedLyrics",lines);}catch(Exception ignored){}
        return result;
    }
}
