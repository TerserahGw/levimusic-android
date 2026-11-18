package com.reaaurelis.levimusic.bridge;

import android.util.Log;
import org.json.JSONArray;
import org.json.JSONObject;
import org.schabi.newpipe.extractor.NewPipe;
import org.schabi.newpipe.extractor.ServiceList;
import org.schabi.newpipe.extractor.downloader.Downloader;
import org.schabi.newpipe.extractor.downloader.Request;
import org.schabi.newpipe.extractor.downloader.Response;
import org.schabi.newpipe.extractor.search.SearchExtractor;
import org.schabi.newpipe.extractor.stream.StreamInfo;
import org.schabi.newpipe.extractor.stream.StreamInfoItem;
import org.schabi.newpipe.extractor.stream.AudioStream;
import org.schabi.newpipe.extractor.InfoItem;

import okhttp3.OkHttpClient;
import okhttp3.RequestBody;
import okhttp3.MediaType;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.concurrent.TimeUnit;

public final class MusicCatalog {
    private static final String TAG="LeviMusicCatalog";
    private static boolean initialized=false;
    private static final Object INIT_LOCK=new Object();
    private static final ArrayList<Song> RESULTS=new ArrayList<>();
    private static final OkHttpClient CLIENT=new OkHttpClient.Builder()
            .connectTimeout(12,TimeUnit.SECONDS)
            .readTimeout(18,TimeUnit.SECONDS)
            .followRedirects(true)
            .retryOnConnectionFailure(true)
            .build();

    private MusicCatalog(){}

    public static final class Song {
        public final String id,title,artist,url,art;
        public final long durationMs;
        public Song(String id,String title,String artist,String url,String art,long durationMs){
            this.id=id;this.title=title;this.artist=artist;
            this.url=url;this.art=art;this.durationMs=durationMs;
        }
        JSONObject json(){
            JSONObject x=new JSONObject();
            try{
                x.put("id",id).put("title",title).put("artist",artist)
                 .put("durationMs",durationMs).put("art",art);
            } catch(Exception ignored){}
            return x;
        }
    }

    private static void initialize() {
        if(initialized)return;
        synchronized(INIT_LOCK) {
            if(initialized)return;
            NewPipe.init(new Downloader(){
                @Override public Response execute(Request r) throws IOException {
                    okhttp3.Request.Builder b=new okhttp3.Request.Builder().url(r.url());
                    for(Map.Entry<String,List<String>> h:r.headers().entrySet()) {
                        for(String val:h.getValue())b.addHeader(h.getKey(),val);
                    }
                    if(r.headers().get("User-Agent")==null && r.headers().get("user-agent")==null) {
                        b.header("User-Agent","Mozilla/5.0 (Linux; Android 16) AppleWebKit/537.36 Chrome/131.0.0.0 Mobile Safari/537.36");
                    }
                    String method=r.httpMethod();
                    byte[] bytes=r.dataToSend();
                    RequestBody body=("POST".equals(method)||"PUT".equals(method))
                            ?RequestBody.create(bytes==null?new byte[0]:bytes,(MediaType)null):null;
                    b.method(method,body);
                    try(okhttp3.Response res=CLIENT.newCall(b.build()).execute()){
                        String data=res.body()==null?"":res.body().string();
                        return new Response(res.code(),res.message(),res.headers().toMultimap(),
                                data,res.request().url().toString());
                    }
                }
            });
            initialized=true;
        }
    }

    public static synchronized Song find(String id){
        if(id==null||id.isEmpty())return null;
        for(Song x:RESULTS)if(x.id.equals(id))return x;
        return null;
    }
    public static synchronized Song neighbor(String id,int direction){
        int at=-1;
        for(int i=0;i<RESULTS.size();i++)if(RESULTS.get(i).id.equals(id)){at=i;break;}
        if(at<0||RESULTS.isEmpty())return null;
        return RESULTS.get((at+direction+RESULTS.size())%RESULTS.size());
    }

    public static List<Song> search(String keyword) throws Exception {
        if(keyword==null||keyword.trim().isEmpty())return List.of();
        initialize();
        SearchExtractor extractor=ServiceList.YouTube.getSearchExtractor(
                keyword.trim(),List.of("videos"),"");
        extractor.fetchPage();
        List<Song> found=new ArrayList<>();
        for(InfoItem item:extractor.getInitialPage().getItems()) {
            if(!(item instanceof StreamInfoItem))continue;
            StreamInfoItem stream=(StreamInfoItem)item;
            String url=stream.getUrl();
            if(url==null||!url.startsWith("https://"))continue;
            String id=idFromUrl(url);
            if(id.isEmpty())continue;
            String art="";
            if(stream.getThumbnails()!=null&&!stream.getThumbnails().isEmpty())
                art=stream.getThumbnails().get(0).getUrl();
            found.add(new Song(id,stream.getName(),stream.getUploaderName(),url,art,
                    Math.max(0,stream.getDuration())*1000L));
            if(found.size()>=25)break;
        }
        synchronized(MusicCatalog.class){
            RESULTS.clear();
            RESULTS.addAll(found);
        }
        return found;
    }

    public static String idFromUrl(String url){
        if(url==null)return "";
        int at=url.indexOf("v=");
        if(at>=0){
            String piece=url.substring(at+2).split("[&#?]")[0];
            return piece.matches("[A-Za-z0-9_-]{11}")?piece:"";
        }
        return "";
    }

    public static JSONObject searchJson(String query) {
        JSONObject reply=new JSONObject();
        try{
            List<Song> data=search(query);
            JSONArray items=new JSONArray();
            for(Song s:data)items.put(s.json());
            reply.put("success",true).put("tracks",items)
                 .put("source","YouTube via NewPipeExtractor")
                 .put("count",items.length());
        } catch(Exception e){
            Log.w(TAG,"Search failed",e);
            try{reply.put("success",false).put("error",e.getMessage()==null?"Search unavailable":e.getMessage()).put("tracks",new JSONArray());}
            catch(Exception ignored){}
        }
        return reply;
    }

    public static String resolveAudio(Song song) throws Exception {
        if(song==null)throw new IOException("Song missing");
        initialize();
        StreamInfo info=StreamInfo.getInfo(ServiceList.YouTube,song.url);
        List<AudioStream> streams=info.getAudioStreams();
        if(streams==null||streams.isEmpty())throw new IOException("No playable audio stream");
        String best="";
        int bestRate=-1;
        for(AudioStream stream:streams){
            String url=stream.getContent();
            if(url==null||!url.startsWith("https://"))continue;
            int bitrate=stream.getAverageBitrate();
            int bonus=0;
            if(stream.getFormat()!=null){
                String type=stream.getFormat().name().toLowerCase();
                if(type.contains("m4a")||type.contains("aac"))bonus=30;
                else if(type.contains("opus"))bonus=20;
            }
            if(bitrate+bonus>bestRate){
                bestRate=bitrate+bonus;best=url;
            }
        }
        if(best.isEmpty())throw new IOException("No supported HTTPS audio stream");
        return best;
    }
}
