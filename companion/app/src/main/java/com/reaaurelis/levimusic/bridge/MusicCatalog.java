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
import org.schabi.newpipe.extractor.playlist.PlaylistExtractor;
import org.schabi.newpipe.extractor.ListExtractor;
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
    private static final ArrayList<Song> PLAY_QUEUE=new ArrayList<>();
    private static boolean playlistQueueMode=false;
    private static final java.util.HashSet<String> REJECTED=new java.util.HashSet<>();
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

    public static final class ImportedPlaylist {
        public final String name;
        public final List<Song> songs;
        ImportedPlaylist(String name,List<Song> songs){this.name=name;this.songs=songs;}
    }
    public static ImportedPlaylist importPublicPlaylist(String link) throws Exception {
        if(link==null||link.length()>2048)throw new IllegalArgumentException("Invalid playlist URL");
        android.net.Uri uri=android.net.Uri.parse(link);
        String host=uri.getHost(),list=uri.getQueryParameter("list");
        if(host==null||!(host.equals("music.youtube.com")||host.equals("www.youtube.com")||
            host.equals("youtube.com")||host.equals("m.youtube.com"))||
            list==null||!list.matches("[A-Za-z0-9_-]{10,100}"))
            throw new IllegalArgumentException("Use a public YouTube or YouTube Music playlist URL");
        String canonical="https://www.youtube.com/playlist?list="+list;
        Exception primaryFailure=null;
        try {
            initialize();
            PlaylistExtractor extractor=ServiceList.YouTube.getPlaylistExtractor(canonical);
            extractor.fetchPage();
            String name=extractor.getName();
            List<Song> songs=new ArrayList<>();
            java.util.HashSet<String> ids=new java.util.HashSet<>();
            ListExtractor.InfoItemsPage<StreamInfoItem> page=extractor.getInitialPage();
            for(int pages=0;page!=null&&pages<15&&songs.size()<300;pages++){
                for(StreamInfoItem stream:page.getItems()){
                    String id=idFromUrl(stream.getUrl());
                    if(id.isEmpty()||!ids.add(id))continue;
                    String art="";
                    if(stream.getThumbnails()!=null&&!stream.getThumbnails().isEmpty())
                        art=stream.getThumbnails().get(stream.getThumbnails().size()-1).getUrl();
                    if(art.isEmpty())art="https://i.ytimg.com/vi/"+id+"/hqdefault.jpg";
                    songs.add(new Song(id,stream.getName(),stream.getUploaderName(),
                        "https://www.youtube.com/watch?v="+id,art,
                        Math.max(0,stream.getDuration())*1000L));
                    if(songs.size()>=300)break;
                }
                page=page.getNextPage()==null?null:extractor.getPage(page.getNextPage());
            }
            if(!songs.isEmpty())return new ImportedPlaylist(name,songs);
        }catch(Exception error){primaryFailure=error;}
        try {
            String endpoint="https://aurelis-symphony.go.altare.global/api/v1/playlists/import?url="+
                java.net.URLEncoder.encode(canonical,"UTF-8");
            try(okhttp3.Response response=CLIENT.newCall(new okhttp3.Request.Builder().url(endpoint).build()).execute()){
                if(!response.isSuccessful()||response.body()==null)throw new IOException("Aurelis playlist service unavailable");
                String body=response.body().string();
                if(body.length()>4_000_000)throw new IOException("Playlist response too large");
                JSONObject root=new JSONObject(body);
                JSONObject info=root.optJSONObject("playlist");
                org.json.JSONArray entries=root.optJSONArray("tracks");
                if(entries==null)throw new IOException("Playlist response has no tracks");
                String name=info==null?"":info.optString("title");
                if(name.isEmpty())name="YouTube "+list;
                List<Song> songs=new ArrayList<>();
                java.util.HashSet<String> ids=new java.util.HashSet<>();
                for(int i=0;i<Math.min(entries.length(),300);i++){
                    JSONObject item=entries.optJSONObject(i);
                    if(item==null)continue;
                    String id=item.optString("id");
                    if(!id.matches("[A-Za-z0-9_-]{11}"))id=idFromUrl(item.optString("url"));
                    if(!id.matches("[A-Za-z0-9_-]{11}")||!ids.add(id))continue;
                    String title=item.optString("title",item.optString("name",id));
                    String artist=item.optString("artist",item.optString("author",item.optString("uploader","YouTube")));
                    String art=item.optString("artwork",item.optString("thumbnail",item.optString("art","")));
                    if(art.isEmpty())art="https://i.ytimg.com/vi/"+id+"/hqdefault.jpg";
                    long duration=item.optLong("duration",0);
                    songs.add(new Song(id,title,artist,"https://www.youtube.com/watch?v="+id,art,
                        Math.max(0,duration)*1000L));
                }
                if(songs.isEmpty())throw new IOException("Aurelis returned no playable tracks");
                return new ImportedPlaylist(name,songs);
            }
        }catch(Exception fallback){
            if(primaryFailure!=null)fallback.addSuppressed(primaryFailure);
            throw fallback;
        }
    }

    public static synchronized Song find(String id){
        if(id==null||id.isEmpty())return null;
        for(Song x:RESULTS)if(x.id.equals(id))return x;
        for(Song x:PLAY_QUEUE)if(x.id.equals(id))return x;
        return null;
    }
    public static synchronized Song neighbor(String id,int direction){
        ArrayList<Song> queue=PLAY_QUEUE.isEmpty()?RESULTS:PLAY_QUEUE;
        int at=-1;
        for(int i=0;i<queue.size();i++)if(queue.get(i).id.equals(id)){at=i;break;}
        if(queue.isEmpty())return null;
        if(at<0)return direction>=0?queue.get(0):queue.get(queue.size()-1);
        if(direction>0 && at==queue.size()-1 && !playlistQueueMode)return null;
        if(queue.size()==1 && !playlistQueueMode)return null;
        return queue.get((at+direction+queue.size())%queue.size());
    }
    public static synchronized boolean isPlaylistQueue(){return playlistQueueMode;}
    /** Add an individually recommended track without losing search or playlist data. */
    public static synchronized void appendRecommendation(Song song){
        if(song==null||playlistQueueMode)return;
        if(PLAY_QUEUE.isEmpty())PLAY_QUEUE.addAll(RESULTS);
        for(Song existing:PLAY_QUEUE)if(song.id.equals(existing.id))return;
        if(PLAY_QUEUE.size()>=120)PLAY_QUEUE.remove(0);
        PLAY_QUEUE.add(song);
    }
    public static synchronized List<Song> queueSnapshot(){
        return new ArrayList<>(PLAY_QUEUE.isEmpty()?RESULTS:PLAY_QUEUE);
    }
    public static synchronized void activatePlaylistQueue(List<Song> tracks){
        playlistQueueMode=true;
        PLAY_QUEUE.clear();
        REJECTED.clear();
        if(tracks!=null)PLAY_QUEUE.addAll(tracks);
    }
    /** Start a fresh radio from the SELECTED search item, not the next search result. */
    public static synchronized void activateSearchQueue(Song seed){
        playlistQueueMode=false;
        PLAY_QUEUE.clear();
        REJECTED.clear();
        if(seed!=null)PLAY_QUEUE.add(seed);
    }
    public static synchronized void activateSearchQueue(){activateSearchQueue(null);}

    public static synchronized int upcomingCount(String currentId){
        int index=-1;
        for(int i=0;i<PLAY_QUEUE.size();i++)if(PLAY_QUEUE.get(i).id.equals(currentId)){index=i;break;}
        return index>=0?Math.max(0,PLAY_QUEUE.size()-index-1):0;
    }
    public static synchronized List<String> excludedIds(List<String> recentlyPlayed){
        java.util.LinkedHashSet<String> ids=new java.util.LinkedHashSet<>(REJECTED);
        if(recentlyPlayed!=null)ids.addAll(recentlyPlayed);
        for(Song item:PLAY_QUEUE)ids.add(item.id);
        return new ArrayList<>(ids);
    }
    public static synchronized boolean enqueue(Song song){
        if(song==null)return false;
        for(Song x:PLAY_QUEUE)if(x.id.equals(song.id))return false;
        if(PLAY_QUEUE.size()>=120)return false;
        PLAY_QUEUE.add(song);
        REJECTED.remove(song.id);
        return true;
    }
    public static synchronized boolean removeUpcoming(String id,String currentId){
        if(id==null||id.equals(currentId))return false;
        for(int i=0;i<PLAY_QUEUE.size();i++)if(id.equals(PLAY_QUEUE.get(i).id)){
            PLAY_QUEUE.remove(i);
            REJECTED.add(id);
            return true;
        }
        return false;
    }
    public static synchronized JSONObject queueJson(String currentId){
        JSONObject out=new JSONObject();
        JSONArray items=new JSONArray();
        int index=-1;
        for(int i=0;i<PLAY_QUEUE.size();i++)if(PLAY_QUEUE.get(i).id.equals(currentId)){index=i;break;}
        if(index<0 && currentId!=null && !currentId.isEmpty()){
            Song now=find(currentId);
            if(now!=null)items.put(now.json());
        }else if(index>=0){
            int visible=Math.min(PLAY_QUEUE.size(),4);
            for(int offset=0;offset<visible;offset++){
                int at=playlistQueueMode?(index+offset)%PLAY_QUEUE.size():index+offset;
                if(at>=PLAY_QUEUE.size())break;
                items.put(PLAY_QUEUE.get(at).json());
            }
        }
        try {
            out.put("success",true).put("tracks",items).put("count",items.length())
               .put("mode",playlistQueueMode?"playlist":"radio")
               .put("currentId",currentId==null?"":currentId)
               .put("editable",true);
        }catch(Exception ignored){}
        return out;
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
                art=stream.getThumbnails().get(stream.getThumbnails().size()-1).getUrl();
            if(art.isEmpty())art="https://i.ytimg.com/vi/"+id+"/hqdefault.jpg";
            found.add(new Song(id,stream.getName(),stream.getUploaderName(),url,art,
                    Math.max(0,stream.getDuration())*1000L));
            if(found.size()>=25)break;
        }
        synchronized(MusicCatalog.class){
            RESULTS.clear();
            RESULTS.addAll(found);
            // Keep the active playlist queue unchanged while the user searches.
        }
        return found;
    }

    public static JSONObject queueJson(){return queueJson("");}
    public static String idFromUrl(String url){
        if(url==null)return "";
        int at=url.indexOf("v=");
        if(at>=0){
            String piece=url.substring(at+2).split("[&#?]")[0];
            return piece.matches("[A-Za-z0-9_-]{11}")?piece:"";
        }
        int atShort=url.indexOf("youtu.be/");
        if(atShort>=0){
            String piece=url.substring(atShort+9).split("[&#?/]")[0];
            if(piece.matches("[A-Za-z0-9_-]{11}"))return piece;
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


    /**
     * Related-track discovery is best-effort: NewPipeExtractor can stop working
     * when the upstream service changes. This never bypasses a provider quota.
     */
    public static List<Song> recommendMany(Song previous,List<String> recentlyPlayed,int requested) throws Exception {
        if(previous==null || requested<1)return List.of();
        int target=Math.min(requested,3);
        initialize();
        java.util.Set<String> seen=new java.util.LinkedHashSet<>();
        if(recentlyPlayed!=null)seen.addAll(recentlyPlayed);
        seen.add(previous.id);
        List<Song> found=new ArrayList<>();
        try{
            StreamInfo info=StreamInfo.getInfo(ServiceList.YouTube,previous.url);
            appendRelated(info.getRelatedItems(),seen,found,target);
        }catch(Exception error){Log.i(TAG,"YouTube related items unavailable",error);}
        if(found.size()<target){
            String query=((previous.artist==null?"":previous.artist)+" "+(previous.title==null?"":previous.title)+" music").trim();
            try{
                SearchExtractor extractor=ServiceList.YouTube.getSearchExtractor(query,List.of("videos"),"");
                extractor.fetchPage();
                appendRelated(extractor.getInitialPage().getItems(),seen,found,target);
            }catch(Exception error){Log.w(TAG,"Fallback recommendations unavailable",error);}
        }
        return found;
    }
    private static void appendRelated(List<? extends InfoItem> items,java.util.Set<String> seen,
                                      List<Song> found,int limit){
        if(items==null)return;
        for(InfoItem item:items){
            if(found.size()>=limit)break;
            if(!(item instanceof StreamInfoItem))continue;
            StreamInfoItem stream=(StreamInfoItem)item;
            String id=idFromUrl(stream.getUrl());
            if(id.isEmpty() || !seen.add(id))continue;
            long duration=stream.getDuration();
            if(duration<45 || duration>900)continue;
            String art="";
            if(stream.getThumbnails()!=null && !stream.getThumbnails().isEmpty())
                art=stream.getThumbnails().get(stream.getThumbnails().size()-1).getUrl();
            if(art.isEmpty())art="https://i.ytimg.com/vi/"+id+"/hqdefault.jpg";
            found.add(new Song(id,stream.getName(),stream.getUploaderName(),stream.getUrl(),art,duration*1000L));
        }
    }

    /** Single-track compatibility wrapper. */
    public static Song recommendAfter(Song previous,List<String> recentlyPlayed) throws Exception {
        List<Song> result=recommendMany(previous,recentlyPlayed,1);
        return result.isEmpty()?null:result.get(0);
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
