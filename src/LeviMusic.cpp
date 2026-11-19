// LeviMusic for LeviLauncher Android, v1.0.1 RC
// Music discovery / playback: Audius public API (not Spotify/YouTube scraping).
// Lyrics: optional LRCLIB synchronized lyrics, only when available.
#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>
#include <pl/Input.hpp>
#include "LucideIcons.hpp"
#include "WorldLyrics3D.hpp"
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
#include <cctype>
#include <utility>
#include <vector>
#include <fstream>
#include <filesystem>

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

enum class Action { Search, PlayPause, Next, Previous, Stop, SetVolume, Seek, Select, Pair, PlaylistList, PlaylistOpen, PlaylistPlay, PlaylistCreate, PlaylistAdd, OpenBrowser, QueueRefresh, QueuePlay, QueueRemove, QueueAdd };
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


#if defined(LEVI_STANDALONE)
// ChruPipe is a self-hosted Kotlin/Ktor+NewPipeExtractor server, NOT a native
// library. This lightweight standalone client talks directly to its public
// /search and /stream JSON endpoints. No LeviMusic Bridge APK or Audius is used.
// Loopback HTTP uses a POSIX socket because Minecraft's Android network security
// policy may reject cleartext Java URLConnection. Remote servers must use HTTPS.
std::string decodeChunked(std::string_view data) {
    std::string decoded;
    size_t cursor=0;
    while(cursor<data.size() && decoded.size()<2*1024*1024) {
        size_t nl=data.find("\r\n",cursor);
        if(nl==std::string_view::npos || nl-cursor>24)return {};
        std::string lengthToken(data.substr(cursor,nl-cursor));
        auto semicolon=lengthToken.find(';');
        if(semicolon!=std::string::npos)lengthToken.resize(semicolon);
        size_t n=0;
        try{n=std::stoul(lengthToken,nullptr,16);}catch(...){return {};}
        cursor=nl+2;
        if(n==0)return decoded;
        if(n>2*1024*1024-decoded.size()||n>data.size()-cursor)return {};
        decoded.append(data.substr(cursor,n));
        cursor+=n;
        if(cursor+2>data.size() || data.substr(cursor,2)!="\r\n")return {};
        cursor+=2;
    }
    return {};
}
std::string chruLoopbackGet(const std::string& url) {
    const std::string prefix="http://";
    if(url.rfind(prefix,0)!=0)return {};
    size_t hostBegin=prefix.size(), slash=url.find('/',hostBegin);
    if(slash==std::string::npos)return {};
    std::string authority=url.substr(hostBegin,slash-hostBegin);
    std::string host=authority,portString="8080";
    auto colon=authority.rfind(':');
    if(colon!=std::string::npos) {
        host=authority.substr(0,colon);
        portString=authority.substr(colon+1);
    }
    if(host!="127.0.0.1"&&host!="localhost")return {};
    int port=0;
    try{port=std::stoi(portString);}catch(...){return {};}
    if(port<1||port>65535)return {};
    int fd=::socket(AF_INET,SOCK_STREAM,0);
    if(fd<0)return {};
    timeval timeout{};timeout.tv_sec=5;
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    sockaddr_in address{};address.sin_family=AF_INET;
    address.sin_port=htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET,"127.0.0.1",&address.sin_addr);
    std::string response;
    if(::connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0) {
        std::string req="GET "+url.substr(slash)+" HTTP/1.1\r\nHost: localhost\r\n"
            "User-Agent: LeviMusic/1.2.1\r\nAccept: application/json\r\n"
            "Accept-Encoding: identity\r\nConnection: close\r\n\r\n";
        if(::send(fd,req.data(),req.size(),MSG_NOSIGNAL)==static_cast<ssize_t>(req.size())){
            char buffer[8192];
            while(response.size()<2*1024*1024) {
                ssize_t read=::recv(fd,buffer,sizeof(buffer),0);
                if(read<=0)break;
                response.append(buffer,static_cast<size_t>(read));
            }
        }
    }
    ::close(fd);
    if(response.rfind("HTTP/1.1 200 ",0)!=0 && response.rfind("HTTP/1.0 200 ",0)!=0)
        return {};
    size_t headerEnd=response.find("\r\n\r\n");
    if(headerEnd==std::string::npos||headerEnd>32768)return {};
    std::string headers=response.substr(0,headerEnd);
    std::string data=response.substr(headerEnd+4);
    std::transform(headers.begin(),headers.end(),headers.begin(),
                   [](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(headers.find("transfer-encoding: chunked")!=std::string::npos)
        return decodeChunked(data);
    return data;
}
std::string chruRequest(JNIEnv* env,const std::string& server,const std::string& path) {
    std::string root=server;
    while(!root.empty()&&root.back()=='/')root.pop_back();
    if(root.size()>350||path.size()>2200)return {};
    if(root.rfind("https://",0)==0)return getHttp(env,root+path);
    if(root.rfind("http://127.0.0.1:",0)==0||
       root=="http://127.0.0.1"||
       root.rfind("http://localhost:",0)==0||
       root=="http://localhost")return chruLoopbackGet(root+path);
    return {}; // No cleartext remote URLs or SSRF to LAN hosts.
}
std::vector<Track> searchChruPipe(JNIEnv* env,const std::string& server,
                                  const std::string& query) {
    std::vector<Track> tracks;
    if(query.empty())return tracks;
    auto body=chruRequest(env,server,"/search?q="+encode(env,query)+"&service=youtube");
    jobject root=makeJson(env,body);
    if(!root)return tracks;
    jobject array=optArray(env,root,"items");
    if(array) {
        jclass cls=env->FindClass("org/json/JSONArray");
        jmethodID length=cls?env->GetMethodID(cls,"length","()I"):nullptr;
        jmethodID item=cls?env->GetMethodID(cls,"optJSONObject","(I)Lorg/json/JSONObject;"):nullptr;
        int n=length?env->CallIntMethod(array,length):0;
        if(clearException(env,"ChruPipe search length"))n=0;
        for(int i=0;i<std::min(n,24);i++){
            jobject obj=item?env->CallObjectMethod(array,item,i):nullptr;
            if(!obj||clearException(env,"ChruPipe search item"))continue;
            if(optString(env,obj,"isLive")=="true"){env->DeleteLocalRef(obj);continue;}
            Track tr;
            tr.id=optString(env,obj,"url");
            tr.title=optString(env,obj,"title");
            tr.artist=optString(env,obj,"uploader");
            tr.art=optString(env,obj,"thumbnailUrl");
            if(tr.id.rfind("https://",0)==0&&!tr.title.empty())
                tracks.push_back(std::move(tr));
            env->DeleteLocalRef(obj);
        }
        if(cls)env->DeleteLocalRef(cls);
        env->DeleteLocalRef(array);
    }
    env->DeleteLocalRef(root);
    return tracks;
}
std::string streamChruPipe(JNIEnv* env,const std::string& server,
                            const std::string& contentUrl) {
    auto body=chruRequest(env,server,"/stream?url="+encode(env,contentUrl));
    jobject root=makeJson(env,body);
    if(!root)return {};
    std::string selected;
    jobject array=optArray(env,root,"audioStreams");
    if(array) {
        jclass cls=env->FindClass("org/json/JSONArray");
        jmethodID length=cls?env->GetMethodID(cls,"length","()I"):nullptr;
        jmethodID item=cls?env->GetMethodID(cls,"optJSONObject","(I)Lorg/json/JSONObject;"):nullptr;
        int n=length?env->CallIntMethod(array,length):0;
        if(clearException(env,"ChruPipe stream length"))n=0;
        for(int i=0;i<std::min(n,30);++i){
            jobject obj=item?env->CallObjectMethod(array,item,i):nullptr;
            if(!obj||clearException(env,"ChruPipe audio stream"))continue;
            auto url=optString(env,obj,"url");
            auto format=optString(env,obj,"format");
            if(url.rfind("https://",0)==0) {
                if(selected.empty())selected=url;
                // Prefer broadly compatible M4A/AAC over Opus/WebM.
                if(format.find("M4A")!=std::string::npos ||
                   format.find("AAC")!=std::string::npos)selected=url;
            }
            env->DeleteLocalRef(obj);
        }
        if(cls)env->DeleteLocalRef(cls);
        env->DeleteLocalRef(array);
    }
    env->DeleteLocalRef(root);
    return selected;
}
#endif

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

std::vector<Lyric> searchLyricsRaw(JNIEnv* env, const std::string& songTitle, const std::string& songArtist,
                                  int trackDurationMs,bool broad=false) {
    const std::string url = broad?
             ("https://lrclib.net/api/search?q="+encode(env,songTitle+" "+songArtist)):
             ("https://lrclib.net/api/search?track_name="+encode(env,songTitle)+
                (songArtist.empty()?"":"&artist_name="+encode(env,songArtist)));
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
            if(optString(env,obj,"instrumental")=="true"){env->DeleteLocalRef(obj);continue;}
            const std::string actualTitle=optString(env,obj,"trackName");
            const std::string actualArtist=optString(env,obj,"artistName");
            auto lower=[](std::string str){
                for(char& c:str)if(c>='A'&&c<='Z')c=static_cast<char>(c+32);
                return str;
            };
            int score=0;
            if(!actualTitle.empty()){
                const auto a=lower(actualTitle),b=lower(songTitle);
                score += a==b?30000:(a.find(b)!=std::string::npos ||
                                      b.find(a)!=std::string::npos?9000:-13000);
            }
            if(!songArtist.empty()&&!actualArtist.empty()){
                const auto a=lower(actualArtist),b=lower(songArtist);
                score += a==b?18000:(a.find(b)!=std::string::npos ||
                                      b.find(a)!=std::string::npos?5500:-8000);
            }
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

std::string bridgeGet(const std::string& path,const std::string& token);

std::vector<Lyric> searchLyrics(JNIEnv* env,const Track& tr,int duration=0,
                                const std::string& bridgeKey={}) {
    if(tr.title.empty())return {};
    // First try a user-imported LRC for the exact currently playing track.
    if(!bridgeKey.empty()) {
        std::string response=bridgeGet("/v1/lyrics?title="+encode(env,tr.title)+
             "&artist="+encode(env,tr.artist),bridgeKey);
        if(!response.empty()) {
            jobject root=makeJson(env,response);
            if(root){
                std::string raw=optString(env,root,"syncedLyrics");
                env->DeleteLocalRef(root);
                auto local=parseLrc(raw);
                if(!local.empty())return local;
            }
        }
    }
    // Then use LRCLIB's exact-match endpoint when title, artist and duration are known.
    if(duration>0&&!tr.artist.empty()) {
        const std::string url="https://lrclib.net/api/get?track_name="+encode(env,tr.title)+
             "&artist_name="+encode(env,tr.artist)+"&album_name=&duration="+
             std::to_string(std::max(1,(duration+500)/1000));
        const std::string response=getHttp(env,url);
        if(!response.empty()) {
            jobject root=makeJson(env,response);
            if(root){
                auto result=parseLrc(optString(env,root,"syncedLyrics"));
                env->DeleteLocalRef(root);
                if(!result.empty())return result;
            }
        }
    }
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
    auto byTitle=searchLyricsRaw(env,cleaned,"",duration);
    if(!byTitle.empty())return byTitle;
    return searchLyricsRaw(env,cleaned,tr.artist,duration,true);
}

std::string romanizeLine(JNIEnv* env,const std::string& line){
    bool nonLatin=false;
    for(unsigned char c:line)if(c>=128){nonLatin=true;break;}
    if(!nonLatin)return line;
    jclass cls=env->FindClass("android/icu/text/Transliterator");
    if(!cls||env->ExceptionCheck()){env->ExceptionClear();return line;}
    jmethodID get=env->GetStaticMethodID(cls,"getInstance",
        "(Ljava/lang/String;)Landroid/icu/text/Transliterator;");
    jmethodID transliterate=env->GetMethodID(cls,"transliterate",
        "(Ljava/lang/String;)Ljava/lang/String;");
    if(!get||!transliterate||env->ExceptionCheck()){
        env->ExceptionClear();env->DeleteLocalRef(cls);return line;
    }
    jstring rule=env->NewStringUTF("Any-Latin; Latin-ASCII");
    jobject object=env->CallStaticObjectMethod(cls,get,rule);
    std::string result=line;
    if(object&&!env->ExceptionCheck()){
        jstring input=toJava(env,line);
        jstring output=(jstring)env->CallObjectMethod(object,transliterate,input);
        if(output&&!env->ExceptionCheck()){
            const char* chars=env->GetStringUTFChars(output,nullptr);
            if(chars){result=chars;env->ReleaseStringUTFChars(output,chars);}
        }
        if(env->ExceptionCheck())env->ExceptionClear();
        if(output)env->DeleteLocalRef(output);
        env->DeleteLocalRef(input);env->DeleteLocalRef(object);
    }
    if(env->ExceptionCheck())env->ExceptionClear();
    env->DeleteLocalRef(rule);env->DeleteLocalRef(cls);
    return result;
}
void romanizeLyrics(JNIEnv* env,std::vector<Lyric>& lines){
    for(auto& lyric:lines)lyric.line=romanizeLine(env,lyric.line);
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

std::vector<std::string> parsePlaylistNames(JNIEnv* env,const std::string& body) {
    std::vector<std::string> names;
    jobject root=makeJson(env,body);
    if(!root)return names;
    jobject array=optArray(env,root,"playlists");
    if(array) {
        jclass cls=env->FindClass("org/json/JSONArray");
        jmethodID count=cls?env->GetMethodID(cls,"length","()I"):nullptr;
        jmethodID item=cls?env->GetMethodID(cls,"optJSONObject","(I)Lorg/json/JSONObject;"):nullptr;
        int size=count?env->CallIntMethod(array,count):0;
        for(int i=0;i<std::min(40,size);i++){
            jobject obj=item?env->CallObjectMethod(array,item,i):nullptr;
            if(obj){std::string name=optString(env,obj,"name");
                if(!name.empty())names.push_back(std::move(name));
                env->DeleteLocalRef(obj);
            }
        }
        if(cls)env->DeleteLocalRef(cls);
        env->DeleteLocalRef(array);
    }
    clearException(env,"playlist JSON");
    env->DeleteLocalRef(root);
    return names;
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

bool validToken(const std::string& v) {
    if(v.size()!=48)return false;
    for(char c:v)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
    return true;
}
std::string clipboardToken(JNIEnv* env) {
    jclass thread=env->FindClass("android/app/ActivityThread");
    if(!thread||clearException(env,"clipboard thread"))return {};
    jmethodID method=env->GetStaticMethodID(thread,"currentApplication","()Landroid/app/Application;");
    jobject app=method?env->CallStaticObjectMethod(thread,method):nullptr;
    if(!app||clearException(env,"clipboard application")){env->DeleteLocalRef(thread);return {};}
    jclass context=env->GetObjectClass(app);
    jmethodID getService=env->GetMethodID(context,"getSystemService","(Ljava/lang/String;)Ljava/lang/Object;");
    jstring name=env->NewStringUTF("clipboard");
    jobject service=getService?env->CallObjectMethod(app,getService,name):nullptr;
    if(clearException(env,"clipboard service"))service=nullptr;
    std::string token;
    if(service) {
        jclass manager=env->GetObjectClass(service);
        jmethodID getClip=env->GetMethodID(manager,"getPrimaryClip","()Landroid/content/ClipData;");
        jobject clip=getClip?env->CallObjectMethod(service,getClip):nullptr;
        if(clip&&!clearException(env,"clipboard clip")) {
            jclass cl=env->GetObjectClass(clip);
            jmethodID count=env->GetMethodID(cl,"getItemCount","()I");
            jmethodID item=env->GetMethodID(cl,"getItemAt","(I)Landroid/content/ClipData$Item;");
            if(count&&item&&env->CallIntMethod(clip,count)>0) {
                jobject one=env->CallObjectMethod(clip,item,0);
                if(one) {
                    jclass itemCl=env->GetObjectClass(one);
                    jmethodID getText=env->GetMethodID(itemCl,"getText","()Ljava/lang/CharSequence;");
                    jobject chars=getText?env->CallObjectMethod(one,getText):nullptr;
                    if(chars) {
                        jclass charCl=env->GetObjectClass(chars);
                        jmethodID toString=env->GetMethodID(charCl,"toString","()Ljava/lang/String;");
                        jstring str=toString?(jstring)env->CallObjectMethod(chars,toString):nullptr;
                        token=fromJava(env,str);
                        if(str)env->DeleteLocalRef(str);
                        env->DeleteLocalRef(charCl);env->DeleteLocalRef(chars);
                    }
                    env->DeleteLocalRef(itemCl);env->DeleteLocalRef(one);
                }
            }
            env->DeleteLocalRef(cl);env->DeleteLocalRef(clip);
        }
        env->DeleteLocalRef(manager);env->DeleteLocalRef(service);
    }
    if(name)env->DeleteLocalRef(name);
    env->DeleteLocalRef(context);env->DeleteLocalRef(app);env->DeleteLocalRef(thread);
    clearException(env,"clipboard cleanup");
    return validToken(token)?token:std::string{};
}

/**
 * Loads a Java WebView panel directly from browser.dex bundled in the standalone
 * .levipack. No companion APK is needed. This is experimental and opt-in; it
 * uses Android ActivityThread reflection and cannot assume every launcher
 * exposes an Activity. Never loads arbitrary downloaded dex or a remote APK.
 */
bool openEmbeddedBrowser(JNIEnv* env,const std::filesystem::path& modDir){
    const auto file=modDir/"browser.dex";
    if(!std::filesystem::is_regular_file(file)) {
        __android_log_print(ANDROID_LOG_WARN,TAG,
            "Standalone browser.dex missing from package: %s",file.c_str());
        return false;
    }
    jclass threadClass=env->FindClass("java/lang/Thread");
    if(!threadClass||clearException(env,"WebView Thread"))return false;
    jmethodID current=env->GetStaticMethodID(threadClass,"currentThread",
                                              "()Ljava/lang/Thread;");
    jobject thread=current?env->CallStaticObjectMethod(threadClass,current):nullptr;
    jmethodID getLoader=env->GetMethodID(threadClass,"getContextClassLoader",
                                          "()Ljava/lang/ClassLoader;");
    jobject parent=thread&&getLoader?env->CallObjectMethod(thread,getLoader):nullptr;
    if(clearException(env,"WebView parent class loader")||!parent) {
        if(thread)env->DeleteLocalRef(thread);
        env->DeleteLocalRef(threadClass);
        return false;
    }
    jclass loaderClass=env->FindClass("dalvik/system/DexClassLoader");
    if(!loaderClass||clearException(env,"DexClassLoader")) {
        env->DeleteLocalRef(parent);env->DeleteLocalRef(thread);
        env->DeleteLocalRef(threadClass);return false;
    }
    jmethodID ctor=env->GetMethodID(loaderClass,"<init>",
          "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V");
    jstring dexPath=toJava(env,file.string());
    jstring optimized=toJava(env,modDir.string());
    jobject loader=ctor?env->NewObject(loaderClass,ctor,dexPath,optimized,nullptr,parent):nullptr;
    bool ok=false;
    if(loader && !clearException(env,"WebView DexClassLoader init")){
        jmethodID load=env->GetMethodID(loaderClass,"loadClass",
                                         "(Ljava/lang/String;)Ljava/lang/Class;");
        jstring className=toJava(env,"com.reaaurelis.levimusic.standalone.NativeBrowser");
        jobject loaded=load?env->CallObjectMethod(loader,load,className):nullptr;
        if(loaded && !clearException(env,"WebView loadClass")){
            jclass c=static_cast<jclass>(loaded);
            jmethodID open=env->GetStaticMethodID(c,"open","()V");
            if(open){
                env->CallStaticVoidMethod(c,open);
                ok=!clearException(env,"NativeBrowser.open");
            }else clearException(env,"NativeBrowser.open missing");
            env->DeleteLocalRef(loaded);
        }
        env->DeleteLocalRef(className);
    }else{
        clearException(env,"DexClassLoader init failed");
    }
    if(loader)env->DeleteLocalRef(loader);
    if(dexPath)env->DeleteLocalRef(dexPath);
    if(optimized)env->DeleteLocalRef(optimized);
    env->DeleteLocalRef(loaderClass);
    env->DeleteLocalRef(parent);
    env->DeleteLocalRef(thread);
    env->DeleteLocalRef(threadClass);
    __android_log_print(ok?ANDROID_LOG_INFO:ANDROID_LOG_WARN,TAG,
        "Embedded Android WebView open request %s",ok?"posted":"failed");
    return ok;
}

std::string marqueeTitle(const std::string& value,size_t width,float seconds) {
    std::vector<std::string> symbols;
    for(size_t i=0;i<value.size();) {
        unsigned char c=static_cast<unsigned char>(value[i]);
        size_t n=c<0x80?1:c<0xE0?2:c<0xF0?3:4;
        n=std::min(n,value.size()-i);
        symbols.push_back(value.substr(i,n));i+=n;
    }
    if(symbols.size()<=width)return value;
    size_t offset=static_cast<size_t>(std::max(0.f,seconds*.85f)) % (symbols.size()+7);
    std::string view;
    for(size_t i=0;i<width;i++) {
        size_t index=offset+i;
        view += index<symbols.size()?symbols[index]:" ";
    }
    return view;
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
    Rect mini,outer,left,lyrics,right,close,settings,pair,library,addSong,searchBox,searchGo,prev,play,next,stop,seek,pagePrev,pageNext;
    Rect tabSearch,tabQueue,tabLyrics,queueNow,queueSaved,savedInput,savedCreate;
    std::vector<Rect> rows;
    bool ok()const{return w>0&&h>0;}
};
UI measure(int hudScale=84,int hudX=3,int hudY=93,int panelTop=15) {
    UI p;
    const auto surface=pl::modmenu::getHudSurfaceSize();
    p.w=surface.width;p.h=surface.height;
    if(!p.ok())return p;
    p.s=std::clamp(p.h/540.f,.65f,2.5f);
    const float u=p.s,factor=std::clamp(hudScale,55,125)/100.f;
    const float miniW=std::min(267*u*factor,p.w*.45f);
    const float miniH=50*u*factor;
    p.mini={
        std::clamp((p.w-miniW)*std::clamp(hudX,0,100)/100.f,4*u,p.w-miniW-4*u),
        std::clamp((p.h-miniH)*std::clamp(hudY,0,100)/100.f,4*u,p.h-miniH-4*u),
        miniW,miniH
    };
    const float topPercent=std::clamp(panelTop,12,32)/100.f;
    p.outer={p.w*.045f,p.h*topPercent,p.w*.91f,p.h*(.977f-topPercent)};
    const float contentY=p.outer.y+53*u,contentH=p.outer.h-65*u;
    const float contentX=p.outer.x+15*u,contentW=p.outer.w-30*u;
    const float gutter=9*u,playerW=contentW*.375f;
    p.left={contentX,contentY,playerW,contentH};
    p.right={contentX+playerW+gutter,contentY,
             contentW-playerW-gutter,contentH};
    // Right-hand three tabs share one content surface, never overlap.
    const float tabGap=5*u;
    const float tabW=(p.right.w-24*u-2*tabGap)/3.f;
    const float tabY=p.right.y+7*u;
    p.tabSearch={p.right.x+12*u,tabY,tabW,30*u};
    p.tabQueue={p.tabSearch.x+tabW+tabGap,tabY,tabW,30*u};
    p.tabLyrics={p.tabQueue.x+tabW+tabGap,tabY,tabW,30*u};
    p.close={p.outer.x+p.outer.w-42*u,p.outer.y+10*u,30*u,30*u};
    p.settings={p.close.x-39*u,p.close.y,30*u,30*u};
    p.pair={p.settings.x-69*u,p.close.y,61*u,30*u};
    p.library={p.right.x+p.right.w-94*u,p.right.y+48*u,82*u,31*u};
    p.queueNow={p.right.x+12*u,p.right.y+48*u,81*u,31*u};
    p.queueSaved={p.library.x,p.right.y+48*u,82*u,31*u};
    p.addSong={p.right.x+p.right.w-48*u,p.right.y+87*u,35*u,26*u};
    p.savedInput={p.right.x+12*u,p.right.y+85*u,p.right.w-85*u,31*u};
    p.savedCreate={p.right.x+p.right.w-60*u,p.right.y+85*u,48*u,31*u};
    p.searchBox={p.right.x+12*u,p.right.y+49*u,p.right.w-83*u,34*u};
    p.searchGo={p.right.x+p.right.w-59*u,p.right.y+49*u,47*u,34*u};
    const float bottom=p.left.y+p.left.h-57*u;
    const float mid=p.left.x+p.left.w*.5f;
    p.stop={-10000.f,-10000.f,0.f,0.f};
    p.prev={mid-87*u,bottom,45*u,44*u};
    p.play={mid-33*u,bottom,66*u,44*u};
    p.next={mid+42*u,bottom,45*u,44*u};
    p.seek={p.left.x+18*u,bottom-41*u,p.left.w-36*u,15*u};
    const float rowStart=p.right.y+118*u;
    const float rowBottom=p.right.y+p.right.h-40*u;
    const float rowStep=std::max(19*u,(rowBottom-rowStart)/5.f);
    for(int i=0;i<5;i++)
        p.rows.push_back({p.right.x+11*u,rowStart+i*rowStep,
                          p.right.w-22*u,std::max(15*u,rowStep-4*u)});
    p.pagePrev={p.right.x+p.right.w-112*u,p.right.y+p.right.h-36*u,37*u,29*u};
    p.pageNext={p.right.x+p.right.w-62*u,p.right.y+p.right.h-36*u,37*u,29*u};
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
    std::vector<Track> searchCache_;
    std::vector<Track> queueTracks_;
    int rightTab_=0; // 0 Search, 1 Playlist / Up Next, 2 Lyrics
    std::vector<std::string> playlistNames_;
    std::string activePlaylist_;
    int libraryMode_=0; // 0 Search, 1 saved playlists, 2 saved tracks, 3 Up Next radio/playlist queue
    std::vector<Lyric> lyrics_;
    std::string query_="lofi", title_="LeviMusic", artist_="Online Music";
    std::string info_="Tap to open your music library", lyric_, apiKey_, bridgeKey_;
#if defined(LEVI_STANDALONE)
    std::string chruBase_="http://127.0.0.1:8080";
#endif
    std::string coverUrl_,coverImageId_;
    int coverCount_=0;
    int selected_=-1,page_=0,volume_=75,elapsed_=0,total_=0;
    bool open_=false,typing_=false,hud_=true,lyricsOn_=true,playing_=false,shutdown_=false;
    std::atomic<bool> enabled_{false};
    std::atomic<int> hudSize_{84},hudX_{3},hudY_{93},panelTop_{15};
    std::atomic<bool> animation_{true};
    std::atomic<bool> browserEnabled_{false},worldLyrics3D_{false},romanizeLyrics_{true};
#if defined(LEVI_STANDALONE)
    std::atomic<bool> useBridge_{false},lyricsBelow_{false},floatingLyrics_{false};
#else
    std::atomic<bool> useBridge_{true},lyricsBelow_{false},floatingLyrics_{false};
#endif
    std::chrono::steady_clock::time_point lyricChanged_{std::chrono::steady_clock::now()};
    std::chrono::steady_clock::time_point bridgePositionStamp_{std::chrono::steady_clock::now()};
    std::chrono::steady_clock::time_point panelToggled_{std::chrono::steady_clock::now()};
    std::string playbackError_;

public:
    LeviMusic():self_(*ll::mod::NativeMod::current()){}
    static LeviMusic& instance(){static LeviMusic mod;return mod;}
    bool load(){
        vm_=self_.getJavaVM();
        try{
            std::ifstream keyFile(self_.getConfigDir()/"bridge.token");
            std::string saved;
            if(keyFile>>saved && validToken(saved))bridgeKey_=saved;
        }catch(...){}
        __android_log_print(ANDROID_LOG_INFO,TAG,"LeviMusic v1.2.3 RC loaded, vm=%p",(void*)vm_);
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
#if defined(LEVI_STANDALONE)
            .config("useBridge","Use Android Bridge (optional)",pl::modmenu::ConfigType::Toggle,"false")
#else
            .config("useBridge","Use Android Bridge",pl::modmenu::ConfigType::Toggle,"true")
#endif
            .config("bridgeKey","Bridge Key (copy from companion app)",pl::modmenu::ConfigType::Text,"")
            .config("lyricBelow","Show lyric bubble below HUD",pl::modmenu::ConfigType::Toggle,"false")
            .config("floatingLyrics","Legacy 2D lyric scatter",pl::modmenu::ConfigType::Toggle,"false")
            .config("worldLyrics3D","Floating lyrics (safe 2D fallback; 3D renderer unavailable)",pl::modmenu::ConfigType::Toggle,"false")
            .config("romanizeLyrics","Romanize lyrics (Latin pronunciation)",pl::modmenu::ConfigType::Toggle,"true")
#if defined(LEVI_STANDALONE)
            .config("embeddedBrowser","Experimental embedded WebView (visible video)",pl::modmenu::ConfigType::Toggle,"false")
#endif
#if defined(LEVI_STANDALONE)
            .config("chruServer","ChruPipe server URL (local 127.0.0.1:8080 or HTTPS)",
                    pl::modmenu::ConfigType::Text,"http://127.0.0.1:8080")
#else
            .config("apiKey","Audius API Key (optional)",pl::modmenu::ConfigType::Text,"")
#endif
            .config("hud","Show compact HUD",pl::modmenu::ConfigType::Toggle,"true")
            .config("lyrics","Enable lyrics",pl::modmenu::ConfigType::Toggle,"true")
            .config("hudSize","HUD Size (%)",pl::modmenu::ConfigType::SliderInt,"84","55","125")
            .config("hudX","HUD Horizontal Position (0-100%)",pl::modmenu::ConfigType::SliderInt,"3","0","100")
            .config("hudY","HUD Vertical Position (0-100%)",pl::modmenu::ConfigType::SliderInt,"93","0","100")
            .config("panelTop","Menu Distance from Top (%)",pl::modmenu::ConfigType::SliderInt,"15","12","32")
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
        lm_world3d::shutdown();
        pl::modmenu::submitDrawCommands(MODULE,std::span<const pl::modmenu::DrawCommand>{});
        pl::modmenu::unregisterModule(MODULE);
        return true;
    }
    bool unload(){return true;}
private:
    void setConfig(std::string_view k,std::string_view v){
        if(k=="romanizeLyrics"){
            romanizeLyrics_=v=="true"||v=="1";
        } else if(k=="worldLyrics3D") {
            worldLyrics3D_=v=="true"||v=="1";
        } else if(k=="embeddedBrowser") {
            browserEnabled_=v=="true"||v=="1";
        } else if(k=="floatingLyrics") {
            floatingLyrics_=v=="true"||v=="1";
        } else if(k=="useBridge") {
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
            try{panelTop_=std::clamp(std::stoi(std::string(v)),12,32);}catch(...){}
        } else if(k=="animation") {
            animation_ = v=="1"||v=="true";
        } else {
            std::lock_guard lock(mu_);
#if defined(LEVI_STANDALONE)
            if(k=="chruServer"){
                std::string requested(v);
                if(requested.size()<350 &&
                   (requested.rfind("https://",0)==0 ||
                    requested.rfind("http://127.0.0.1:",0)==0 ||
                    requested=="http://127.0.0.1" ||
                    requested.rfind("http://localhost:",0)==0 ||
                    requested=="http://localhost")) {
                    chruBase_=std::move(requested);
                }
            } else
#endif
            if(k=="bridgeKey"){
                if(!v.empty())bridgeKey_=std::string(v).substr(0,100);
            }
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
        if(!enabled_ || (e.action!=0 && e.action!=1))return false;
        UI l=measure(hudSize_,hudX_,hudY_,panelTop_);
        if(!l.ok())return false;
        bool opened,hud;
        {std::lock_guard lock(mu_);opened=open_;hud=hud_;}
        if(e.action==0)return opened || (hud&&l.mini.contains(e.x,e.y));
        if(!opened){
            if(hud&&l.mini.contains(e.x,e.y)){
                {std::lock_guard lock(mu_);open_=true;panelToggled_=std::chrono::steady_clock::now();}
                paint();return true;
            }
            return false;
        }
        if(l.close.contains(e.x,e.y)||!l.outer.contains(e.x,e.y)){
            {std::lock_guard lock(mu_);open_=false;typing_=false;panelToggled_=std::chrono::steady_clock::now();}
            pl::input::hideKeyboard();
        }else if(l.settings.contains(e.x,e.y)){
            {std::lock_guard lock(mu_);open_=false;typing_=false;panelToggled_=std::chrono::steady_clock::now();}
            pl::input::hideKeyboard();
            pl::modmenu::requestOpenMenu();
#if defined(LEVI_STANDALONE)
        }else if(l.pair.contains(e.x,e.y)&&!useBridge_){
            if(browserEnabled_)addJob(Action::OpenBrowser);
            else update("Embedded browser disabled","Standalone",
                        "Enable the experimental WebView in mod settings first");
#endif
        }else if(l.pair.contains(e.x,e.y)&&useBridge_){
            addJob(Action::Pair);
        }else if(l.tabSearch.contains(e.x,e.y)){
            {std::lock_guard lock(mu_);
                rightTab_=0;libraryMode_=0;results_=searchCache_;page_=0;selected_=-1;typing_=false;
            }
            pl::input::hideKeyboard();
        }else if(l.tabQueue.contains(e.x,e.y)){
            {std::lock_guard lock(mu_);
                rightTab_=1;libraryMode_=3;page_=0;selected_=-1;typing_=false;
            }
            pl::input::hideKeyboard();
            if(useBridge_)addJob(Action::QueueRefresh);
        }else if(l.tabLyrics.contains(e.x,e.y)){
            {std::lock_guard lock(mu_);rightTab_=2;typing_=false;}
            pl::input::hideKeyboard();
        }else if(l.prev.contains(e.x,e.y))addJob(Action::Previous);
        else if(l.play.contains(e.x,e.y))addJob(Action::PlayPause);
        else if(l.next.contains(e.x,e.y))addJob(Action::Next);
        else if(l.seek.contains(e.x,e.y)){
            int duration;
            {std::lock_guard lock(mu_);duration=total_;}
            if(duration>0){
                float ratio=std::clamp((e.x-l.seek.x)/l.seek.w,0.f,1.f);
                addJob(Action::Seek,std::to_string(static_cast<int>(duration*ratio)));
            }
        }else{
            int tab,mode;
            {std::lock_guard lock(mu_);tab=rightTab_;mode=libraryMode_;}
            if(tab==0 && l.searchBox.contains(e.x,e.y)){
                {std::lock_guard lock(mu_);if(!typing_)query_.clear();typing_=true;}
                pl::input::showKeyboard();
            }else if(tab==0 && l.searchGo.contains(e.x,e.y)){
                std::string q;
                {std::lock_guard lock(mu_);q=query_;typing_=false;page_=0;}
                pl::input::hideKeyboard();
                addJob(Action::Search,q);
            }else if(tab==1 && l.queueNow.contains(e.x,e.y)){
                {std::lock_guard lock(mu_);libraryMode_=3;page_=0;selected_=-1;typing_=false;}
                pl::input::hideKeyboard();
                if(useBridge_)addJob(Action::QueueRefresh);
            }else if(tab==1 && l.queueSaved.contains(e.x,e.y)){
                {std::lock_guard lock(mu_);libraryMode_=1;page_=0;selected_=-1;typing_=false;}
                pl::input::hideKeyboard();
                if(useBridge_)addJob(Action::PlaylistList);
            }else if(tab==1 && mode==1 && l.savedInput.contains(e.x,e.y)){
                {std::lock_guard lock(mu_);query_.clear();typing_=true;}
                pl::input::showKeyboard();
            }else if(tab==1 && mode==1 && l.savedCreate.contains(e.x,e.y)){
                std::string name;
                {std::lock_guard lock(mu_);name=query_;typing_=false;}
                pl::input::hideKeyboard();
                addJob(Action::PlaylistCreate,name);
            }else if(tab!=2 && l.pagePrev.contains(e.x,e.y)){
                {std::lock_guard lock(mu_);page_=std::max(0,page_-1);}
            }else if(tab!=2 && l.pageNext.contains(e.x,e.y)){
                {std::lock_guard lock(mu_);
                    const size_t count=tab==0?results_.size():
                            mode==3?queueTracks_.size():
                            mode==1?playlistNames_.size():results_.size();
                    if(static_cast<size_t>((page_+1)*5)<count)++page_;
                }
            }else if(tab!=2){
                int selected=-1;
                std::string id,name;
                bool plus=false,minus=false;
                {
                    std::lock_guard lock(mu_);
                    for(size_t i=0;i<l.rows.size();i++){
                        int index=page_*5+static_cast<int>(i);
                        const bool inRange=tab==0?index<static_cast<int>(results_.size()):
                            mode==3?index<static_cast<int>(queueTracks_.size()):
                            mode==1?index<static_cast<int>(playlistNames_.size()):
                                    index<static_cast<int>(results_.size());
                        if(!inRange || !l.rows[i].contains(e.x,e.y))continue;
                        selected_=index;selected=index;
                        plus=tab==0 && e.x>l.rows[i].x+l.rows[i].w-42*l.s;
                        minus=tab==1 && mode==3 && index>0
                            && e.x>l.rows[i].x+l.rows[i].w-42*l.s;
                        if(tab==0)id=results_[static_cast<size_t>(index)].id;
                        else if(mode==3)id=queueTracks_[static_cast<size_t>(index)].id;
                        else if(mode==1)name=playlistNames_[static_cast<size_t>(index)];
                        break;
                    }
                }
                if(selected>=0){
                    if(tab==0)addJob(plus?Action::QueueAdd:Action::Select,id);
                    else if(mode==3)addJob(minus?Action::QueueRemove:Action::QueuePlay,id);
                    else if(mode==1)addJob(Action::PlaylistOpen,name);
                    else if(mode==2)addJob(Action::PlaylistPlay,std::to_string(selected));
                }
            }
        }
        paint();
        return true;
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
        if(editing){if(submit){
            pl::input::hideKeyboard();
            int mode;{std::lock_guard lock(mu_);mode=libraryMode_;}
            addJob(mode==1?Action::PlaylistCreate:Action::Search,query);
        }paint();}
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
        if(editing&&e.isKeyDown){if(submit){
            pl::input::hideKeyboard();
            int mode;{std::lock_guard lock(mu_);mode=libraryMode_;}
            addJob(mode==1?Action::PlaylistCreate:Action::Search,query);
        }paint();}
        return editing;
    }
    void paint(){
        if(!enabled_)return;
        UI l=measure(hudSize_,hudX_,hudY_,panelTop_);
        if(!l.ok())return;
        std::string title,artist,info,query,lyric,coverImage;
        std::vector<Track> tracks,queueTracks;
        std::vector<std::string> playlistNames;
        std::string activePlaylist;
        std::vector<Lyric> timedLyrics;
        float lyricAge=100.f;
        int current,page,elapsed,total,volume,libraryMode,rightTab;
        bool open,showHud,playing,typing;
        float panelAge=1.f;
        {
            std::lock_guard lock(mu_);
            title=title_;artist=artist_;info=info_;query=query_;lyric=lyricsOn_?lyric_:"";coverImage=coverImageId_;
            tracks=results_;queueTracks=queueTracks_;playlistNames=playlistNames_;
            activePlaylist=activePlaylist_;libraryMode=libraryMode_;rightTab=rightTab_;
            timedLyrics=lyrics_;
            panelAge=std::chrono::duration<float>(std::chrono::steady_clock::now()-panelToggled_).count();
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
        float panelProgress=std::clamp(panelAge/.24f,0.f,1.f);
        panelProgress=panelProgress*panelProgress*(3.f-2.f*panelProgress);
        const float panelFade=animation_?(open?panelProgress:1.f-panelProgress):1.f;
        const bool panelVisible=open||(animation_&&panelAge<.24f);
        const bool streamingBridge=useBridge_.load();
        const float tick=std::chrono::duration<float>(std::chrono::steady_clock::now().time_since_epoch()).count();
        // Client-only native 3D scene: independent from Behavior Packs or servers.
        if(worldLyrics3D_ && lm_world3d::installed)
            lm_world3d::publish((lyricsOn_ && !timedLyrics.empty())?lyric:std::string{});
        else if(lm_world3d::installed)
            lm_world3d::publish("");
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
        if(!panelVisible) {
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
                text(x,l.mini.y+l.mini.h*.40f,animation_?marqueeTitle(title,25,tick):title,fs,C_TEXT,125);
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
            if(lyricsOn_ && !(worldLyrics3D_ && lm_world3d::installed) && !timedLyrics.empty() && elapsed>=0) {
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
                        float subtitleX=l.mini.x+3*u;
                        if(floatingLyrics_ || worldLyrics3D_){
                            // Client-only 2D fallback, per-line location (not 3D world text).
                            // True world-space rendering needs version-specific camera hooks.
                            const uint32_t seed=static_cast<uint32_t>(item.ms)*2654435761U+0x9E3779B9U;
                            const float px=static_cast<float>(((seed^(seed>>13))*1274126177U)%1000U)/1000.f;
                            const float py=static_cast<float>(((seed>>7)*747796405U)%1000U)/1000.f;
                            subtitleX=std::clamp(l.w*(.18f+px*.47f),18*u,l.w*.67f);
                            subtitleY=std::clamp(l.h*(.32f+py*.40f),44*u,l.h-36*u);
                        }
                        if(!(floatingLyrics_ || worldLyrics3D_) && (subtitleY<25*u||subtitleY>l.h-12*u))
                            subtitleY=lyricsBelow_?l.mini.y-16*u:l.mini.y+l.mini.h+26*u;
                        if(subtitleY>23*u&&subtitleY<l.h-10*u)
                            outlineText(subtitleX,subtitleY,item.line,((floatingLyrics_ || worldLyrics3D_)?15.8f:14.4f)*u,alpha);
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
        text(l.outer.x+199*u,l.outer.y+31*u,"v1.2.3 • REAAURELIS",10.0f*u,C_DIM);
#if defined(LEVI_STANDALONE)
        if(!streamingBridge){
            box(l.pair,0xFF33363CU,9.f);
            text(l.pair.x+8*u,l.pair.y+21*u,"WEB",10.2f*u,C_TEXT,8);
        }
#endif
        if(streamingBridge){
            box(l.pair,0xFF33363CU,9.f);
            text(l.pair.x+8*u,l.pair.y+21*u,"PAIR",10.2f*u,C_TEXT,6);

        }
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
        const size_t titleChars=static_cast<size_t>(
            std::clamp((l.left.w-129*u)/(7.8f*u),9.f,35.f));
        text(ix,l.left.y+65*u,animation_?marqueeTitle(title,titleChars,tick):title,
             15.2f*u,C_TEXT,static_cast<size_t>(titleChars*4));
        text(ix,l.left.y+87*u,artist,12*u,C_DIM,43);
        // Compact status, no potentially huge text block overlapping seek.
        text(l.left.x+17*u,l.left.y+134*u,info,10.6f*u,C_DIM,
             static_cast<size_t>(std::max(12.f,l.left.w/(7.7f*u))));

        // Player card no longer embeds a second lyrics box.
        const float separation=std::min(l.seek.y-22*u,l.left.y+158*u);
        if(separation>l.left.y+139*u) {
            box({l.left.x+16*u,separation,l.left.w-32*u,1*u},0x33FFFFFFU,1.f);
            icon("audio-lines",l.left.x+18*u,separation+12*u,13*u);
            text(l.left.x+40*u,separation+23*u,
                 playing?"LISTENING NOW":"READY TO LISTEN",10*u,C_DIM,34);
        }
        const float prog=total>0?std::clamp(float(elapsed)/total,0.f,1.f):0.f;
        const float lineY=l.seek.y+l.seek.h*.5f;
        box({l.seek.x,lineY-1.35f*u,l.seek.w,2.7f*u},0xFF334758U,2.f);
        if(total>0)box({l.seek.x,lineY-1.35f*u,std::max(.5f,l.seek.w*prog),2.7f*u},C_MINT,2.f);
        if(total>0)disc(l.seek.x+l.seek.w*prog,lineY,4*u,C_MINT);
        text(l.seek.x,l.seek.y+l.seek.h+9*u,clockText(elapsed),10*u,C_DIM);
        text(l.seek.x+l.seek.w-42*u,l.seek.y+l.seek.h+9*u,clockText(total),10*u,C_DIM);
        // One central Play/Pause button. Redundant Stop removed.
        box(l.prev,0xFF294052U,10.f);
        icon("skip-back",l.prev.x+12*u,l.prev.y+12*u,22*u);
        box(l.play,0xFF575B64U,13.f);
        box({l.play.x+2*u,l.play.y+2*u,l.play.w-4*u,l.play.h-4*u},0xFF696E77U,12.f);
        icon(playing?"pause":"play",l.play.x+(l.play.w-23*u)*.5f,l.play.y+10*u,23*u);
        box(l.next,0xFF294052U,10.f);
        icon("skip-forward",l.next.x+12*u,l.next.y+12*u,22*u);
        icon("volume-2",l.left.x+16*u,l.left.y+l.left.h-14*u,12*u);
        text(l.left.x+34*u,l.left.y+l.left.h-4*u,
             streamingBridge?("AUTOPLAY NEXT • BRIDGE"):
#if defined(LEVI_STANDALONE)
             ("CHRUPIPE • "+std::to_string(volume)+"%"),
#else
             ("AUDIUS • "+std::to_string(volume)+"%"),
#endif
             9.7f*u,C_DIM);


        // Compact right-hand tabbed content: Search | Playlist / Up Next | Lyrics.
        for(int tab=0;tab<3;tab++){
            Rect r=tab==0?l.tabSearch:tab==1?l.tabQueue:l.tabLyrics;
            const bool active=rightTab==tab;
            box(r,active?0xFF525760U:0xFF282B31U,8.f);
            const std::string label=tab==0?"SEARCH":tab==1?"PLAYLIST":"LYRICS";
            const float alignX=r.x+(r.w-static_cast<float>(label.size())*6.2f*u)*.5f;
            text(alignX,r.y+20*u,label,11.1f*u,active?C_TEXT:C_DIM,14);
        }
        const auto drawPage=[&](size_t count,const std::string& prefix){
            if(count>5){
                box(l.pagePrev,0xFF34373EU,7.f);
                icon("chevron-left",l.pagePrev.x+9*u,l.pagePrev.y+6*u,18*u);
                box(l.pageNext,0xFF34373EU,7.f);
                icon("chevron-right",l.pageNext.x+9*u,l.pageNext.y+6*u,18*u);
            }
            text(l.right.x+14*u,l.right.y+l.right.h-14*u,
                 prefix+"  "+std::to_string(page+1)+" / "+
                    std::to_string(std::max<size_t>(1,(count+4)/5)),10*u,C_DIM,41);
        };
        const auto drawSongs=[&](const std::vector<Track>& songs,bool isRadio,
                                 bool addControl,bool isSaved){
            for(size_t i=0;i<l.rows.size();++i){
                size_t n=static_cast<size_t>(page*5)+i;
                if(n>=songs.size())break;
                Rect row=l.rows[i];
                const bool chosen=isRadio?n==0:static_cast<int>(n)==current;
                box(row,chosen?0xFF414750U:0xFF292C32U,8.f);
                const std::string prefix=isRadio?(n==0?"NOW  ":"NEXT "+std::to_string(n)+"  "):
                    (std::to_string(n+1)+".  ");
                const float suffixWidth=(isRadio && n>0) || addControl?45*u:12*u;
                const size_t maxChars=static_cast<size_t>(
                    std::max(13.f,(row.w-16*u-suffixWidth)/(7.1f*u)));
                text(row.x+10*u,row.y+std::min(row.h-3*u,19*u),
                     prefix+songs[n].title,11.7f*u,C_TEXT,maxChars);
                if(isRadio && n>0){
                    icon("x",row.x+row.w-28*u,row.y+5*u,18*u);
                }else if(addControl){
                    icon("plus",row.x+row.w-27*u,row.y+5*u,18*u);
                }else if(chosen){
                    icon(playing?"audio-lines":"play",row.x+row.w-27*u,row.y+5*u,17*u);
                }
            }
            drawPage(songs.size(),isRadio?"UP NEXT":isSaved?"SAVED":"RESULTS");
        };
        if(rightTab==2){
            icon("audio-lines",l.right.x+16*u,l.right.y+57*u,17*u);
            text(l.right.x+44*u,l.right.y+71*u,"SYNCHRONIZED LYRICS",11.7f*u,C_TEXT,35);
            box({l.right.x+13*u,l.right.y+85*u,l.right.w-26*u,1*u},0x35FFFFFFU,1.f);
            const int count=static_cast<int>(timedLyrics.size());
            int activeLine=-1;
            for(int i=0;i<count;i++){
                if(timedLyrics[static_cast<size_t>(i)].ms>elapsed)break;
                activeLine=i;
            }
            if(count==0){
                text(l.right.x+20*u,l.right.y+137*u,
                     "No synchronized lyrics found for this track.",12.6f*u,C_DIM,69);
                text(l.right.x+20*u,l.right.y+160*u,
                     "Try another recording or import timed LRC in the app.",10.5f*u,C_DIM,72);
            }else{
                const float step=std::max(21*u,
                    std::min(37*u,(l.right.h-111*u)/6.f));
                const int start=std::max(0,activeLine-2);
                const size_t lineLength=static_cast<size_t>(
                    std::max(18.f,(l.right.w-42*u)/(7.2f*u)));
                for(int i=0;i<7;i++){
                    int j=start+i;
                    if(j>=count)break;
                    float y=l.right.y+110*u+i*step;
                    if(y+9*u>l.right.y+l.right.h-26*u)break;
                    bool currentLine=j==activeLine;
                    if(currentLine){
                        box({l.right.x+12*u,y-18*u,l.right.w-24*u,27*u},0xFF3A3E47U,7.f);
                        box({l.right.x+12*u,y-18*u,2*u,27*u},0xFFEAEDF1U,1.f);
                    }
                    text(l.right.x+22*u,y,timedLyrics[static_cast<size_t>(j)].line,
                         currentLine?12.6f*u:11*u,currentLine?C_TEXT:C_DIM,lineLength);
                }
                text(l.right.x+16*u,l.right.y+l.right.h-12*u,
                     "LRC "+std::to_string(std::max(0,activeLine+1))+
                     " / "+std::to_string(count),10*u,C_DIM,36);
            }
        }else if(rightTab==0){
            box(l.searchBox,0xFF282B30U,8.f);
            text(l.searchBox.x+10*u,l.searchBox.y+22*u,
                 query.empty()?"Search songs and artists...":
                    ((typing?"_ ":"")+query),
                 11.9f*u,typing?C_TEXT:C_DIM,63);
            box(l.searchGo,0xFF50555EU,8.f);
            icon("search",l.searchGo.x+13*u,l.searchGo.y+7*u,20*u);
            text(l.right.x+15*u,l.right.y+106*u,
                 "SEARCH RESULTS  •  Tap song to play, + to add to Up Next",
                 10*u,C_DIM,75);
            if(tracks.empty()){
                text(l.right.x+18*u,l.right.y+169*u,
                     "Search for a song to start your radio.",12*u,C_DIM,57);
            }else drawSongs(tracks,false,streamingBridge,false);
        }else if(rightTab==1){
            box(l.queueNow,libraryMode==3?0xFF50555EU:0xFF2C3037U,7.f);
            text(l.queueNow.x+11*u,l.queueNow.y+20*u,
                 "UP NEXT",10.5f*u,libraryMode==3?C_TEXT:C_DIM,15);
            box(l.queueSaved,(libraryMode==1||libraryMode==2)?0xFF50555EU:0xFF2C3037U,7.f);
            text(l.queueSaved.x+10*u,l.queueSaved.y+20*u,
                 "SAVED",10.5f*u,libraryMode!=3?C_TEXT:C_DIM,14);
            if(!streamingBridge){
                text(l.right.x+18*u,l.right.y+150*u,
                     "Up Next is available in the Android Bridge version.",
                     11.5f*u,C_DIM,66);
            }else if(libraryMode==3){
                text(l.right.x+15*u,l.right.y+106*u,
                     "RADIO • CURRENT TRACK + 3 RECOMMENDATIONS",10.6f*u,C_DIM,61);
                if(queueTracks.empty()){
                    text(l.right.x+18*u,l.right.y+164*u,
                         "Select a song from Search to create Up Next.",
                         11.8f*u,C_DIM,60);
                }else{
                    drawSongs(queueTracks,true,false,false);
                    text(l.right.x+15*u,l.right.y+l.right.h-15*u,
                         "Tap a track to play or X to remove it.",10*u,C_DIM,60);
                }
            }else if(libraryMode==1){
                box(l.savedInput,0xFF282B30U,8.f);
                text(l.savedInput.x+9*u,l.savedInput.y+21*u,
                     typing?("_ "+query):(query.empty()?"New playlist name...":query),
                     11.2f*u,typing?C_TEXT:C_DIM,47);
                box(l.savedCreate,0xFF555960U,8.f);
                icon("plus",l.savedCreate.x+14*u,l.savedCreate.y+6*u,18*u);
                for(size_t i=0;i<l.rows.size();i++){
                    size_t index=static_cast<size_t>(page*5)+i;
                    if(index>=playlistNames.size())break;
                    Rect row=l.rows[i];
                    box(row,0xFF292C32U,8.f);
                    text(row.x+11*u,row.y+std::min(19*u,row.h-3*u),
                         playlistNames[index],11.8f*u,C_TEXT,58);
                    icon("chevron-right",row.x+row.w-29*u,row.y+6*u,17*u);
                }
                drawPage(playlistNames.size(),"PLAYLISTS");
            }else{
                text(l.right.x+16*u,l.right.y+103*u,
                     activePlaylist.empty()?"SAVED PLAYLIST":activePlaylist,
                     12.6f*u,C_TEXT,56);
                drawSongs(tracks,false,false,true);
            }
        }
        if(animation_ && panelFade<1.f){
            for(size_t i=0;i<out.size();i++){
                auto& c=out[i];
                if(i>0)c.y+=(1.f-panelFade)*14*u;
                uint32_t alpha=static_cast<uint32_t>(std::round(((c.color>>24)&255U)*panelFade));
                c.color=(c.color&0x00FFFFFFU)|(alpha<<24);
            }
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
#if defined(LEVI_STANDALONE)
            std::string server;
            {std::lock_guard lock(mu_);server=chruBase_;}
            update(tr.title,tr.artist,"Resolving stream via ChruPipe...");
            const auto mediaUrl=streamChruPipe(env,server,tr.id);
            if(mediaUrl.empty()) {
                update(tr.title,tr.artist,"ChruPipe returned no compatible audio stream");
                return;
            }
            bool ok=player.startUrl(env,mediaUrl,vol);
            if(!ok){
                update(tr.title,tr.artist,"Could not play extracted stream in Android MediaPlayer");
                return;
            }
#else
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
#endif
            {
                std::lock_guard lock(mu_);
                playing_=true;total_=player.duration(env);
            }
#if defined(LEVI_STANDALONE)
            update(tr.title,tr.artist,"ChruPipe / NewPipeExtractor streaming");
#else
            update(tr.title,tr.artist,"Streaming online (Audius)");
#endif
            std::vector<Lyric> found=searchLyrics(env,tr,player.duration(env));
            {
                std::lock_guard lock(mu_);
                if(romanizeLyrics_)romanizeLyrics(env,found);
                lyrics_=std::move(found);
                lyric_=lyrics_.empty()?"No synchronized lyrics":"...";
            }
        };
        auto lastPaint=std::chrono::steady_clock::now();
        auto lastBridge=std::chrono::steady_clock::now()-std::chrono::seconds(2);
        auto lastQueue=std::chrono::steady_clock::now()-std::chrono::seconds(3);
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
            // Experimental local-only world rendering, opt-in and signature-gated.
            if(worldLyrics3D_){
                // Native world hook is disabled until renderer offsets are verified for this game build.
                // The old signature matched an incompatible update method and stalled the client.
                lm_world3d::enabled=false;
            }else if(lm_world3d::installed){
                // Do not unhook a live game render path on every settings toggle.
                // Keep the detour inert; unhook only on module disable.
                lm_world3d::enabled=false;
                lm_world3d::publish("");
            }
            if(useBridge_) {
                std::string key;
                {std::lock_guard lock(mu_);key=bridgeKey_;}
                if(hasJob) {
                    std::string action;
                    switch(job.action) {
                        case Action::Pair: {
                            const std::string candidate=clipboardToken(env);
                            if(validToken(candidate) && !bridgeGet("/v1/health",candidate).empty()){
                                try{
                                    std::filesystem::create_directories(self_.getConfigDir());
                                    std::ofstream file(self_.getConfigDir()/"bridge.token",std::ios::trunc);
                                    if(file)file<<candidate;
                                } catch(...) {}
                                {std::lock_guard lock(mu_);bridgeKey_=candidate;}
                                key=candidate;
                                update("LeviMusic connected","Android Bridge",
                                       "Pairing saved. Search and playlists ready.");
                            }else{
                                update("Pairing unsuccessful","LeviMusic Bridge",
                                       "Copy key in the APK, then tap PAIR here");
                            }
                            break;
                        }
                        case Action::PlaylistList: {
                            if(key.empty()){update("Bridge not paired","Playlists","Tap PAIR after copying from app");break;}
                            const auto reply=bridgeGet("/v1/playlists"+
                                (job.query.empty()?std::string{}:
                                ("?query="+encode(env,job.query))),key);
                            auto names=parsePlaylistNames(env,reply);
                            {std::lock_guard lock(mu_);playlistNames_=std::move(names);
                                libraryMode_=1;page_=0;selected_=-1;
                                results_.clear();
                            }
                            if(reply.empty())update("Playlists unavailable","Bridge connection","Open companion and retry");
                            break;
                        }
                        case Action::PlaylistOpen: {
                            if(key.empty())break;
                            auto reply=bridgeGet("/v1/playlist/tracks?name="+encode(env,job.query),key);
                            auto songs=parseBridgeSearch(env,reply);
                            {std::lock_guard lock(mu_);results_=std::move(songs);
                                libraryMode_=2;activePlaylist_=job.query;
                                page_=0;selected_=-1;
                            }
                            if(reply.empty())update("Playlist unavailable",job.query,"Check the Bridge app");
                            break;
                        }
                        case Action::PlaylistPlay: {
                            if(key.empty())break;
                            std::string playlist;
                            {std::lock_guard lock(mu_);playlist=activePlaylist_;}
                            if(!playlist.empty()){
                                auto reply=bridgeGet("/v1/playlist/play?name="+encode(env,playlist)
                                           +"&index="+encode(env,job.query),key);
                                auto root=makeJson(env,reply);
                                bool accepted=root&&optString(env,root,"accepted")=="true";
                                if(root)env->DeleteLocalRef(root);
                                if(!accepted)update("Playlist play failed",playlist,"Retry from the app");
                            }
                            break;
                        }
                        case Action::PlaylistCreate: {
                            if(key.empty())break;
                            if(job.query.empty()){update("Name required","New playlist","Enter a name");break;}
                            auto reply=bridgeGet("/v1/playlist/create?name="+encode(env,job.query),key);
                            auto root=makeJson(env,reply);
                            bool accepted=root&&optString(env,root,"accepted")=="true";
                            if(root)env->DeleteLocalRef(root);
                            if(!accepted)update("Playlist creation failed",job.query,"Check name or app");
                            auto names=parsePlaylistNames(env,bridgeGet("/v1/playlists",key));
                            {std::lock_guard lock(mu_);playlistNames_=std::move(names);
                                libraryMode_=1;page_=0;query_.clear();selected_=-1;
                            }
                            break;
                        }
                        case Action::PlaylistAdd: {
                            std::string name,id;
                            {std::lock_guard lock(mu_);name=activePlaylist_;
                                if(selected_>=0&&selected_<static_cast<int>(results_.size()))
                                    id=results_[static_cast<size_t>(selected_)].id;
                            }
                            if(name.empty()||id.empty()){
                                update("Choose a playlist and song","Playlists",
                                       "Open a playlist once, then select a search song and tap +");
                                break;
                            }
                            auto reply=bridgeGet("/v1/playlist/add?name="+encode(env,name)
                                   +"&id="+encode(env,id),key);
                            auto root=makeJson(env,reply);
                            bool accepted=root&&optString(env,root,"accepted")=="true";
                            if(root)env->DeleteLocalRef(root);
                            update(accepted?"Added to playlist":"Add failed",name,
                                   accepted?"Song saved":"Check the playlist and Bridge");
                            break;
                        }
                        case Action::QueueRefresh:{
                            if(key.empty())break;
                            const auto payload=bridgeGet("/v1/queue",key);
                            if(!payload.empty()){
                                auto songs=parseBridgeSearch(env,payload);
                                std::lock_guard lock(mu_);
                                queueTracks_=std::move(songs);
                            }
                            break;
                        }
                        case Action::QueueAdd:
                        case Action::QueueRemove:
                        case Action::QueuePlay:{
                            if(key.empty()||job.query.empty())break;
                            const std::string type=job.action==Action::QueueAdd?"add":
                                job.action==Action::QueueRemove?"remove":"play";
                            const auto payload=bridgeGet("/v1/queue/"+type+
                                                       "?id="+encode(env,job.query),key);
                            auto json=makeJson(env,payload);
                            bool ok=json && optString(env,json,"accepted")=="true";
                            std::string message=json?optString(env,json,"message"):"Bridge offline";
                            if(json)env->DeleteLocalRef(json);
                            if(!ok){
                                std::lock_guard lock(mu_);
                                info_=message.empty()?"Unable to edit Up Next":message;
                            }
                            auto updated=parseBridgeSearch(env,bridgeGet("/v1/queue",key));
                            {std::lock_guard lock(mu_);queueTracks_=std::move(updated);}
                            lastQueue=std::chrono::steady_clock::now();
                            break;
                        }
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
                                    searchCache_=results_;
                                    libraryMode_=0;
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
                            std::string selectedId=job.query;
                            {
                                std::lock_guard lock(mu_);
                                if(selectedId.empty() && selected_>=0 &&
                                   selected_<static_cast<int>(results_.size()))
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
                        case Action::OpenBrowser: break;
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
                if(now-lastQueue>std::chrono::milliseconds(1450)) {
                    bool showingQueue;
                    {std::lock_guard lock(mu_);showingQueue=open_&&rightTab_==1&&libraryMode_==3;}
                    if(showingQueue && !key.empty()){
                        auto reply=bridgeGet("/v1/queue",key);
                        if(!reply.empty()){
                            auto queue=parseBridgeSearch(env,reply);
                            {std::lock_guard lock(mu_);queueTracks_=std::move(queue);}
                        }
                        lastQueue=now;
                    }
                }
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
                            auto found=searchLyrics(env,track,state.duration,key);
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
#if defined(LEVI_STANDALONE)
                        update("Searching",job.query,"Contacting your ChruPipe server...");
                        std::string server;
                        {std::lock_guard lock(mu_);server=chruBase_;}
                        auto found=searchChruPipe(env,server,job.query);
#else
                        update("Searching",job.query,"Contacting Audius...");
                        std::string key;
                        {std::lock_guard lock(mu_);key=apiKey_;}
                        auto found=searchAudiusFrom(env,job.query,API,key);
                        if(found.empty())
                            found=searchAudiusFrom(env,job.query,"https://discoveryprovider.audius.co/v1",key);
#endif
                        {
                            std::lock_guard lock(mu_);
                            results_=std::move(found);
                            selected_=results_.empty()?-1:0;
                            page_=0;
                        }
                        if(selected_>=0)playSelected();
#if defined(LEVI_STANDALONE)
                        else update("ChruPipe unavailable / no results",job.query,
                                    "Run the ChruPipe backend or set its server URL");
#else
                        else update("No results",job.query,"Please check internet/API key");
#endif
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
                    case Action::OpenBrowser:
#if defined(LEVI_STANDALONE)
                        if(browserEnabled_) openEmbeddedBrowser(env,self_.getModDir());
#endif
                        break;
                    case Action::Pair:case Action::PlaylistList:case Action::PlaylistOpen:
                    case Action::PlaylistPlay:case Action::PlaylistCreate:case Action::PlaylistAdd:
                    case Action::QueueRefresh:case Action::QueuePlay:
                    case Action::QueueRemove:case Action::QueueAdd:
                        break;
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
            } // Non-bridge mode: ChruPipe (standalone), legacy Audius (full)
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
