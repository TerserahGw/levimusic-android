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
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutorCompletionService;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;

/** Local LRC, word-timed TTML, multiple synced providers and video captions. */
public final class LyricsFinder {
    private static final String TAG="LeviMusicLyrics";
    private static final ExecutorService SOURCES=Executors.newFixedThreadPool(8,task->{
        Thread thread=new Thread(task,"LeviMusic-LyricsSource");thread.setDaemon(true);return thread;
    });
    private LyricsFinder(){}
    private static String enc(String text){
        try{return URLEncoder.encode(text==null?"":text,"UTF-8");}catch(Exception e){return "";}
    }
    private static String fetch(String address){
        try{
            URLConnection connection=new URL(address).openConnection();
            connection.setConnectTimeout(4500);connection.setReadTimeout(6500);
            connection.setRequestProperty("User-Agent","LeviMusic/1.4.1 (Android; lyrics client)");
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
    // Reject short, badly offset or empty responses before selecting a provider.
    static boolean usable(String raw,int duration){
        if(!hasTimedLyrics(raw)||raw.length()>500000)return false;
        int lines=0;long last=-1;
        for(String row:raw.split("\\r?\\n")){
            java.util.regex.Matcher m=java.util.regex.Pattern.compile(
                "^\\[(\\d{1,3}):(\\d{2})(?:[.:](\\d{1,3}))?\\]\\s*(.*)$").matcher(row);
            if(!m.find()||m.group(4).replaceAll("<\\d{1,3}:\\d{2}[.:]\\d{1,3}>","").trim().isEmpty())continue;
            long fraction=m.group(3)==null?0:Long.parseLong((m.group(3)+"000").substring(0,3));
            long at=Long.parseLong(m.group(1))*60000+Long.parseLong(m.group(2))*1000+fraction;
            if(at<last||duration>0&&at>duration+12000L)return false;
            last=at;++lines;
        }
        return lines>=3;
    }
    // The public lrc.red catalogue indexes Apple-style TTML by recording ISRC.
    // Match the recording before requesting its document to avoid wrong edits.
    private static String lrcRed(String title,String artist,int duration){
        try{
            JSONObject search=new JSONObject(fetch("https://lrc.red/search.json?q="+enc(title+" "+artist)));
            JSONArray hits=search.optJSONArray("hits");if(hits==null)return "";
            int best=-1;String isrc="";
            for(int i=0;i<Math.min(hits.length(),30);i++){
                JSONObject hit=hits.optJSONObject(i);if(hit==null)continue;
                String code=hit.optString("isrc").toUpperCase(Locale.ROOT);
                if(!code.matches("[A-Z]{2}[A-Z0-9]{3}\\d{7}"))continue;
                if(!normalize(title).equals(normalize(hit.optString("title"))))continue;
                double seconds=hit.optDouble("duration",0);
                if(duration>0&&seconds>0&&Math.abs(duration/1000.0-seconds)>3.0)continue;
                int rank=score(title,artist,duration,hit.optString("title"),
                    hit.optString("artist"),(long)(seconds*1000));
                if(rank>best){best=rank;isrc=code;}
            }
            if(best<130||isrc.isEmpty())return "";
            String raw=WordTimedTtml.convert(fetch("https://lrc.red/s/"+isrc+".ttml"));
            return usable(raw,duration)?raw:"";
        }catch(Exception ignored){return "";}
    }
    private static String betterLyrics(String title,String artist,int duration){
        if(title.isEmpty()||artist.isEmpty()||duration<=0)return "";
        try{
            JSONObject answer=new JSONObject(fetch("https://lyrics-api.boidu.dev/getLyrics?s="+
                enc(title)+"&a="+enc(artist)+"&d="+duration/1000));
            String raw=WordTimedTtml.convert(answer.optString("ttml"));
            return usable(raw,duration)?raw:"";
        }catch(Exception ignored){return "";}
    }
    private static String kugou(String title,String artist,int duration){
        if(title.isEmpty()||artist.isEmpty()||duration<=0)return "";
        try{
            JSONObject answer=new JSONObject(fetch("https://lyrics-api.boidu.dev/kugou/getLyrics?s="+
                enc(title)+"&a="+enc(artist)+"&d="+duration/1000));
            String raw=answer.optString("lyrics");
            return usable(raw,duration)?raw:"";
        }catch(Exception ignored){return "";}
    }
    private static String qq(String title,String artist,int duration){
        if(title.isEmpty()||artist.isEmpty()||duration<=0)return "";
        try{
            JSONObject answer=new JSONObject(fetch("https://api.betterlyrics.org/qq/getLyrics?s="+
                enc(title)+"&a="+enc(artist)+"&d="+duration/1000));
            String raw=WordTimedQrc.convert(answer.optString("lyrics"),title,artist,duration);
            return usable(raw,duration)?raw:"";
        }catch(Exception ignored){return "";}
    }
    private static String mark(long ms,char open,char close){
        return String.format(Locale.ROOT,"%c%02d:%05.2f%c",open,ms/60000,(ms%60000)/1000.0,close);
    }
    // LiriQo merges multiple providers; only accept a matching recording and real timed lines.
    private static String[] liriqo(String title,String artist,int duration){
        if(title.isEmpty()||artist.isEmpty()||duration<=0)return new String[]{"",""};
        try{
            JSONObject root=new JSONObject(fetch("https://api.liriqo-alfarrizi.my.id/v1/lyrics?title="+
                enc(title)+"&artist="+enc(artist)+"&duration="+duration/1000+"&fast=1"));
            JSONObject metadata=root.optJSONObject("metadata"),track=root.optJSONObject("primary");
            if(metadata==null||track==null)return new String[]{"",""};
            int rank=score(title,artist,duration,metadata.optString("title"),
                metadata.optString("artist"),(long)(metadata.optDouble("duration",0)*1000));
            if(rank<130||"plain".equals(track.optString("syncLevel")))return new String[]{"",""};
            JSONArray timed=track.optJSONArray("timed");if(timed==null)return new String[]{"",""};
            StringBuilder output=new StringBuilder();long last=-1;
            boolean wordTiming=track.optBoolean("nativeWordTiming");
            for(int i=0;i<Math.min(1000,timed.length());i++){
                JSONObject line=timed.optJSONObject(i);if(line==null)continue;
                long at=line.optLong("start",-1);String text=line.optString("text").trim();
                if(at<0||at<last||duration>0&&at>duration+12000L||text.isEmpty())continue;
                last=at;output.append(mark(at,'[',']')).append(' ');
                JSONArray words=wordTiming?line.optJSONArray("words"):null;
                StringBuilder rich=new StringBuilder(),plain=new StringBuilder();
                if(words!=null)for(int j=0;j<Math.min(100,words.length());j++){
                    JSONObject word=words.optJSONObject(j);if(word==null)continue;
                    long wordAt=word.optLong("start",-1);
                    if(wordAt<at||wordAt>at+30000)continue;
                    String token=word.optString("text");
                    rich.append(mark(wordAt,'<','>')).append(token);plain.append(token);
                }
                if(rich.length()>0&&normalize(plain.toString()).equals(normalize(text)))
                    output.append(rich);
                else output.append(text);
                output.append('\n');
            }
            String raw=output.toString();
            if(!usable(raw,duration))return new String[]{"",""};
            String provider=track.optString("upstreamProvider",track.optString("provider","LiriQo"));
            return new String[]{raw,"LiriQo / "+provider};
        }catch(Exception ignored){return new String[]{"",""};}
    }
    private static String lrclib(String title,String artist,int duration){
        try{
            if(!artist.isEmpty()&&duration>0&&duration<3600000){
                String address="https://lrclib.net/api/get?track_name="+enc(title)+"&artist_name="+enc(artist)
                    +"&duration="+Math.round(duration/1000f);
                JSONObject item=new JSONObject(fetch(address));
                String raw=item.optString("syncedLyrics");
                if(score(title,artist,duration,item.optString("trackName"),
                    item.optString("artistName"),(long)(item.optDouble("duration",0)*1000))>=100
                    &&usable(raw,duration))return raw;
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
                    String raw=item.optString("syncedLyrics");if(!usable(raw,duration))continue;
                    int rank=score(title,artist,duration,item.optString("trackName"),
                        item.optString("artistName"),(long)(item.optDouble("duration",0)*1000));
                    if(rank>best){best=rank;selected=raw;}
                }
                if(best>=100)return selected;
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
            return usable(raw,duration)?raw:"";
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
        if(raw.isEmpty()){
            final class Found {
                final String lyrics,provider;final int quality;
                Found(String l,String p,int q){lyrics=l;provider=p;quality=q;}
            }
            ExecutorCompletionService<Found> completed=new ExecutorCompletionService<>(SOURCES);
            List<Future<Found>> tasks=new ArrayList<>();
            tasks.add(completed.submit(()->new Found(lrcRed(title,artist,duration),"lrc.red",80)));
            tasks.add(completed.submit(()->new Found(betterLyrics(title,artist,duration),"BetterLyrics TTML",75)));
            tasks.add(completed.submit(()->new Found(lrclib(title,artist,duration),"LRCLIB",50)));
            tasks.add(completed.submit(()->new Found(netease(title,artist,duration),"NetEase",45)));
            tasks.add(completed.submit(()->new Found(kugou(title,artist,duration),"KuGou / BetterLyrics",40)));
            tasks.add(completed.submit(()->new Found(qq(title,artist,duration),"QQ Music / BetterLyrics",84)));
            tasks.add(completed.submit(()->{
                String[] result=liriqo(title,artist,duration);
                return new Found(result[0],result[1],result[0].contains("<")?78:55);
            }));
            tasks.add(completed.submit(()->new Found(captions(videoUrl),"YouTube captions",20)));
            final long deadline=System.nanoTime()+TimeUnit.SECONDS.toNanos(18);
            int best=-1;
            for(int i=0;i<tasks.size();i++){
                long remaining=deadline-System.nanoTime();if(remaining<=0)break;
                try{
                    Future<Found> next=completed.poll(remaining,TimeUnit.NANOSECONDS);
                    if(next==null)break;
                    Found found=next.get();
                    if(found.quality>best&&usable(found.lyrics,duration)){
                        raw=found.lyrics;source=found.provider;best=found.quality;
                        if(best>=84)break;
                    }
                }catch(Exception ignored){/* an unavailable source does not block the rest */}
            }
            for(Future<Found> task:tasks)if(!task.isDone())task.cancel(true);
        }
        if(raw.isEmpty()){raw=captions(videoUrl);source="YouTube captions";}
        JSONObject result=new JSONObject();
        try{result.put("success",!raw.isEmpty()).put("source",raw.isEmpty()?"none":source)
            .put("syncedLyrics",raw).put("romanizedLyrics",LyricsRomanizer.lrc(raw,title,language));}
        catch(Exception ignored){}
        return result;
    }
}

