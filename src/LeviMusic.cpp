// LeviMusic for LeviLauncher Android, v1.0.1 RC
// Music discovery / playback: Audius public API (not Spotify/YouTube scraping).
// Lyrics: optional LRCLIB synchronized lyrics, only when available.
#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>
#include <pl/Input.hpp>
#include "LucideIcons.hpp"
#include <cmath>
#include <jni.h>
#include <android/log.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <unistd.h>
#include <cerrno>
#include <utility>
#include <vector>

namespace {
constexpr const char* TAG = "LeviMusic";
constexpr const char* MODULE = "levimusic.player";
constexpr const char* API = "https://api.audius.co/v1";
constexpr const char* APP_NAME = "LeviMusicBedrock";

struct Track {
    std::string id;
    std::string title;
    std::string artist;
    std::string art;
};
struct Lyric {
    int ms = 0;
    std::string line;
};

enum class Action { Search, PlayPause, Next, Previous, Stop, SetVolume, Seek, Select };
struct Job { Action action; std::string query; };

bool clearException(JNIEnv* env, const char* step) {
    if (!env->ExceptionCheck()) return false;
    env->ExceptionClear();
    __android_log_print(ANDROID_LOG_WARN, TAG, "Java exception at %s", step);
    env->ExceptionDescribe();
    return true;
}

struct EnvScope {
    JavaVM* vm = nullptr;
    JNIEnv* env = nullptr;
    bool attached = false;
    explicit EnvScope(JavaVM* v) : vm(v) {
        if (!vm) return;
        if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) return;
        if (vm->functions->AttachCurrentThread(vm, &env, nullptr) == JNI_OK) attached = true;
    }
    ~EnvScope() { if (attached && vm) vm->DetachCurrentThread(); }
    explicit operator bool() const { return env != nullptr; }
};

// Convert UTF-8 safely; NewStringUTF alone can crash on song-title emojis.
jstring toJava(JNIEnv* env, const std::string& value) {
    if (value.size() > 1000000) return nullptr;
    jclass cls = env->FindClass("java/lang/String");
    if (!cls || clearException(env, "String class")) return nullptr;
    jmethodID ctor = env->GetMethodID(cls, "<init>", "([BLjava/lang/String;)V");
    jbyteArray bytes = env->NewByteArray(static_cast<jsize>(value.size()));
    if (bytes && !value.empty()) {
        env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(value.size()),
                                reinterpret_cast<const jbyte*>(value.data()));
    }
    jstring utf8 = env->NewStringUTF("UTF-8");
    jstring result = (ctor && bytes && utf8) ? (jstring)env->NewObject(cls, ctor, bytes, utf8) : nullptr;
    if (clearException(env, "UTF-8 toJava")) result = nullptr;
    if (utf8) env->DeleteLocalRef(utf8);
    if (bytes) env->DeleteLocalRef(bytes);
    env->DeleteLocalRef(cls);
    return result;
}
std::string fromJava(JNIEnv* env, jstring value) {
    if (!value) return {};
    jclass cls = env->FindClass("java/lang/String");
    if (!cls || clearException(env, "String class")) return {};
    jmethodID get = env->GetMethodID(cls, "getBytes", "(Ljava/lang/String;)[B");
    jstring utf8 = env->NewStringUTF("UTF-8");
    jbyteArray data = (get && utf8) ? (jbyteArray)env->CallObjectMethod(value, get, utf8) : nullptr;
    std::string out;
    if (!clearException(env, "fromJava UTF-8") && data) {
        jsize n = env->GetArrayLength(data);
        if (n > 0 && n <= 1000000) {
            out.resize(static_cast<size_t>(n));
            env->GetByteArrayRegion(data, 0, n, reinterpret_cast<jbyte*>(out.data()));
            if (clearException(env, "fromJava bytes")) out.clear();
        }
    }
    if (data) env->DeleteLocalRef(data);
    if (utf8) env->DeleteLocalRef(utf8);
    env->DeleteLocalRef(cls);
    return out;
}

std::string encode(JNIEnv* env, const std::string& value) {
    jclass cl = env->FindClass("java/net/URLEncoder");
    if (!cl || clearException(env, "URLEncoder class")) return {};
    jmethodID method = env->GetStaticMethodID(cl, "encode", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    if (!method || clearException(env, "encode method")) { env->DeleteLocalRef(cl); return {}; }
    jstring str = toJava(env, value);
    jstring utf8 = env->NewStringUTF("UTF-8");
    jstring result = (jstring)env->CallStaticObjectMethod(cl, method, str, utf8);
    std::string out;
    if (!clearException(env, "url encode")) out = fromJava(env, result);
    if (result) env->DeleteLocalRef(result);
    if (str) env->DeleteLocalRef(str);
    if (utf8) env->DeleteLocalRef(utf8);
    env->DeleteLocalRef(cl);
    return out;
}

// No external binaries / local MP3s. Java's platform HTTPS stack performs fetches.
std::string getHttp(JNIEnv* env, const std::string& url) {
    if (url.rfind("https://", 0) != 0) return {};
    jclass urlClass = env->FindClass("java/net/URL");
    if (!urlClass || clearException(env, "URL class")) return {};
    jmethodID ctor = env->GetMethodID(urlClass, "<init>", "(Ljava/lang/String;)V");
    jmethodID open = env->GetMethodID(urlClass, "openConnection", "()Ljava/net/URLConnection;");
    jstring jurl = toJava(env, url);
    jobject u = (ctor && jurl) ? env->NewObject(urlClass, ctor, jurl) : nullptr;
    jobject conn = u && open ? env->CallObjectMethod(u, open) : nullptr;
    bool ok = !clearException(env, "open HTTPS") && conn;
    std::string content;
    jobject stream = nullptr;
    jbyteArray bytes = nullptr;
    if (ok) {
        jclass connClass = env->FindClass("java/net/URLConnection");
        if (connClass) {
            jmethodID connectTimeout = env->GetMethodID(connClass, "setConnectTimeout", "(I)V");
            jmethodID readTimeout = env->GetMethodID(connClass, "setReadTimeout", "(I)V");
            jmethodID input = env->GetMethodID(connClass, "getInputStream", "()Ljava/io/InputStream;");
            if (connectTimeout) env->CallVoidMethod(conn, connectTimeout, 8000);
            if (readTimeout) env->CallVoidMethod(conn, readTimeout, 8000);
            // LRCLIB encourages a descriptive client header; some edges reject
            // default Java URLConnection clients.
            if(url.find("lrclib.net")!=std::string::npos){
                jmethodID header=env->GetMethodID(connClass,"setRequestProperty",
                    "(Ljava/lang/String;Ljava/lang/String;)V");
                jstring k=env->NewStringUTF("User-Agent");
                jstring v=env->NewStringUTF("LeviMusic/1.0.1 (https://github.com/TerserahGw/levimusic-android)");
                if(header && k && v)env->CallVoidMethod(conn,header,k,v);
                clearException(env,"LRCLIB user agent");
                if(k)env->DeleteLocalRef(k);
                if(v)env->DeleteLocalRef(v);
            }
            if (input) stream = env->CallObjectMethod(conn, input);
            clearException(env, "getInputStream");
            env->DeleteLocalRef(connClass);
        }
    }
    if (stream) {
        jclass streamClass = env->FindClass("java/io/InputStream");
        jmethodID read = streamClass ? env->GetMethodID(streamClass, "read", "([B)I") : nullptr;
        jmethodID close = streamClass ? env->GetMethodID(streamClass, "close", "()V") : nullptr;
        bytes = env->NewByteArray(8192);
        if (read && bytes) {
            content.reserve(24000);
            while (content.size() < 750000) {
                jint n = env->CallIntMethod(stream, read, bytes);
                if (clearException(env, "read HTTPS") || n <= 0) break;
                if (content.size() + static_cast<size_t>(n) > 750000) break;
                char buf[8192];
                env->GetByteArrayRegion(bytes, 0, n, reinterpret_cast<jbyte*>(buf));
                if (clearException(env, "byte copy")) break;
                content.append(buf, static_cast<size_t>(n));
            }
        }
        if (close) env->CallVoidMethod(stream, close);
        clearException(env, "close stream");
        if (streamClass) env->DeleteLocalRef(streamClass);
    }
    if (bytes) env->DeleteLocalRef(bytes);
    if (stream) env->DeleteLocalRef(stream);
    if (conn) env->DeleteLocalRef(conn);
    if (u) env->DeleteLocalRef(u);
    if (jurl) env->DeleteLocalRef(jurl);
    env->DeleteLocalRef(urlClass);
    return content;
}

std::string optString(JNIEnv* env, jobject obj, const char* field) {
    if (!obj) return {};
    jclass cl = env->FindClass("org/json/JSONObject");
    if (!cl) { clearException(env, "JSONObject class"); return {}; }
    jmethodID method = env->GetMethodID(cl, "optString", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    jstring key = env->NewStringUTF(field);
    jstring fallback = env->NewStringUTF("");
    jstring value = method ? (jstring)env->CallObjectMethod(obj, method, key, fallback) : nullptr;
    std::string out;
    if (!clearException(env, "optString")) out = fromJava(env, value);
    if (value) env->DeleteLocalRef(value);
    if (key) env->DeleteLocalRef(key);
    if (fallback) env->DeleteLocalRef(fallback);
    env->DeleteLocalRef(cl);
    return out;
}

jobject optObject(JNIEnv* env, jobject obj, const char* field) {
    if (!obj) return nullptr;
    jclass cl = env->FindClass("org/json/JSONObject");
    if (!cl) { clearException(env, "JSONObject class"); return nullptr; }
    jmethodID method = env->GetMethodID(cl, "optJSONObject", "(Ljava/lang/String;)Lorg/json/JSONObject;");
    jstring key = env->NewStringUTF(field);
    jobject result = method ? env->CallObjectMethod(obj, method, key) : nullptr;
    if (clearException(env, "optJSONObject")) result = nullptr;
    if (key) env->DeleteLocalRef(key);
    env->DeleteLocalRef(cl);
    return result;
}

jobject optArray(JNIEnv* env, jobject obj, const char* field) {
    if (!obj) return nullptr;
    jclass cl = env->FindClass("org/json/JSONObject");
    if (!cl) { clearException(env, "JSONObject class"); return nullptr; }
    jmethodID method = env->GetMethodID(cl, "optJSONArray", "(Ljava/lang/String;)Lorg/json/JSONArray;");
    jstring key = env->NewStringUTF(field);
    jobject result = method ? env->CallObjectMethod(obj, method, key) : nullptr;
    if (clearException(env, "optJSONArray")) result = nullptr;
    if (key) env->DeleteLocalRef(key);
    env->DeleteLocalRef(cl);
    return result;
}

jobject makeJson(JNIEnv* env, const std::string& text) {
    if (text.empty()) return nullptr;
    jclass cl = env->FindClass("org/json/JSONObject");
    if (!cl) { clearException(env, "JSONObject class"); return nullptr; }
    jmethodID ctor = env->GetMethodID(cl, "<init>", "(Ljava/lang/String;)V");
    jstring src = toJava(env, text);
    jobject result = ctor && src ? env->NewObject(cl, ctor, src) : nullptr;
    if (clearException(env, "JSON parse")) result = nullptr;
    if (src) env->DeleteLocalRef(src);
    env->DeleteLocalRef(cl);
    return result;
}

std::vector<Track> searchAudiusFrom(JNIEnv* env, const std::string& query, const std::string& baseUrl, const std::string& key={}) {
    std::vector<Track> tracks;
    if (query.empty()) return tracks;
    std::string url=baseUrl+"/tracks/search?query="+encode(env,query)+"&limit=20&app_name="+APP_NAME;
    if(!key.empty())url+="&api_key="+encode(env,key);
    std::string text=getHttp(env,url);
    jobject root = makeJson(env, text);
    if (!root) return tracks;
    jobject array = optArray(env, root, "data");
    if (array) {
        jclass arrClass = env->FindClass("org/json/JSONArray");
        jmethodID length = env->GetMethodID(arrClass, "length", "()I");
        jmethodID get = env->GetMethodID(arrClass, "optJSONObject", "(I)Lorg/json/JSONObject;");
        jint count = length ? env->CallIntMethod(array, length) : 0;
        if (clearException(env, "JSON array")) count = 0;
        for (jint i = 0; get && i < std::min(count, 20); ++i) {
            jobject obj = env->CallObjectMethod(array, get, i);
            if (clearException(env, "track") || !obj) continue;
            Track tr;
            tr.id = optString(env, obj, "id");
            tr.title = optString(env, obj, "title");
            jobject user = optObject(env, obj, "user");
            tr.artist = optString(env, user, "name");
            if (tr.artist.empty()) tr.artist = optString(env, user, "handle");
            if (user) env->DeleteLocalRef(user);
            if (!tr.id.empty() && !tr.title.empty() && optString(env, obj, "is_streamable") != "false") tracks.push_back(std::move(tr));
            env->DeleteLocalRef(obj);
        }
        env->DeleteLocalRef(arrClass);
        env->DeleteLocalRef(array);
    }
    env->DeleteLocalRef(root);
    return tracks;
}

std::vector<Track> searchAudius(JNIEnv* env, const std::string& query) {
    return searchAudiusFrom(env, query, API);
}

std::vector<Lyric> parseLrc(const std::string& raw) {
    std::vector<Lyric> lines;
    std::istringstream input(raw);
    std::string row;
    while (std::getline(input, row) && lines.size() < 1000) {
        if (row.empty() || row[0] != '[') continue;
        const auto end = row.find(']');
        if (end == std::string::npos || end > 16) continue;
        const auto colon = row.find(':');
        if (colon == std::string::npos || colon > end) continue;
        try {
            const int minutes = std::stoi(row.substr(1, colon - 1));
            const double seconds = std::stod(row.substr(colon + 1, end - colon - 1));
            if (minutes < 0 || minutes > 240 || seconds < 0 || seconds >= 60) continue;
            std::string line = row.substr(end + 1);
            if (line.empty()) continue;
            lines.push_back({static_cast<int>(minutes * 60000 + seconds * 1000), line});
        } catch (...) { /* skip invalid timestamps */ }
    }
    std::sort(lines.begin(), lines.end(), [](const Lyric& a, const Lyric& b) { return a.ms < b.ms; });
    return lines;
}

std::vector<Lyric> searchLyricsRaw(JNIEnv* env, const std::string& songTitle, const std::string& songArtist, int trackDurationMs) {
    const std::string url = "https://lrclib.net/api/search?track_name=" + encode(env, songTitle) +
                            (songArtist.empty()?"":"&artist_name="+encode(env,songArtist));
    const std::string text = getHttp(env, url);
    if (text.empty()) return {};
    jclass arrayClass = env->FindClass("org/json/JSONArray");
    if (!arrayClass) { clearException(env, "lyrics array class"); return {}; }
    jmethodID ctor = env->GetMethodID(arrayClass, "<init>", "(Ljava/lang/String;)V");
    jmethodID length = env->GetMethodID(arrayClass, "length", "()I");
    jmethodID get = env->GetMethodID(arrayClass, "optJSONObject", "(I)Lorg/json/JSONObject;");
    jstring source = toJava(env, text);
    jobject array = ctor && source ? env->NewObject(arrayClass, ctor, source) : nullptr;
    if (clearException(env, "lyrics parse")) array = nullptr;
    std::vector<Lyric> result;
    if (array && length && get) {
        jint count = env->CallIntMethod(array, length);
        if (clearException(env, "lyrics length")) count = 0;
        int bestScore = -999999;
        for (int i = 0; i < std::min(count, 20); i++) {
            jobject obj = env->CallObjectMethod(array, get, i);
            if (clearException(env, "lyrics entry") || !obj) continue;
            auto lrc = optString(env, obj, "syncedLyrics");
            int score = 0;
            if(trackDurationMs > 0) {
                try {
                    int seconds = static_cast<int>(std::stod(optString(env,obj,"duration")));
                    int delta = std::abs(seconds * 1000 - trackDurationMs);
                    score = 100000 - delta;
                    if(delta > 15000) {env->DeleteLocalRef(obj);continue;}
                } catch(...) { score = -100000; }
            }
            env->DeleteLocalRef(obj);
            if(!lrc.empty()){
                auto candidate = parseLrc(lrc);
                if(!candidate.empty() && score > bestScore) {
                    bestScore=score;
                    result=std::move(candidate);
                }
            }
        }
    }
    if (array) env->DeleteLocalRef(array);
    if (source) env->DeleteLocalRef(source);
    env->DeleteLocalRef(arrayClass);
    return result;
}



// Different uploads often append "[Official Audio]", "(Music Video)", uploader "- Topic",
// or use "Artist - Song" for the recording title. Never substitute a lyric with a
// substantially different duration when the real duration is known.
std::string cleanLyricTitle(const std::string& original) {
    std::string out=original;
    auto lower=[](std::string v){
        for(char& c:v)if(c>='A'&&c<='Z')c=static_cast<char>(c+32);
        return v;
    };
    for(const auto& tag:{"official","music video","visualizer","lyrics","lyric video","audio","4k"}) {
        std::string current=lower(out);
        size_t where=current.find(tag);
        if(where!=std::string::npos) {
            auto open=current.rfind('(',where);
            auto close=current.find(')',where);
            if(open!=std::string::npos && close!=std::string::npos && open<where) {
                out.erase(open,close-open+1);continue;
            }
            open=current.rfind('[',where);
            close=current.find(']',where);
            if(open!=std::string::npos&&close!=std::string::npos&&open<where)
                out.erase(open,close-open+1);
        }
    }
    auto start=out.find_first_not_of(" \t-");
    if(start==std::string::npos)return {};
    auto end=out.find_last_not_of(" \t-");
    return out.substr(start,end-start+1);
}

std::vector<Lyric> searchLyrics(JNIEnv* env,const Track& tr,int duration=0) {
    if(tr.title.empty())return {};
    auto first=searchLyricsRaw(env,tr.title,tr.artist,duration);
    if(!first.empty())return first;
    std::string cleaned=cleanLyricTitle(tr.title);
    if(cleaned.empty())return {};
    if(cleaned!=tr.title) {
        auto improved=searchLyricsRaw(env,cleaned,tr.artist,duration);
        if(!improved.empty())return improved;
    }
    const auto split=cleaned.find(" - ");
    if(split!=std::string::npos && split+3<cleaned.size()) {
        auto inferredArtist=cleanLyricTitle(cleaned.substr(0,split));
        auto withoutPrefix=cleanLyricTitle(cleaned.substr(split+3));
        if(!inferredArtist.empty() && !withoutPrefix.empty()) {
            auto inferred=searchLyricsRaw(env,withoutPrefix,inferredArtist,duration);
            if(!inferred.empty())return inferred;
        }
        auto result=searchLyricsRaw(env,withoutPrefix,tr.artist,duration);
        if(!result.empty())return result;
        return searchLyricsRaw(env,withoutPrefix,"",duration);
    }
    return searchLyricsRaw(env,cleaned,"",duration);
}

// Probe Audius redirects on the music worker (never on the game UI thread).
// Android's MediaPlayer is more reliable when given the final HTTPS audio URL.
std::string resolveStreamUrl(JNIEnv* env, const std::string& url) {
    if(url.rfind("https://",0)!=0)return {};
    jclass urlClass=env->FindClass("java/net/URL");
    if(!urlClass||clearException(env,"stream URL class"))return {};
    jmethodID ctor=env->GetMethodID(urlClass,"<init>","(Ljava/lang/String;)V");
    jmethodID open=env->GetMethodID(urlClass,"openConnection","()Ljava/net/URLConnection;");
    jstring jurl=toJava(env,url);
    jobject addr=ctor&&jurl?env->NewObject(urlClass,ctor,jurl):nullptr;
    jobject conn=addr&&open?env->CallObjectMethod(addr,open):nullptr;
    if(clearException(env,"stream connection"))conn=nullptr;
    std::string finalUrl;
    if(conn){
        jclass base=env->FindClass("java/net/URLConnection");
        jclass http=env->FindClass("java/net/HttpURLConnection");
        jmethodID ct=base?env->GetMethodID(base,"setConnectTimeout","(I)V"):nullptr;
        jmethodID rt=base?env->GetMethodID(base,"setReadTimeout","(I)V"):nullptr;
        jmethodID getUrl=base?env->GetMethodID(base,"getURL","()Ljava/net/URL;"):nullptr;
        jmethodID getType=base?env->GetMethodID(base,"getContentType","()Ljava/lang/String;"):nullptr;
        jmethodID follow=http?env->GetMethodID(http,"setInstanceFollowRedirects","(Z)V"):nullptr;
        jmethodID response=http?env->GetMethodID(http,"getResponseCode","()I"):nullptr;
        jmethodID disconnect=http?env->GetMethodID(http,"disconnect","()V"):nullptr;
        if(ct)env->CallVoidMethod(conn,ct,7000);
        if(rt)env->CallVoidMethod(conn,rt,7000);
        if(follow)env->CallVoidMethod(conn,follow,JNI_TRUE);
        jint status=response?env->CallIntMethod(conn,response):0;
        if(clearException(env,"stream HTTP headers"))status=0;
        if(status>=200&&status<300) {
            jobject redirected=getUrl?env->CallObjectMethod(conn,getUrl):nullptr;
            jstring mime=getType?(jstring)env->CallObjectMethod(conn,getType):nullptr;
            std::string mimeType=fromJava(env,mime);
            if(redirected){
                jmethodID toString=env->GetMethodID(urlClass,"toString","()Ljava/lang/String;");
                jstring str=toString?(jstring)env->CallObjectMethod(redirected,toString):nullptr;
                if(!clearException(env,"stream redirect"))finalUrl=fromJava(env,str);
                if(str)env->DeleteLocalRef(str);
                env->DeleteLocalRef(redirected);
            }
            if(mime)env->DeleteLocalRef(mime);
            __android_log_print(ANDROID_LOG_INFO,TAG,"Stream HTTP %d type %s",status,mimeType.c_str());
            if(mimeType.find("json")!=std::string::npos)finalUrl.clear();
        } else {
            __android_log_print(ANDROID_LOG_WARN,TAG,"Audius stream HTTP status %d",status);
        }
        if(disconnect)env->CallVoidMethod(conn,disconnect);
        clearException(env,"stream disconnect");
        if(base)env->DeleteLocalRef(base);
        if(http)env->DeleteLocalRef(http);
        env->DeleteLocalRef(conn);
    }
    if(addr)env->DeleteLocalRef(addr);
    if(jurl)env->DeleteLocalRef(jurl);
    env->DeleteLocalRef(urlClass);
    return finalUrl.rfind("https://",0)==0?finalUrl:std::string{};
}

// All MediaPlayer method calls occur on a single attached worker thread.

constexpr int BRIDGE_PORT=38771;
std::string bridgeGet(const std::string& path,const std::string& token) {
    if(token.size()!=48)return {};
    for(char c:token)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return {};
    int fd=::socket(AF_INET,SOCK_STREAM,0);
    if(fd<0)return {};
    // External searches run on the native worker and may take several seconds.
    const int timeoutMs=path.rfind("/v1/search?",0)==0?30000:
                       path.rfind("/v1/play?",0)==0?3500:1500;
    timeval timeout{};timeout.tv_sec=timeoutMs/1000;
    timeout.tv_usec=(timeoutMs%1000)*1000;
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    sockaddr_in address{};address.sin_family=AF_INET;
    address.sin_port=htons(BRIDGE_PORT);
    inet_pton(AF_INET,"127.0.0.1",&address.sin_addr);
    std::string body;
    if(::connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0) {
        std::string request="GET "+path+" HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\nX-LeviMusic-Token: "+token+"\r\n\r\n";
        if(::send(fd,request.data(),request.size(),MSG_NOSIGNAL)>0) {
            char buf[4096];
            while(body.size()<262144) {
                ssize_t got=::recv(fd,buf,sizeof(buf),0);
                if(got<=0)break;
                body.append(buf,static_cast<size_t>(got));
            }
        }
    }
    ::close(fd);
    if(body.rfind("HTTP/1.1 200",0)!=0)return {};
    size_t pos=body.find("\r\n\r\n");
    return pos!=std::string::npos?body.substr(pos+4):std::string{};
}

std::vector<Track> parseBridgeSearch(JNIEnv* env,const std::string& source) {
    std::vector<Track> tracks;
    jobject root=makeJson(env,source);
    if(!root)return tracks;
    jobject array=optArray(env,root,"tracks");
    if(array) {
        jclass cls=env->FindClass("org/json/JSONArray");
        jmethodID size=cls?env->GetMethodID(cls,"length","()I"):nullptr;
        jmethodID item=cls?env->GetMethodID(cls,"optJSONObject","(I)Lorg/json/JSONObject;"):nullptr;
        jint count=size?env->CallIntMethod(array,size):0;
        if(clearException(env,"Bridge results count"))count=0;
        for(jint i=0;item && i<std::min(count,25);i++) {
            jobject object=env->CallObjectMethod(array,item,i);
            if(clearException(env,"Bridge result")||!object)continue;
            Track song;
            song.id=optString(env,object,"id");
            song.title=optString(env,object,"title");
            song.artist=optString(env,object,"artist");
            song.art=optString(env,object,"art");
            if(!song.id.empty()&&!song.title.empty())tracks.push_back(std::move(song));
            env->DeleteLocalRef(object);
        }
        if(cls)env->DeleteLocalRef(cls);
        env->DeleteLocalRef(array);
    }
    env->DeleteLocalRef(root);
    return tracks;
}

struct BridgeState {
    bool connected=false,available=false,playing=false;
    std::string title,artist,provider,message,art;
    int elapsed=0,duration=0;
};
BridgeState decodeBridge(JNIEnv* env,const std::string& response) {
    BridgeState result;
    auto root=makeJson(env,response);
    if(!root)return result;
    result.connected=true;
    result.available=optString(env,root,"available")=="true";
    result.playing=optString(env,root,"playing")=="true";
    result.title=optString(env,root,"title");
    result.artist=optString(env,root,"artist");
    result.provider=optString(env,root,"provider");
    result.message=optString(env,root,"message");
    result.art=optString(env,root,"art");
    try{result.elapsed=std::max(0,std::stoi(optString(env,root,"positionMs")));}catch(...){}
    try{result.duration=std::max(0,std::stoi(optString(env,root,"durationMs")));}catch(...){}
    env->DeleteLocalRef(root);
    return result;
}


bool registerCover(JNIEnv* env,const std::string& url,const std::string& id){
    if(url.rfind("https://",0)!=0 || url.size()>2048)return false;
    const std::string raw=getHttp(env,url);
    if(raw.empty() || raw.size()>750000)return false;
    const int dimension=96;
    jbyteArray bytes=env->NewByteArray(static_cast<jsize>(raw.size()));
    if(!bytes)return false;
    env->SetByteArrayRegion(bytes,0,static_cast<jsize>(raw.size()),reinterpret_cast<const jbyte*>(raw.data()));
    jclass decoder=env->FindClass("android/graphics/BitmapFactory");
    jmethodID decode=decoder?env->GetStaticMethodID(decoder,"decodeByteArray","([BII)Landroid/graphics/Bitmap;"):nullptr;
    jobject bitmap=decode?env->CallStaticObjectMethod(decoder,decode,bytes,0,static_cast<jint>(raw.size())):nullptr;
    bool okay=!clearException(env,"BitmapFactory decode") && bitmap!=nullptr;
    if(decoder)env->DeleteLocalRef(decoder);
    env->DeleteLocalRef(bytes);
    if(!okay)return false;
    jclass bmp=env->FindClass("android/graphics/Bitmap");
    jmethodID scale=bmp?env->GetStaticMethodID(bmp,"createScaledBitmap","(Landroid/graphics/Bitmap;IIZ)Landroid/graphics/Bitmap;"):nullptr;
    jobject reduced=scale?env->CallStaticObjectMethod(bmp,scale,bitmap,dimension,dimension,JNI_TRUE):nullptr;
    okay=!clearException(env,"scale cover") && reduced!=nullptr;
    env->DeleteLocalRef(bitmap);
    if(!okay){if(bmp)env->DeleteLocalRef(bmp);return false;}
    jintArray pixels=env->NewIntArray(dimension*dimension);
    jmethodID getPixels=bmp?env->GetMethodID(bmp,"getPixels","([IIIIIII)V"):nullptr;
    if(pixels && getPixels)env->CallVoidMethod(reduced,getPixels,pixels,0,dimension,0,0,dimension,dimension);
    okay=pixels && !clearException(env,"getPixels");
    std::vector<unsigned char> rgba(dimension*dimension*4);
    if(okay){
        std::vector<jint> argb(dimension*dimension);
        env->GetIntArrayRegion(pixels,0,dimension*dimension,argb.data());
        okay=!clearException(env,"cover pixels");
        if(okay)for(int i=0;i<dimension*dimension;i++){
            uint32_t pixel=static_cast<uint32_t>(argb[static_cast<size_t>(i)]);
            rgba[static_cast<size_t>(i)*4+0]=static_cast<unsigned char>((pixel>>16)&255);
            rgba[static_cast<size_t>(i)*4+1]=static_cast<unsigned char>((pixel>>8)&255);
            rgba[static_cast<size_t>(i)*4+2]=static_cast<unsigned char>(pixel&255);
            rgba[static_cast<size_t>(i)*4+3]=static_cast<unsigned char>((pixel>>24)&255);
        }
    }
    if(pixels)env->DeleteLocalRef(pixels);
    env->DeleteLocalRef(reduced);
    if(bmp)env->DeleteLocalRef(bmp);
    return okay && pl::modmenu::registerImage(id,rgba,dimension,dimension);
}

class AndroidPlayer {
    jobject player_ = nullptr;
public:
    ~AndroidPlayer() = default;
    void release(JNIEnv* env) {
        if (!player_) return;
        jclass cls = env->GetObjectClass(player_);
        jmethodID release = env->GetMethodID(cls, "release", "()V");
        if (release) env->CallVoidMethod(player_, release);
        clearException(env, "MediaPlayer release");
        env->DeleteLocalRef(cls);
        env->DeleteGlobalRef(player_);
        player_ = nullptr;
    }
    bool startUrl(JNIEnv* env, const std::string& url, int volume) {
        release(env);
        const std::string playableUrl=resolveStreamUrl(env,url);
        if(playableUrl.empty())return false;
        jclass cls = env->FindClass("android/media/MediaPlayer");
        if (!cls || clearException(env, "MediaPlayer class")) return false;
        jmethodID ctor = env->GetMethodID(cls, "<init>", "()V");
        jobject local = ctor ? env->NewObject(cls, ctor) : nullptr;
        if (!local || clearException(env, "MediaPlayer ctor")) { env->DeleteLocalRef(cls); return false; }
        player_ = env->NewGlobalRef(local);
        env->DeleteLocalRef(local);
        jmethodID setAudioStreamType = env->GetMethodID(cls, "setAudioStreamType", "(I)V");
        jmethodID setDataSource = env->GetMethodID(cls, "setDataSource", "(Ljava/lang/String;)V");
        jmethodID prepare = env->GetMethodID(cls, "prepare", "()V");
        jmethodID start = env->GetMethodID(cls, "start", "()V");
        jstring jurl = toJava(env, playableUrl);
        if (setAudioStreamType) env->CallVoidMethod(player_, setAudioStreamType, 3); // STREAM_MUSIC
        if (setDataSource && jurl) env->CallVoidMethod(player_, setDataSource, jurl);
        bool ok = !clearException(env, "setDataSource") && prepare && start;
        if (ok) env->CallVoidMethod(player_, prepare); // blocking: isolated from game main thread
        ok = ok && !clearException(env, "prepare");
        if (ok) env->CallVoidMethod(player_, start);
        ok = ok && !clearException(env, "start");
        if (jurl) env->DeleteLocalRef(jurl);
        env->DeleteLocalRef(cls);
        if (!ok) release(env);
        if (ok) setVolume(env, volume);
        return ok;
    }
    bool isPlaying(JNIEnv* env) {
        if (!player_) return false;
        jclass cls = env->GetObjectClass(player_);
        jmethodID fn = env->GetMethodID(cls, "isPlaying", "()Z");
        jboolean playing = fn ? env->CallBooleanMethod(player_, fn) : JNI_FALSE;
        if (clearException(env, "isPlaying")) playing = JNI_FALSE;
        env->DeleteLocalRef(cls);
        return playing == JNI_TRUE;
    }
    void toggle(JNIEnv* env) {
        if (!player_) return;
        jclass cls = env->GetObjectClass(player_);
        const bool playing = isPlaying(env);
        jmethodID fn = env->GetMethodID(cls, playing ? "pause" : "start", "()V");
        if (fn) env->CallVoidMethod(player_, fn);
        clearException(env, "toggle");
        env->DeleteLocalRef(cls);
    }
    int position(JNIEnv* env) {
        if (!player_) return 0;
        jclass cls = env->GetObjectClass(player_);
        jmethodID fn = env->GetMethodID(cls, "getCurrentPosition", "()I");
        jint pos = fn ? env->CallIntMethod(player_, fn) : 0;
        if (clearException(env, "position")) pos = 0;
        env->DeleteLocalRef(cls);
        return std::max(0, static_cast<int>(pos));
    }
    void setVolume(JNIEnv* env, int volume) {
        if (!player_) return;
        jclass cls = env->GetObjectClass(player_);
        jmethodID fn = env->GetMethodID(cls, "setVolume", "(FF)V");
        const float v = std::clamp(volume, 0, 100) / 100.f;
        if (fn) env->CallVoidMethod(player_, fn, v, v);
        clearException(env, "volume");
        env->DeleteLocalRef(cls);
    }
    int duration(JNIEnv* env) {
        if(!player_) return 0;
        jclass cls=env->GetObjectClass(player_);
        jmethodID m=env->GetMethodID(cls,"getDuration","()I");
        jint n=m?env->CallIntMethod(player_,m):0;
        if(clearException(env,"duration")) n=0;
        env->DeleteLocalRef(cls);
        return std::max(0,static_cast<int>(n));
    }
    void seek(JNIEnv* env,int ms) {
        if(!player_)return;
        jclass cls=env->GetObjectClass(player_);
        jmethodID m=env->GetMethodID(cls,"seekTo","(I)V");
        if(m)env->CallVoidMethod(player_,m,std::max(0,ms));
        clearException(env,"seek");
        env->DeleteLocalRef(cls);
    }

};


// A lightweight, in-game UI rendered by the official preloader HUD primitives.
// No external LeviLauncher buttons are registered.
std::string shorten(const std::string& value,size_t bytes) {
    if(value.size()<=bytes)return value;
    size_t n=bytes>3?bytes-3:bytes;
    while(n>0 && (static_cast<unsigned char>(value[n])&0xC0u)==0x80u)--n;
    return value.substr(0,n)+"...";
}
std::string clockText(int ms) {
    int n=std::max(0,ms/1000);
    return std::to_string(n/60)+":"+(n%60<10?"0":"")+std::to_string(n%60);
}
struct Rect {
    float x{},y{},w{},h{};
    bool contains(float a,float b)const{return a>=x&&b>=y&&a<=x+w&&b<=y+h;}
};
struct UI {
    float w{},h{},s{};
    Rect mini,outer,left,right,close,settings,searchBox,searchGo,prev,play,next,stop,seek,pagePrev,pageNext;
    std::vector<Rect> rows;
    bool ok()const{return w>0&&h>0;}
};
UI measure(int hudScale=84,int hudX=3,int hudY=93,int panelTop=26) {
    UI p;
    const auto surface=pl::modmenu::getHudSurfaceSize();
    p.w=surface.width;p.h=surface.height;
    if(!p.ok())return p;
    p.s=std::clamp(p.h/540.f,.65f,2.5f);
    const float u=p.s, factor=std::clamp(hudScale,55,125)/100.f;
    const float miniW=std::min(267*u*factor,p.w*.45f);
    const float miniH=50*u*factor;
    const float x=std::clamp((p.w-miniW)*std::clamp(hudX,0,100)/100.f,4*u,p.w-miniW-4*u);
    const float y=std::clamp((p.h-miniH)*std::clamp(hudY,0,100)/100.f,4*u,p.h-miniH-4*u);
    p.mini={x,y,miniW,miniH};
    const float topPercent=std::clamp(panelTop,15,35)/100.f;
    p.outer={p.w*.07f,p.h*topPercent,p.w*.86f,p.h*(.95f-topPercent)};
    const float contentY=p.outer.y+54*u;
    const float contentH=p.outer.h-67*u;
    const float contentX=p.outer.x+16*u;
    const float contentW=p.outer.w-32*u;
    p.left={contentX,contentY,contentW*.43f-5*u,contentH};
    p.right={contentX+contentW*.43f+5*u,contentY,contentW*.57f-5*u,contentH};
    p.close={p.outer.x+p.outer.w-42*u,p.outer.y+10*u,30*u,30*u};
    p.settings={p.close.x-40*u,p.close.y,30*u,30*u};
    p.searchBox={p.right.x+12*u,p.right.y+11*u,p.right.w-82*u,36*u};
    p.searchGo={p.right.x+p.right.w-58*u,p.right.y+11*u,46*u,36*u};
    const float b=p.left.y+p.left.h-58*u,mid=p.left.x+p.left.w*.5f;
    p.stop={p.left.x+14*u,b,36*u,44*u};
    p.prev={mid-89*u,b,45*u,44*u};
    p.play={mid-33*u,b,66*u,44*u};
    p.next={mid+44*u,b,45*u,44*u};
    p.seek={p.left.x+20*u,b-41*u,p.left.w-40*u,15*u};
    const float rowY=p.right.y+68*u;
    const float bottom=p.right.y+p.right.h-44*u;
    const float rowH=std::max(20*u,(bottom-rowY)/6.f);
    for(int i=0;i<6;i++)p.rows.push_back({p.right.x+12*u,rowY+i*rowH,p.right.w-24*u,rowH-3*u});
    p.pagePrev={p.right.x+p.right.w-113*u,p.right.y+p.right.h-39*u,38*u,31*u};
    p.pageNext={p.right.x+p.right.w-62*u,p.right.y+p.right.h-39*u,38*u,31*u};
    return p;
}

constexpr uint32_t C_BG=0xF5101114U;
constexpr uint32_t C_PANEL=0xEF1C1E23U;
constexpr uint32_t C_BORDER=0x447C8088U;
constexpr uint32_t C_TEXT=0xFFFFFFFFU;
constexpr uint32_t C_DIM=0xFFA9ABB4U;
constexpr uint32_t C_MINT=0xFFE4E6E8U;

class LeviMusic {
    ll::mod::NativeMod& self_;
    JavaVM* vm_=nullptr;
    std::thread worker_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Job> jobs_;
    std::vector<Track> results_;
    std::vector<Lyric> lyrics_;
    std::string query_="lofi", title_="LeviMusic", artist_="Online Music";
    std::string info_="Tap to open your music library", lyric_, apiKey_, bridgeKey_;
    std::string coverUrl_,coverImageId_;
    int coverCount_=0;
    int selected_=-1,page_=0,volume_=75,elapsed_=0,total_=0;
    bool open_=false,typing_=false,hud_=true,lyricsOn_=true,playing_=false,shutdown_=false;
    std::atomic<bool> enabled_{false};
    std::atomic<int> hudSize_{84},hudX_{3},hudY_{93},panelTop_{26};
    std::atomic<bool> animation_{true};
    std::atomic<bool> useBridge_{true},lyricsBelow_{false};
    std::chrono::steady_clock::time_point lyricChanged_{std::chrono::steady_clock::now()};
    std::chrono::steady_clock::time_point bridgePositionStamp_{std::chrono::steady_clock::now()};

public:
    LeviMusic():self_(*ll::mod::NativeMod::current()){}
    static LeviMusic& instance(){static LeviMusic mod;return mod;}
    bool load(){
        vm_=self_.getJavaVM();
        __android_log_print(ANDROID_LOG_INFO,TAG,"LeviMusic v1.0.1 RC loaded, vm=%p",(void*)vm_);
        return vm_!=nullptr;
    }
    bool enable(){
        {
            std::lock_guard lock(mu_);
            shutdown_=false;open_=false;typing_=false;
        }
        bool ok=pl::modmenu::ModuleBuilder(MODULE,"LeviMusic Online")
            .modId(self_.getId()).defaultEnabled(true).hideInHudEditor(true)
            .description("Native music HUD. Bridge mode controls Spotify or YouTube Music via companion Android app.")
            .config("volume","Volume",pl::modmenu::ConfigType::SliderInt,"75","0","100")
            .config("useBridge","Spotify / YouTube Music Bridge",pl::modmenu::ConfigType::Toggle,"true")
            .config("bridgeKey","Bridge Key (copy from companion app)",pl::modmenu::ConfigType::Text,"")
            .config("lyricBelow","Show lyric bubble below HUD",pl::modmenu::ConfigType::Toggle,"false")
            .config("apiKey","Audius API Key (optional)",pl::modmenu::ConfigType::Text,"")
            .config("hud","Show compact HUD",pl::modmenu::ConfigType::Toggle,"true")
            .config("lyrics","Enable lyrics",pl::modmenu::ConfigType::Toggle,"true")
            .config("hudSize","HUD Size (%)",pl::modmenu::ConfigType::SliderInt,"84","55","125")
            .config("hudX","HUD Horizontal Position (0-100%)",pl::modmenu::ConfigType::SliderInt,"3","0","100")
            .config("hudY","HUD Vertical Position (0-100%)",pl::modmenu::ConfigType::SliderInt,"93","0","100")
            .config("panelTop","Menu Distance from Top (%)",pl::modmenu::ConfigType::SliderInt,"26","15","35")
            .config("animation","Subtle Animations",pl::modmenu::ConfigType::Toggle,"true")
            .onConfigChanged([](std::string_view,std::string_view k,std::string_view v){
                instance().setConfig(k,v);
            }).registerModule();
        if(!ok)return false;
        static std::once_flag registered;
        std::call_once(registered,[](){
            pl::input::registerTouchCallback([](const pl::input::TouchEvent& e){return instance().touch(e);});
            pl::input::registerTextInputCallback([](const pl::input::TextInputEvent& e){return instance().input(e);});
            pl::input::registerKeyCallback([](const pl::input::KeyEvent& e){return instance().key(e);});
        });
        levimusic_icons::registerAll();
        enabled_=true;
        worker_=std::thread([this](){process();});
        paint();
        return true;
    }
    bool disable(){
        enabled_=false;
        {std::lock_guard lock(mu_);shutdown_=true;typing_=false;jobs_.clear();}
        cv_.notify_all();
        if(worker_.joinable())worker_.join();
        pl::modmenu::submitDrawCommands(MODULE,std::span<const pl::modmenu::DrawCommand>{});
        pl::modmenu::unregisterModule(MODULE);
        return true;
    }
    bool unload(){return true;}
private:
    void setConfig(std::string_view k,std::string_view v){
        if(k=="useBridge") {
            useBridge_=v=="true"||v=="1";
        } else if(k=="lyricBelow") {
            lyricsBelow_=v=="true"||v=="1";
        } else if(k=="hudSize") {
            try{hudSize_=std::clamp(std::stoi(std::string(v)),55,125);}catch(...){}
        } else if(k=="hudX") {
            try{hudX_=std::clamp(std::stoi(std::string(v)),0,100);}catch(...){}
        } else if(k=="hudY") {
            try{hudY_=std::clamp(std::stoi(std::string(v)),0,100);}catch(...){}
        } else if(k=="panelTop") {
            try{panelTop_=std::clamp(std::stoi(std::string(v)),15,35);}catch(...){}
        } else if(k=="animation") {
            animation_ = v=="1"||v=="true";
        } else {
            std::lock_guard lock(mu_);
            if(k=="bridgeKey")bridgeKey_=std::string(v).substr(0,100);
            else if(k=="apiKey")apiKey_=std::string(v).substr(0,200);
            else if(k=="hud")hud_=v=="1"||v=="true";
            else if(k=="lyrics")lyricsOn_=v=="1"||v=="true";
            else if(k=="volume"){
                try{volume_=std::clamp(std::stoi(std::string(v)),0,100);}catch(...){}
                jobs_.push_back({Action::SetVolume,{}});cv_.notify_one();
            }
        }
        paint();
    }
    void addJob(Action action,std::string arg={}){
        std::lock_guard lock(mu_);
        if(!shutdown_&&jobs_.size()<30){jobs_.push_back({action,std::move(arg)});cv_.notify_one();}
    }
    void update(std::string title,std::string artist,std::string message){
        {
            std::lock_guard lock(mu_);
            title_=std::move(title);artist_=std::move(artist);info_=std::move(message);
        }
        paint();
    }
    bool touch(const pl::input::TouchEvent& e){
        if(!enabled_ || (e.action!=0&&e.action!=1))return false;
        UI l=measure(hudSize_,hudX_,hudY_,panelTop_);if(!l.ok())return false;
        bool opened,hud;
        {std::lock_guard lock(mu_);opened=open_;hud=hud_;}
        if(e.action==0) return opened || (hud&&l.mini.contains(e.x,e.y));
        if(!opened){
            if(hud&&l.mini.contains(e.x,e.y)){
                {std::lock_guard lock(mu_);open_=true;}
                paint();return true;
            }
            return false;
        }
        if(l.settings.contains(e.x,e.y)){
            {std::lock_guard lock(mu_);open_=false;typing_=false;}
            pl::input::hideKeyboard();
            pl::modmenu::requestOpenMenu();
        } else if(l.close.contains(e.x,e.y)||!l.outer.contains(e.x,e.y)){
            {std::lock_guard lock(mu_);open_=false;typing_=false;}
            pl::input::hideKeyboard();
        } else if(l.searchBox.contains(e.x,e.y)){
            {std::lock_guard lock(mu_);if(!typing_)query_.clear();typing_=true;}
            pl::input::showKeyboard();
        } else if(l.searchGo.contains(e.x,e.y)){
            std::string q;
            {std::lock_guard lock(mu_);q=query_;typing_=false;page_=0;}
            pl::input::hideKeyboard();addJob(Action::Search,q);
        } else if(l.prev.contains(e.x,e.y))addJob(Action::Previous);
        else if(l.play.contains(e.x,e.y))addJob(Action::PlayPause);
        else if(l.next.contains(e.x,e.y))addJob(Action::Next);
        else if(l.stop.contains(e.x,e.y))addJob(Action::Stop);
        else if(l.seek.contains(e.x,e.y)){
            int duration;
            {std::lock_guard lock(mu_);duration=total_;}
            if(duration>0){
                float ratio=std::clamp((e.x-l.seek.x)/l.seek.w,0.f,1.f);
                addJob(Action::Seek,std::to_string(static_cast<int>(duration*ratio)));
            }
        } else if(l.pagePrev.contains(e.x,e.y)){
            {std::lock_guard lock(mu_);page_=std::max(0,page_-1);}
        } else if(l.pageNext.contains(e.x,e.y)){
            {std::lock_guard lock(mu_);if(static_cast<size_t>((page_+1)*6)<results_.size())++page_;}
        } else {
            bool tapped=false;
            {
                std::lock_guard lock(mu_);
                for(size_t i=0;i<l.rows.size();++i){
                    int n=page_*6+static_cast<int>(i);
                    if(n<static_cast<int>(results_.size()) && l.rows[i].contains(e.x,e.y)){
                        selected_=n;tapped=true;break;
                    }
                }
            }
            if(tapped)addJob(Action::Select);
        }
        paint();return true;
    }
    bool input(const pl::input::TextInputEvent& e){
        if(!enabled_)return false;
        bool editing=false,submit=false;std::string query;
        {
            std::lock_guard lock(mu_);
            editing=open_&&typing_;
            if(editing){
                for(char ch:e.text){
                    if(ch=='\n'||ch=='\r'){submit=true;break;}
                    if(query_.size()<160)query_+=ch;
                }
                if(submit)typing_=false;
                query=query_;
            }
        }
        if(editing){if(submit){pl::input::hideKeyboard();addJob(Action::Search,query);}paint();}
        return editing;
    }
    bool key(const pl::input::KeyEvent& e){
        if(!enabled_)return false;
        bool editing=false,submit=false;std::string query;
        {
            std::lock_guard lock(mu_);
            editing=open_&&typing_;
            if(editing&&e.isKeyDown){
                if(e.keyCode==67&&!query_.empty()){
                    query_.pop_back();
                    while(!query_.empty() && (static_cast<unsigned char>(query_.back())&0xC0u)==0x80u)query_.pop_back();
                }else if(e.keyCode==66){submit=true;typing_=false;}
                query=query_;
            }
        }
        if(editing&&e.isKeyDown){if(submit){pl::input::hideKeyboard();addJob(Action::Search,query);}paint();}
        return editing;
    }
    void paint(){
        if(!enabled_)return;
        UI l=measure(hudSize_,hudX_,hudY_,panelTop_);
        if(!l.ok())return;
        std::string title,artist,info,query,lyric,coverImage;
        std::vector<Track> tracks;
        std::vector<Lyric> timedLyrics;
        float lyricAge=100.f;
        int current,page,elapsed,total,volume;
        bool open,showHud,playing,typing;
        {
            std::lock_guard lock(mu_);
            title=title_;artist=artist_;info=info_;query=query_;lyric=lyricsOn_?lyric_:"";coverImage=coverImageId_;
            tracks=results_;timedLyrics=lyrics_;
            lyricAge=std::chrono::duration<float>(std::chrono::steady_clock::now()-lyricChanged_).count();
            current=selected_;page=page_;
            elapsed=elapsed_;total=total_;volume=volume_;
            open=open_;showHud=hud_;playing=playing_;typing=typing_;
            if(useBridge_ && playing_) {
                auto age=std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now()-bridgePositionStamp_).count();
                elapsed=static_cast<int>(std::min<int64_t>(total>0?total:INT32_MAX,
                    static_cast<int64_t>(elapsed)+std::clamp<int64_t>(age,0,1300)));
            }
        }
        std::vector<pl::modmenu::DrawCommand> out;
        out.reserve(110);
        const float u=l.s;
        const bool moving=animation_ && playing;
        const bool streamingBridge=useBridge_.load();
        const float tick=std::chrono::duration<float>(std::chrono::steady_clock::now().time_since_epoch()).count();
        auto box=[&](Rect r,uint32_t color,float radius=9.f){
            pl::modmenu::DrawCommand c;
            c.type=pl::modmenu::DrawCommandType::RectFilled;
            c.x=r.x;c.y=r.y;c.w=r.w;c.h=r.h;c.x3=radius*u;c.color=color;out.push_back(std::move(c));
        };
        auto text=[&](float x,float y,std::string v,float size,uint32_t color,size_t max=60){
            pl::modmenu::DrawCommand c;c.type=pl::modmenu::DrawCommandType::Text;
            c.x=x;c.y=y;c.size=size;c.color=color;c.text=shorten(v,max);
            out.push_back(std::move(c));
        };
        auto disc=[&](float x,float y,float radius,uint32_t color){
            pl::modmenu::DrawCommand c;c.type=pl::modmenu::DrawCommandType::CircleFilled;
            c.x=x;c.y=y;c.size=radius;c.color=color;out.push_back(std::move(c));
        };
        auto icon=[&](std::string name,float x,float y,float side){
            pl::modmenu::DrawCommand c;
            c.type=pl::modmenu::DrawCommandType::Image;
            c.x=x;c.y=y;c.w=side;c.h=side;
            c.color=0xFFFFFFFFU;
            c.imageId="levimusic.icon."+name;
            out.push_back(std::move(c));
        };
        auto outlineText=[&](float x,float y,std::string value,float size,float alpha){
            uint32_t textAlpha=static_cast<uint32_t>(std::round(255.f*std::clamp(alpha,0.f,1.f)));
            uint32_t border=(textAlpha<<24)|0x00000000U;
            uint32_t white=(textAlpha<<24)|0x00FFFFFFU;
            const float d=std::max(1.2f,size*.11f);
            const std::string visible=shorten(value,70);
            for(int dy=-1;dy<=1;dy++){
                for(int dx=-1;dx<=1;dx++){
                    if(dx==0&&dy==0)continue;
                    text(x+dx*d,y+dy*d,visible,size,border,70);
                }
            }
            text(x,y,visible,size,white,70);
        };
        auto equalizer=[&](float x,float y,float width,float height){
            for(int i=0;i<4;i++){
                float wave=moving?(.25f+.75f*std::abs(std::sin(tick*3.4f+i*1.31f))):.35f;
                float bh=height*wave;
                box({x+i*width*.25f,y+height-bh,std::max(2.f,width*.13f),bh},C_MINT,1.8f);
            }
        };
        const float miniScale=std::clamp(hudSize_.load(),55,125)/100.f;
        if(!open) {
            if(showHud){
                box({l.mini.x+2*u,l.mini.y+4*u,l.mini.w,l.mini.h},0x70000000U,15*miniScale);
                box(l.mini,0xF11D2026U,15*miniScale);
                box({l.mini.x,l.mini.y,l.mini.w,1.1f*u},0x66FFFFFFU,2.f);
                const float sz=17*u*miniScale;
                if(!coverImage.empty()) {
                    pl::modmenu::DrawCommand c;
                    c.type=pl::modmenu::DrawCommandType::Image;c.imageId=coverImage;
                    c.x=l.mini.x+8*u*miniScale;c.y=l.mini.y+8*u*miniScale;
                    c.w=34*u*miniScale;c.h=34*u*miniScale;c.color=0xFFFFFFFFU;
                    out.push_back(std::move(c));
                } else {
                    disc(l.mini.x+24*u*miniScale,l.mini.y+l.mini.h*.50f,sz,0xFF37393DU);
                    icon("disc-3",l.mini.x+13*u*miniScale,l.mini.y+(l.mini.h-23*u*miniScale)/2,23*u*miniScale);
                }
                float x=l.mini.x+48*u*miniScale;
                float fs=std::max(10.f,12.5f*u*miniScale);
                text(x,l.mini.y+l.mini.h*.40f, title,fs,C_TEXT,31);
                text(x,l.mini.y+l.mini.h*.72f, artist,fs*.77f,C_DIM,37);
                equalizer(l.mini.x+l.mini.w-19*u*miniScale,l.mini.y+l.mini.h*.24f,
                          13*u*miniScale,l.mini.h*.47f);
                if(total>0){
                    float ratio=std::clamp(float(elapsed)/total,0.f,1.f);
                    box({l.mini.x+10*u*miniScale,l.mini.y+l.mini.h-5*u*miniScale,
                         l.mini.w-20*u*miniScale,1.8f*u*miniScale},0xFF2A4056U,1.f);
                    box({l.mini.x+10*u*miniScale,l.mini.y+l.mini.h-5*u*miniScale,
                         (l.mini.w-20*u*miniScale)*ratio,1.8f*u*miniScale},C_MINT,1.f);
                }
            }
            // No background: white text + black multi-pass outline.
            // LRC lines remain until the NEXT timestamp, never a fixed 5-second cutoff.
            if(lyricsOn_ && !timedLyrics.empty() && elapsed>=0) {
                int active=-1;
                for(size_t i=0;i<timedLyrics.size();i++) {
                    if(timedLyrics[i].ms>elapsed)break;
                    active=static_cast<int>(i);
                }
                if(active>=0) {
                    const Lyric& item=timedLyrics[static_cast<size_t>(active)];
                    int end=active+1<static_cast<int>(timedLyrics.size())
                           ?timedLyrics[static_cast<size_t>(active+1)].ms
                           :(total>item.ms?total:INT32_MAX);
                    if(elapsed<end && !item.line.empty()) {
                        float fadeIn=std::clamp((elapsed-item.ms)/170.f,0.f,1.f);
                        float fadeOut=end==INT32_MAX?1.f:std::clamp((end-elapsed)/160.f,0.f,1.f);
                        float alpha=std::clamp(fadeIn*fadeOut,0.f,1.f);
                        float subtitleY=lyricsBelow_?l.mini.y+l.mini.h+26*u:l.mini.y-16*u;
                        if(subtitleY<25*u||subtitleY>l.h-12*u)
                            subtitleY=lyricsBelow_?l.mini.y-16*u:l.mini.y+l.mini.h+26*u;
                        if(subtitleY>23*u&&subtitleY<l.h-10*u)
                            outlineText(l.mini.x+3*u,subtitleY,item.line,14.4f*u,alpha);
                    }
                }
            }
            pl::modmenu::submitDrawCommands(MODULE,out);
            return;
        }

        // Upper safe zone is deliberately left free for other mods' on-screen buttons.
        box({0,0,l.w,l.h},0x92000000U,0.f);
        box({l.outer.x,l.outer.y+4*u,l.outer.w,l.outer.h},0x77000000U,18.f);
        box(l.outer,C_BG,18.f);
        box({l.outer.x+3*u,l.outer.y+3*u,l.outer.w-6*u,1.0f*u},0x33FFFFFFU,1.f);
        box({l.outer.x+14*u,l.outer.y+12*u,31*u,31*u},0xFF35363AU,9.f);
        icon("audio-lines",l.outer.x+20*u,l.outer.y+18*u,19*u);
        text(l.outer.x+57*u,l.outer.y+33*u,"LEVI MUSIC",19*u,C_TEXT);
        text(l.outer.x+199*u,l.outer.y+31*u,"REAAURELIS • v1.0.1",10.0f*u,C_DIM);
        box(l.settings,0xFF32343AU,9.f);
        icon("settings-2",l.settings.x+6*u,l.settings.y+6*u,18*u);
        box(l.close,0xFF32343AU,9.f);
        icon("x",l.close.x+6*u,l.close.y+6*u,18*u);
        box(l.left,C_PANEL,12.f);
        box(l.right,C_PANEL,12.f);

        // Left: focused now-playing card. Small art leaves room for lyrics, seek and controls.
        const float artX=l.left.x+61*u;
        const float artY=l.left.y+68*u;
        if(!coverImage.empty()) {
            pl::modmenu::DrawCommand c;
            c.type=pl::modmenu::DrawCommandType::Image;c.imageId=coverImage;
            c.x=artX-43*u;c.y=artY-43*u;c.w=86*u;c.h=86*u;c.color=0xFFFFFFFFU;
            out.push_back(std::move(c));
        } else {
            disc(artX,artY,46*u,0xFF37393DU);
            disc(artX,artY,38*u,0xFF1E2024U);
            disc(artX,artY,27*u,0xFF4C4F55U);
            disc(artX,artY,10*u,C_MINT);
        }
        const float ix=l.left.x+119*u;
        text(ix,l.left.y+37*u,"NOW PLAYING",10.5f*u,C_MINT,35);
        text(ix,l.left.y+65*u,title,16*u,C_TEXT,41);
        text(ix,l.left.y+87*u,artist,12*u,C_DIM,43);
        // Compact status, no potentially huge text block overlapping seek.
        text(l.left.x+17*u,l.left.y+134*u,info,11*u,C_DIM,62);

        const float lyricTop=l.left.y+154*u;
        const float lyricHeight=std::max(22.f,l.seek.y-lyricTop-14*u);
        if(lyricHeight>24.f) {
            box({l.left.x+15*u,lyricTop,l.left.w-30*u,lyricHeight},0xA9112232U,8.f);
            box({l.left.x+15*u,lyricTop,3*u,lyricHeight},C_MINT,2.f);
            text(l.left.x+29*u,lyricTop+std::min(26*u,lyricHeight*.65f),
                 lyric.empty()?"Lyrics appear here when available":lyric,
                 11.5f*u,C_DIM,57);
        }
        const float prog=total>0?std::clamp(float(elapsed)/total,0.f,1.f):0.f;
        const float lineY=l.seek.y+l.seek.h*.5f;
        box({l.seek.x,lineY-1.35f*u,l.seek.w,2.7f*u},0xFF334758U,2.f);
        if(total>0)box({l.seek.x,lineY-1.35f*u,std::max(.5f,l.seek.w*prog),2.7f*u},C_MINT,2.f);
        if(total>0)disc(l.seek.x+l.seek.w*prog,lineY,4*u,C_MINT);
        text(l.seek.x,l.seek.y+l.seek.h+9*u,clockText(elapsed),10*u,C_DIM);
        text(l.seek.x+l.seek.w-42*u,l.seek.y+l.seek.h+9*u,clockText(total),10*u,C_DIM);
        box(l.stop,0xFF294052U,10.f);
        icon("square",l.stop.x+9*u,l.stop.y+13*u,18*u);
        box(l.prev,0xFF294052U,10.f);
        icon("skip-back",l.prev.x+12*u,l.prev.y+12*u,22*u);
        box(l.play,0xFF575B64U,13.f);
        box({l.play.x+2*u,l.play.y+2*u,l.play.w-4*u,l.play.h-4*u},0xFF696E77U,12.f);
        icon(playing?"pause":"play",l.play.x+(l.play.w-23*u)*.5f,l.play.y+10*u,23*u);
        box(l.next,0xFF294052U,10.f);
        icon("skip-forward",l.next.x+12*u,l.next.y+12*u,22*u);
        icon("volume-2",l.left.x+16*u,l.left.y+l.left.h-14*u,12*u);
        text(l.left.x+34*u,l.left.y+l.left.h-4*u,
             streamingBridge?("Spotify / YouTube Music • "+std::to_string(volume)+"%"):("Audius • "+std::to_string(volume)+"%"),10*u,C_DIM);

        if(streamingBridge) {
            // Search is routed to the standalone Android engine over the local authenticated Bridge.
            box(l.searchBox,0xFF25272CU,9.f);
            text(l.searchBox.x+11*u,l.searchBox.y+22*u,
                 (typing?"_ ":"")+query,12.2f*u,typing?C_TEXT:C_DIM,46);
            box(l.searchGo,0xFF555860U,10.f);
            icon("search",l.searchGo.x+13*u,l.searchGo.y+8*u,20*u);
            const bool showingSearch=!tracks.empty();
            icon(showingSearch?"list-music":"audio-lines",l.right.x+14*u,l.right.y+54*u,16*u);
            text(l.right.x+38*u,l.right.y+66*u,showingSearch?"SEARCH RESULTS":"LIVE LYRICS",11*u,C_TEXT,34);
            if(showingSearch){
                text(l.right.x+l.right.w-90*u,l.right.y+65*u,
                     std::to_string(tracks.size())+" songs",10*u,C_DIM,22);
                for(size_t i=0;i<l.rows.size();i++) {
                    size_t n=static_cast<size_t>(page*6)+i;
                    if(n>=tracks.size())break;
                    Rect r=l.rows[i];
                    const bool chosen=static_cast<int>(n)==current;
                    box(r,chosen?0xFF42454CU:0xFF292B31U,8.f);
                    text(r.x+11*u,r.y+std::min(20*u,r.h*.45f),
                         std::to_string(n+1)+".  "+tracks[n].title,12*u,C_TEXT,47);
                    text(r.x+11*u,r.y+std::min(38*u,r.h*.83f),
                         tracks[n].artist,10*u,C_DIM,55);
                    if(chosen)icon("play",r.x+r.w-25*u,r.y+7*u,16*u);
                }
                box(l.pagePrev,0xFF41434AU,8.f);
                icon("chevron-left",l.pagePrev.x+9*u,l.pagePrev.y+7*u,19*u);
                box(l.pageNext,0xFF41434AU,8.f);
                icon("chevron-right",l.pageNext.x+9*u,l.pageNext.y+7*u,19*u);
                text(l.right.x+14*u,l.right.y+l.right.h-16*u,
                     "Page "+std::to_string(page+1)+" / "+std::to_string(std::max<size_t>(1,(tracks.size()+5)/6)),
                     11*u,C_DIM);
            } else {
                const int available=static_cast<int>(timedLyrics.size());
                int active=-1;
                for(int i=0;i<available;i++){
                    if(timedLyrics[static_cast<size_t>(i)].ms>elapsed)break;
                    active=i;
                }
                const int start=std::max(0,active-1);
                const int maxRows=std::min(6,std::max(0,available-start));
                if(available==0){
                    text(l.right.x+19*u,l.right.y+128*u,
                         "Search music above, or play in the app.",12*u,C_DIM,70);
                    text(l.right.x+19*u,l.right.y+156*u,
                         "Timed lyrics appear here when found.",11*u,C_DIM,70);
                } else {
                    for(int i=0;i<maxRows;i++){
                        int j=start+i;
                        float y=l.right.y+112*u+i*35*u;
                        if(j==active)box({l.right.x+13*u,y-18*u,l.right.w-26*u,29*u},
                                         0xFF3C3E45U,8.f);
                        text(l.right.x+21*u,y,timedLyrics[static_cast<size_t>(j)].line,
                             j==active?13*u:11.5f*u,j==active?C_TEXT:C_DIM,74);
                    }
                }
                text(l.right.x+15*u,l.right.y+l.right.h-14*u,
                     "Clear search to show lyrics",10*u,C_DIM,60);
            }
        } else {
        // Right: search bar, readable active track, paginate 20 results.
        box(l.searchBox,0xFF111E30U,9.f);
        text(l.searchBox.x+12*u,l.searchBox.y+23*u,
             (typing?"_ ":"")+query,12.5f*u,typing?C_MINT:C_TEXT,44);
        box(l.searchGo,0xFF585B63U,10.f);
        icon("search",l.searchGo.x+14*u,l.searchGo.y+8*u,20*u);
        icon("list-music",l.right.x+13*u,l.right.y+52*u,14*u);
        text(l.right.x+36*u,l.right.y+64*u,"SEARCH RESULTS",10.5f*u,C_DIM,25);
        text(l.right.x+l.right.w-73*u,l.right.y+64*u,
             std::to_string(tracks.size())+" songs",10*u,C_MINT,17);
        for(size_t i=0;i<l.rows.size();i++) {
            size_t n=static_cast<size_t>(page*6)+i;
            if(n>=tracks.size())break;
            const Rect r=l.rows[i];
            const bool currentSong=static_cast<int>(n)==current;
            box(r,currentSong?0xFF2A5862U:((n%2==0)?0xFF223448U:0xFF26384BU),8.f);
            if(currentSong)box({r.x,r.y,3*u,r.h},C_MINT,1.f);
            text(r.x+12*u,r.y+std::min(20*u,r.h*.45f),
                 std::to_string(n+1)+".  "+tracks[n].title,12*u,C_TEXT,46);
            text(r.x+12*u,r.y+std::min(38*u,r.h*.84f),
                 tracks[n].artist,10*u,C_DIM,52);
            if(currentSong)icon(playing?"audio-lines":"play",
                               r.x+r.w-29*u,r.y+(r.h-17*u)*.5f,17*u);
        }
        box(l.pagePrev,0xFF2B4256U,8.f);
        icon("chevron-left",l.pagePrev.x+9*u,l.pagePrev.y+7*u,19*u);
        box(l.pageNext,0xFF2B4256U,8.f);
        icon("chevron-right",l.pageNext.x+9*u,l.pageNext.y+7*u,19*u);
        text(l.right.x+14*u,l.right.y+l.right.h-18*u,
             "Page "+std::to_string(page+1)+" / "+std::to_string(std::max<size_t>(1,(tracks.size()+5)/6)),
             11*u,C_DIM);
        }
        pl::modmenu::submitDrawCommands(MODULE,out);
    }
    void process(){
        EnvScope scope(vm_);
        if(!scope){update("LeviMusic","Android","Java VM unavailable");return;}
        JNIEnv* env=scope.env;
        AndroidPlayer player;
        auto playSelected=[this,&player,env](){
            Track tr;int vol;std::string key;
            {
                std::lock_guard lock(mu_);
                if(selected_<0||selected_>=static_cast<int>(results_.size()))return;
                tr=results_[static_cast<size_t>(selected_)];
                vol=volume_;key=apiKey_;playing_=false;elapsed_=0;total_=0;
                lyrics_.clear();lyric_.clear();
            }
            update(tr.title,tr.artist,"Opening Audius stream...");
            std::string base=std::string(API)+"/tracks/"+encode(env,tr.id)+"/stream?app_name="+APP_NAME;
            if(!key.empty())base+="&api_key="+encode(env,key);
            bool ok=player.startUrl(env,base,vol);
            if(!ok){
                update(tr.title,tr.artist,"Retrying backup Audius discovery node...");
                std::string mirror="https://discoveryprovider.audius.co/v1/tracks/"+encode(env,tr.id)
                      +"/stream?app_name="+APP_NAME;
                if(!key.empty())mirror+="&api_key="+encode(env,key);
                ok=player.startUrl(env,mirror,vol);
            }
            if(!ok){update(tr.title,tr.artist,"Stream error - try another song or API key");return;}
            {
                std::lock_guard lock(mu_);
                playing_=true;total_=player.duration(env);
            }
            update(tr.title,tr.artist,"Streaming online (Audius)");
            std::vector<Lyric> found=searchLyrics(env,tr,player.duration(env));
            {
                std::lock_guard lock(mu_);
                lyrics_=std::move(found);
                lyric_=lyrics_.empty()?"No synchronized lyrics":"...";
            }
        };
        auto lastPaint=std::chrono::steady_clock::now();
        auto lastBridge=std::chrono::steady_clock::now()-std::chrono::seconds(2);
        std::string previousTitle, previousArtist, previousProvider,previousArt;
        bool lastBridgeConnected=false;
        while(true){
            Job job{};bool hasJob=false;
            {
                std::unique_lock lock(mu_);
                cv_.wait_for(lock,std::chrono::milliseconds(100),
                    [this]{return shutdown_||!jobs_.empty();});
                if(shutdown_)break;
                if(!jobs_.empty()){
                    job=std::move(jobs_.front());
                    jobs_.pop_front();hasJob=true;
                }
            }
            if(useBridge_) {
                std::string key;
                {std::lock_guard lock(mu_);key=bridgeKey_;}
                if(hasJob) {
                    std::string action;
                    switch(job.action) {
                        case Action::PlayPause: {
                            bool currentlyPlaying;
                            {std::lock_guard lock(mu_);currentlyPlaying=playing_;}
                            action=currentlyPlaying?"pause":"play";
                            break;
                        }
                        case Action::Next: action="next";break;
                        case Action::Previous: action="previous";break;
                        case Action::Seek: action="seek&position="+job.query;break;
                        case Action::Search: {
                            if(job.query.empty()){
                                std::lock_guard lock(mu_);
                                results_.clear();selected_=-1;page_=0;
                            } else if(!key.empty()){
                                std::string query=job.query.substr(0,70);
                                update("Searching",job.query,"Searching via LeviMusic companion...");
                                std::string reply=bridgeGet("/v1/search?query="+encode(env,query),key);
                                auto found=parseBridgeSearch(env,reply);
                                bool foundAny=!found.empty();
                                if(!foundAny){
                                    if(reply.empty())update("Search unavailable",job.query,"Bridge offline / request timed out");
                                    else update("No search results",job.query,"Try another title, check connectivity");
                                } else update("Search results",job.query,std::to_string(found.size())+" songs ready to play");
                                {
                                    std::lock_guard lock(mu_);
                                    results_=std::move(found);
                                    selected_=-1;
                                    page_=0;
                                }
                            } else {
                                update("Bridge Key missing","Open LeviMusic settings","Paste the pairing key from the APK");
                            }
                            break;
                        }
                        case Action::Stop: action="pause";break;
                        case Action::Select: {
                            std::string selectedId;
                            {
                                std::lock_guard lock(mu_);
                                if(selected_>=0 && selected_<static_cast<int>(results_.size()))
                                    selectedId=results_[static_cast<size_t>(selected_)].id;
                            }
                            if(!selectedId.empty()&&!key.empty()){
                                std::string reply=bridgeGet("/v1/play?id="+encode(env,selectedId),key);
                                auto answer=makeJson(env,reply);
                                bool accepted=answer && optString(env,answer,"accepted")=="true";
                                if(answer)env->DeleteLocalRef(answer);
                                if(!accepted)update("Play request failed","LeviMusic Bridge",
                                    "Open the companion app and retry the track");
                            }
                            break;
                        }
                        case Action::SetVolume: break;
                    }
                    if(!action.empty() && !key.empty()) {
                        auto answer=bridgeGet("/v1/control?action="+action,key);
                        if(answer.empty()) {
                            __android_log_print(ANDROID_LOG_WARN,TAG,"Companion media command failed: %s",action.c_str());
                            update("Bridge command failed",action,"Check APK running / pairing key");
                        }
                    }
                }
                auto now=std::chrono::steady_clock::now();
                if(now-lastBridge>std::chrono::milliseconds(700)) {
                    lastBridge=now;
                    BridgeState state;
                    if(!key.empty())state=decodeBridge(env,bridgeGet("/v1/status",key));
                    if(!state.connected) {
                        if(lastBridgeConnected || previousTitle!="") {
                            previousTitle.clear();previousArtist.clear();previousProvider.clear();previousArt.clear();previousArt.clear();
                        }
                        lastBridgeConnected=false;
                        {
                            std::lock_guard lock(mu_);
                            title_="LeviMusic";
                            artist_="Spotify / YouTube Music";
                            info_=key.empty()?"Paste Bridge Key in LeviMusic settings":
                                  "Start LeviMusic Bridge; grant Notification Access";
                            playing_=false;elapsed_=0;total_=0;lyric_.clear();lyrics_.clear();coverImageId_.clear();coverImageId_.clear();
                        }
                    } else if(!state.available) {
                        lastBridgeConnected=true;
                        previousTitle.clear();previousArtist.clear();previousProvider.clear();
                        {
                            std::lock_guard lock(mu_);
                            title_="Choose a song";
                            artist_="Spotify / YouTube Music";
                            info_="Start music in the official app";
                            playing_=false;elapsed_=0;total_=0;lyric_.clear();lyrics_.clear();
                        }
                    } else {
                        lastBridgeConnected=true;
                        const bool changed=state.title!=previousTitle ||
                                          state.artist!=previousArtist || state.provider!=previousProvider;
                        if(changed) {
                            previousTitle=state.title;previousArtist=state.artist;previousProvider=state.provider;
                            {
                                std::lock_guard lock(mu_);
                                lyrics_.clear();lyric_.clear();
                            }
                        }
                        {
                            std::lock_guard lock(mu_);
                            title_=state.title.empty()?"Unknown track":state.title;
                            artist_=state.artist.empty()?state.provider:state.artist;
                            info_=state.provider+" • Music session connected";
                            playing_=state.playing;elapsed_=state.elapsed;total_=state.duration;
                            bridgePositionStamp_=std::chrono::steady_clock::now();
                        }
                        if(state.art!=previousArt || changed){
                            previousArt=state.art;
                            bool good=false;std::string imageId;
                            if(!state.art.empty() && coverCount_<80){
                                imageId="levimusic.cover."+std::to_string(++coverCount_);
                                good=registerCover(env,state.art,imageId);
                            }
                            {
                                std::lock_guard lock(mu_);
                                coverImageId_=good?imageId:"";
                                coverUrl_=state.art;
                            }
                        }
                        if(changed && !state.title.empty()) {
                            Track track;
                            track.title=state.title;track.artist=state.artist;
                            auto found=searchLyrics(env,track,state.duration);
                            {
                                std::lock_guard lock(mu_);
                                lyrics_=std::move(found);
                                lyric_=lyrics_.empty()?"":"";
                                lyricChanged_=std::chrono::steady_clock::now();
                            }
                        }
                        std::string current;
                        {
                            std::lock_guard lock(mu_);
                            for(const auto& line:lyrics_) {
                                if(line.ms>state.elapsed)break;
                                current=line.line;
                            }
                            if(current!=lyric_) {
                                lyric_=std::move(current);
                                lyricChanged_=std::chrono::steady_clock::now();
                            }
                        }
                    }
                }
            } else {
            if(hasJob){
                switch(job.action){
                    case Action::Search:{
                        if(job.query.empty()){update("Search","Empty query","Tap search and enter a title");break;}
                        update("Searching",job.query,"Contacting Audius...");
                        std::string key;
                        {std::lock_guard lock(mu_);key=apiKey_;}
                        auto found=searchAudiusFrom(env,job.query,API,key);
                        if(found.empty())
                            found=searchAudiusFrom(env,job.query,"https://discoveryprovider.audius.co/v1",key);
                        {
                            std::lock_guard lock(mu_);
                            results_=std::move(found);
                            selected_=results_.empty()?-1:0;
                            page_=0;
                        }
                        if(selected_>=0)playSelected();
                        else update("No results",job.query,"Please check internet/API key");
                        break;
                    }
                    case Action::Select:playSelected();break;
                    case Action::PlayPause:
                        if(player.isPlaying(env)){
                            player.toggle(env);{std::lock_guard lock(mu_);playing_=false;}
                        }else if(player.position(env)>0){
                            player.toggle(env);{std::lock_guard lock(mu_);playing_=true;}
                        }else playSelected();
                        break;
                    case Action::Previous:
                    case Action::Next:{
                        {
                            std::lock_guard lock(mu_);
                            if(!results_.empty()){
                                int n=static_cast<int>(results_.size());
                                selected_=(selected_+(job.action==Action::Next?1:n-1))%n;
                                page_=selected_/6;
                            }
                        }
                        playSelected();break;
                    }
                    case Action::Stop:
                        player.release(env);
                        {std::lock_guard lock(mu_);
                            playing_=false;elapsed_=0;total_=0;lyrics_.clear();lyric_.clear();
                        }
                        update("LeviMusic","Stopped","Choose a song to play");break;
                    case Action::SetVolume:{
                        int v;{std::lock_guard lock(mu_);v=volume_;}
                        player.setVolume(env,v);break;
                    }
                    case Action::Seek:
                        try{player.seek(env,std::stoi(job.query));}catch(...){}break;
                }
            }
            bool playing=player.isPlaying(env);
            int pos=player.position(env),dur=player.duration(env);
            {
                std::lock_guard lock(mu_);
                playing_=playing;elapsed_=pos;
                if(dur>0)total_=dur;
                if(playing&&!lyrics_.empty()){
                    for(const auto& line:lyrics_){
                        if(line.ms>pos)break;
                        if(lyric_!=line.line) {
                            lyric_=line.line;
                            lyricChanged_=std::chrono::steady_clock::now();
                        }
                    }
                }
            }
            } // Audius legacy mode
            const auto now=std::chrono::steady_clock::now();
            if(now-lastPaint>std::chrono::milliseconds(animation_?110:400)){
                paint();lastPaint=now;
            }
        }
        player.release(env);
    }
};
} // namespace
PL_REGISTER_MOD(LeviMusic, LeviMusic::instance())
