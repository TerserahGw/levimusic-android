package com.reaaurelis.levimusic.bridge;

import android.content.Context;
import android.util.Log;
import org.json.JSONArray;
import org.json.JSONObject;
import org.schabi.newpipe.extractor.MediaFormat;
import org.schabi.newpipe.extractor.ServiceList;
import org.schabi.newpipe.extractor.stream.StreamInfo;
import org.schabi.newpipe.extractor.stream.SubtitlesStream;
import java.io.InputStream;
import java.net.URL;
import java.net.URLConnection;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/** Local LRC, LRCLIB, NetEase and original-video captions, in that order. */
public final class LyricsFinder {
    private static final String TAG="LeviMusicLyrics";
    private LyricsFinder(){}
    private static String enc(String text){
        try{return URLEncoder.encode(text==null?"":text,"UTF-8");}catch(Exception e){return "";}
    }
    private static String fetch(String address){
        try{
            URLConnection connection=new URL(address).openConnection();
            connection.setConnectTimeout(4500);connection.setReadTimeout(6500);
            connection.setRequestProperty("User-Agent","LeviMusic/1.2.7 (Android; lyrics client)");
            try(InputStream stream=connection.getInputStream()){
                byte[] data=stream.readNBytes(500000);
                return new String(data,StandardCharsets.UTF_8);
            }
        }catch(Exception e){Log.d(TAG,"Lyrics source unavailable: "+new URLSafe(address).host);return "";}
    }
    private static final class URLSafe {
        final String host;
        URLSafe(String address){String value="";try{value=new URL(address).getHost();}catch(Exception ignored){}host=value;}
    }
    private static String normalize(String s){
        return (s==null?"":s.toLowerCase(Locale.ROOT))
            .replaceAll("(?i)\\s*([\\[(](official|music video|visualizer|lyric video|audio|4k).*?[\\])])"," ")
            .replaceAll("(?i)\\s+- topic$","").replaceAll("[^\\p{L}\\p{N}]+"," ").trim();
    }
    private static int score(String wantTitle,String wantArtist,int durationMs,
                             String gotTitle,String gotArtist,long gotDurationMs){
        String title=normalize(wantTitle),candidate=normalize(gotTitle);
        if(title.isEmpty()||candidate.isEmpty())return -1;
        int score=title.equals(candidate)?100:Math.min(title.length(),candidate.length())>=4
            && (title.contains(candidate)||candidate.contains(title))?55:-1;
        if(score<0)return -1;
        String artist=normalize(wantArtist),other=normalize(gotArtist);
        if(!artist.isEmpty()&&!other.isEmpty()){
            if(artist.equals(other))score+=55;
            else if(artist.contains(other)||other.contains(artist))score+=25;
            else if(!artist.contains("topic")&&!other.contains("topic"))score-=30;
        }
        if(durationMs>0&&gotDurationMs>0){
            long delta=Math.abs((long)durationMs-gotDurationMs);
            if(delta>18000)return -1;
            score+=delta<=3000?35:delta<=8000?18:3;
        }
        return score;
    }
    private static boolean hasTimedLyrics(String raw){
        return raw!=null&&raw.matches("(?s).*\\[\\d{1,3}:\\d{2}(?:[.\\:]\\d+)?\\].*");
    }
    private static String lrclib(String title,String artist,int duration){
        try{
            if(!artist.isEmpty()&&duration>0&&duration<3600000){
                String address="https://lrclib.net/api/get?track_name="+enc(title)+"&artist_name="+enc(artist)
                    +"&duration="+Math.round(duration/1000f);
                JSONObject item=new JSONObject(fetch(address));
                String raw=item.optString("syncedLyrics");
                if(hasTimedLyrics(raw))return raw;
            }
        }catch(Exception ignored){}
        for(String query:new String[]{"track_name="+enc(title)+"&artist_name="+enc(artist),
                                     "q="+enc(title+" "+artist)}){
            String response=fetch("https://lrclib.net/api/search?"+query);
            if(response.isEmpty())continue;
            try{
                JSONArray results=new JSONArray(response);
                int best=-1;String selected="";
                for(int i=0;i<Math.min(20,results.length());i++){
                    JSONObject item=results.optJSONObject(i);if(item==null||item.optBoolean("instrumental"))continue;
                    String raw=item.optString("syncedLyrics");if(!hasTimedLyrics(raw))continue;
                    int rank=score(title,artist,duration,item.optString("trackName"),
                        item.optString("artistName"),(long)(item.optDouble("duration",0)*1000));
                    if(rank>best){best=rank;selected=raw;}
                }
                if(best>=70)return selected;
            }catch(Exception ignored){}
        }
        return "";
    }
    private static String netease(String title,String artist,int duration){
        String response=fetch("https://music.163.com/api/search/get/web?s="+
            enc(title+" "+artist)+"&type=1&limit=8");
        if(response.isEmpty())return "";
        try{
            JSONArray songs=new JSONObject(response).optJSONObject("result").optJSONArray("songs");
            if(songs==null)return "";
            int best=-1;long id=0;
            for(int i=0;i<songs.length();i++){
                JSONObject item=songs.optJSONObject(i);if(item==null)continue;
                JSONArray artists=item.optJSONArray("artists");
                JSONObject singer=artists==null?null:artists.optJSONObject(0);
                String candidateArtist=singer==null?"":singer.optString("name");
                long ms=item.optLong("duration",item.optLong("dt",0));
                int rank=score(title,artist,duration,item.optString("name"),candidateArtist,ms);
                if(rank>best){best=rank;id=item.optLong("id");}
            }
            if(best<100||id<=0)return "";
            JSONObject lyric=new JSONObject(fetch("https://music.163.com/api/song/lyric?id="+id+"&lv=1&kv=0&tv=0"));
            JSONObject lrc=lyric.optJSONObject("lrc");
            String raw=lrc==null?"":lrc.optString("lyric");
            return hasTimedLyrics(raw)?raw:"";
        }catch(Exception ignored){return "";}
    }
    private static long timestamp(String time){
        try{
            String[] hms=time.trim().replace(',','.').split(":");
            double value=0;
            for(String part:hms)value=value*60+Double.parseDouble(part);
            return Math.round(value*1000);
        }catch(Exception e){return -1;}
    }
    static String vtt(String input){
        if(input==null||!input.startsWith("WEBVTT"))return "";
        StringBuilder lrc=new StringBuilder();long previous=-1;
        String[] rows=input.split("\\r?\\n");
        for(int i=0;i<rows.length;i++){
            String row=rows[i];int arrow=row.indexOf("-->");if(arrow<0)continue;
            long ms=timestamp(row.substring(0,arrow));if(ms<0)continue;
            StringBuilder body=new StringBuilder();
            while(++i<rows.length&&!rows[i].trim().isEmpty()){
                String cleaned=rows[i].replaceAll("<[^>]*>","")
                    .replace("&amp;","&").replace("&lt;","<").replace("&gt;",">")
                    .replace("&nbsp;"," ").replaceAll("\\s+"," ").trim();
                if(!cleaned.isEmpty()&&!cleaned.startsWith("[")){
                    if(body.length()>0)body.append(' ');
                    body.append(cleaned);
                }
            }
            if(body.length()>0&&ms!=previous){
                lrc.append(String.format(Locale.ROOT,"[%02d:%05.2f] ",ms/60000,(ms%60000)/1000.0));
                lrc.append(body).append('\n');previous=ms;
            }
        }
        return lrc.toString();
    }
    private static String captions(String videoUrl){
        if(videoUrl==null||!videoUrl.startsWith("https://www.youtube.com/watch?v="))return "";
        try{
            StreamInfo info=StreamInfo.getInfo(ServiceList.YouTube,videoUrl);
            List<SubtitlesStream> candidates=info.getSubtitles();
            for(SubtitlesStream subtitle:candidates){
                if(subtitle.isAutoGenerated()||subtitle.getFormat()!=MediaFormat.VTT)continue;
                String raw=vtt(fetch(subtitle.getContent()));
                if(raw.split("\\n").length>=3)return raw;
            }
        }catch(Exception e){Log.d(TAG,"Video captions unavailable");}
        return "";
    }
    public static JSONObject find(Context context,String title,String artist,int duration,String videoUrl,String language){
        String raw=LyricsStore.get(context,title,artist),source="local LRC";
        if(raw.isEmpty()){raw=lrclib(title,artist,duration);source="LRCLIB";}
        if(raw.isEmpty()){raw=netease(title,artist,duration);source="NetEase";}
        if(raw.isEmpty()){raw=captions(videoUrl);source="YouTube captions";}
        JSONObject result=new JSONObject();
        try{result.put("success",!raw.isEmpty()).put("source",raw.isEmpty()?"none":source)
            .put("syncedLyrics",raw).put("romanizedLyrics",LyricsRomanizer.lrc(raw,title,language));}
        catch(Exception ignored){}
        return result;
    }
}
