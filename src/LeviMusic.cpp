// LeviMusic for LeviLauncher Android, beta 0.1.2
// Music discovery / playback: Audius public API (not Spotify/YouTube scraping).
// Lyrics: optional LRCLIB synchronized lyrics, only when available.
#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>
#include <pl/Input.hpp>
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

std::vector<Lyric> searchLyrics(JNIEnv* env, const Track& tr) {
    const std::string url = "https://lrclib.net/api/search?track_name=" + encode(env, tr.title) +
                            "&artist_name=" + encode(env, tr.artist);
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
        for (int i = 0; i < std::min(count, 5); i++) {
            jobject obj = env->CallObjectMethod(array, get, i);
            if (clearException(env, "lyrics entry") || !obj) continue;
            auto lrc = optString(env, obj, "syncedLyrics");
            env->DeleteLocalRef(obj);
            if (!lrc.empty()) { result = parseLrc(lrc); if (!result.empty()) break; }
        }
    }
    if (array) env->DeleteLocalRef(array);
    if (source) env->DeleteLocalRef(source);
    env->DeleteLocalRef(arrayClass);
    return result;
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
    Rect mini,outer,left,right,close,searchBox,searchGo,prev,play,next,stop,seek,pagePrev,pageNext;
    std::vector<Rect> rows;
    bool ok()const{return w>0&&h>0;}
};
UI measure() {
    UI p;
    const auto surface=pl::modmenu::getHudSurfaceSize();
    p.w=surface.width;p.h=surface.height;
    if(!p.ok())return p;
    p.s=std::clamp(p.h/540.f,.65f,2.5f);
    const float s=p.s;
    p.mini={18*s,p.h-83*s,std::min(335*s,p.w*.4f),64*s};
    p.outer={p.w*.05f,p.h*.07f,p.w*.90f,p.h*.86f};
    const float contentY=p.outer.y+63*s;
    const float contentH=p.outer.h-76*s;
    const float contentX=p.outer.x+22*s;
    const float contentW=p.outer.w-44*s;
    p.left={contentX,contentY,contentW*.445f-6*s,contentH};
    p.right={contentX+contentW*.445f+6*s,contentY,contentW*.555f-6*s,contentH};
    p.close={p.outer.x+p.outer.w-51*s,p.outer.y+12*s,33*s,34*s};
    p.searchBox={p.right.x+12*s,p.right.y+12*s,p.right.w-112*s,42*s};
    p.searchGo={p.right.x+p.right.w-91*s,p.right.y+12*s,79*s,42*s};
    const float b=p.left.y+p.left.h-91*s, center=p.left.x+p.left.w*.5f;
    p.stop={p.left.x+10*s,b,39*s,52*s};
    p.prev={center-102*s,b,53*s,52*s};
    p.play={center-40*s,b,80*s,52*s};
    p.next={center+49*s,b,53*s,52*s};
    p.seek={p.left.x+20*s,b-54*s,p.left.w-40*s,18*s};
    float rowY=p.right.y+80*s;
    float rowsBottom=p.right.y+p.right.h-57*s;
    float rowH=std::min(53*s,std::max(28*s,(rowsBottom-rowY)/6));
    for(int i=0;i<6;i++)p.rows.push_back({p.right.x+12*s,rowY+i*rowH,p.right.w-24*s,rowH-3*s});
    p.pagePrev={p.right.x+p.right.w*.45f,p.right.y+p.right.h-48*s,39*s,35*s};
    p.pageNext={p.right.x+p.right.w*.45f+48*s,p.right.y+p.right.h-48*s,39*s,35*s};
    return p;
}
constexpr uint32_t C_BG=0xEA0C1522U;
constexpr uint32_t C_PANEL=0xEC1B2A3CU;
constexpr uint32_t C_BORDER=0xFF455D73U;
constexpr uint32_t C_TEXT=0xFFFFFFFFU;
constexpr uint32_t C_DIM=0xFFB6C5D6U;
constexpr uint32_t C_MINT=0xFF7DE2D4U;

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
    std::string info_="Tap to search songs", lyric_, apiKey_;
    int selected_=-1,page_=0,volume_=75,elapsed_=0,total_=0;
    bool open_=false,typing_=false,hud_=true,lyricsOn_=true,playing_=false,shutdown_=false;
    std::atomic<bool> enabled_{false};
public:
    LeviMusic():self_(*ll::mod::NativeMod::current()){}
    static LeviMusic& instance(){static LeviMusic mod;return mod;}
    bool load(){
        vm_=self_.getJavaVM();
        __android_log_print(ANDROID_LOG_INFO,TAG,"LeviMusic v0.1.2 loaded, vm=%p",(void*)vm_);
        return vm_!=nullptr;
    }
    bool enable(){
        {
            std::lock_guard lock(mu_);
            shutdown_=false;open_=false;typing_=false;
        }
        bool ok=pl::modmenu::ModuleBuilder(MODULE,"LeviMusic Online")
            .modId(self_.getId()).defaultEnabled(true).hideInHudEditor(true)
            .description("Touch the LeviMusic mini-player at the bottom left to open the full panel.")
            .config("volume","Volume",pl::modmenu::ConfigType::SliderInt,"75","0","100")
            .config("apiKey","Audius API Key (optional)",pl::modmenu::ConfigType::Text,"")
            .config("hud","Show compact HUD",pl::modmenu::ConfigType::Toggle,"true")
            .config("lyrics","Enable lyrics",pl::modmenu::ConfigType::Toggle,"true")
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
        std::lock_guard lock(mu_);
        if(k=="apiKey")apiKey_=std::string(v).substr(0,200);
        else if(k=="hud")hud_=v=="1"||v=="true";
        else if(k=="lyrics")lyricsOn_=v=="1"||v=="true";
        else if(k=="volume"){
            try{volume_=std::clamp(std::stoi(std::string(v)),0,100);}catch(...){}
            jobs_.push_back({Action::SetVolume,{}});cv_.notify_one();
        }
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
        UI l=measure();if(!l.ok())return false;
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
        if(l.close.contains(e.x,e.y)||!l.outer.contains(e.x,e.y)){
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
        UI l=measure();if(!l.ok())return;
        std::string name,artist,info,query,lyric;
        std::vector<Track> tracks;
        int current,page,elapsed,total,volume;
        bool open,showHud,playing,typing;
        {
            std::lock_guard lock(mu_);
            name=title_;artist=artist_;info=info_;query=query_;lyric=lyricsOn_?lyric_:"";
            tracks=results_;current=selected_;page=page_;elapsed=elapsed_;total=total_;
            volume=volume_;open=open_;showHud=hud_;playing=playing_;typing=typing_;
        }
        std::vector<pl::modmenu::DrawCommand> out;
        const float u=l.s;
        auto rectangle=[&](Rect r,uint32_t color){
            pl::modmenu::DrawCommand c;c.type=pl::modmenu::DrawCommandType::RectFilled;
            c.x=r.x;c.y=r.y;c.w=r.w;c.h=r.h;c.x3=8*u;c.color=color;out.push_back(std::move(c));
        };
        auto text=[&](float x,float y,std::string v,float size,uint32_t color,size_t max=60){
            pl::modmenu::DrawCommand c;c.type=pl::modmenu::DrawCommandType::Text;
            c.x=x;c.y=y;c.size=size;c.color=color;c.text=shorten(v,max);out.push_back(std::move(c));
        };
        auto circle=[&](float x,float y,float radius,uint32_t color){
            pl::modmenu::DrawCommand c;c.type=pl::modmenu::DrawCommandType::CircleFilled;
            c.x=x;c.y=y;c.size=radius;c.color=color;out.push_back(std::move(c));
        };
        if(!open){
            if(showHud){
                rectangle({l.mini.x-2*u,l.mini.y-2*u,l.mini.w+4*u,l.mini.h+4*u},C_BORDER);
                rectangle(l.mini,C_BG);
                rectangle({l.mini.x,l.mini.y,l.mini.w,4*u},C_MINT);
                circle(l.mini.x+30*u,l.mini.y+33*u,18*u,0xFF3E5368U);
                circle(l.mini.x+30*u,l.mini.y+33*u,7*u,C_MINT);
                text(l.mini.x+58*u,l.mini.y+23*u,name,15*u,C_TEXT,42);
                text(l.mini.x+58*u,l.mini.y+44*u,artist+"  •  Tap to open",11*u,C_DIM,51);
                if(total>0)rectangle({l.mini.x,l.mini.y+l.mini.h-3*u,
                    l.mini.w*std::clamp(float(elapsed)/total,0.f,1.f),3*u},C_MINT);
            }
            pl::modmenu::submitDrawCommands(MODULE,out);
            return;
        }
        rectangle({l.outer.x-2*u,l.outer.y-2*u,l.outer.w+4*u,l.outer.h+4*u},C_BORDER);
        rectangle(l.outer,C_BG);
        rectangle({l.outer.x,l.outer.y,5*u,l.outer.h},C_MINT);
        text(l.outer.x+24*u,l.outer.y+37*u,"LEVI MUSIC",22*u,C_TEXT);
        text(l.outer.x+211*u,l.outer.y+35*u,"ONLINE PLAYER • v0.1.2",12*u,C_MINT);
        rectangle(l.close,0xFF344558U);
        text(l.close.x+9*u,l.close.y+25*u,"X",20*u,C_TEXT);
        rectangle(l.left,C_PANEL);
        rectangle(l.right,C_PANEL);
        const float cx=l.left.x+l.left.w*.5f;
        const float discY=l.left.y+std::min(84*u,l.left.h*.22f);
        circle(cx,discY,58*u,0xFF40536CU);
        circle(cx,discY,48*u,0xFF142237U);
        circle(cx,discY,34*u,0xFF486074U);
        circle(cx,discY,10*u,C_MINT);
        const float ty=l.left.y+std::min(174*u,l.left.h*.44f);
        text(l.left.x+16*u,ty,name,18*u,C_TEXT,50);
        text(l.left.x+16*u,ty+26*u,artist,13*u,C_MINT,50);
        text(l.left.x+16*u,ty+46*u,info,11*u,C_DIM,56);
        if(!lyric.empty())text(l.left.x+16*u,ty+67*u,lyric,12*u,C_DIM,55);
        rectangle(l.seek,0xFF35465CU);
        if(total>0)rectangle({l.seek.x,l.seek.y,
            l.seek.w*std::clamp(float(elapsed)/total,0.f,1.f),l.seek.h},C_MINT);
        text(l.seek.x,l.seek.y+l.seek.h+17*u,clockText(elapsed),12*u,C_DIM);
        text(l.seek.x+l.seek.w-58*u,l.seek.y+l.seek.h+17*u,clockText(total),12*u,C_DIM);
        rectangle(l.stop,0xFF3A5268U);text(l.stop.x+8*u,l.stop.y+33*u,"[]",15*u,C_TEXT);
        rectangle(l.prev,0xFF3A5268U);text(l.prev.x+7*u,l.prev.y+33*u,"|<<",17*u,C_TEXT);
        rectangle(l.play,C_MINT);text(l.play.x+12*u,l.play.y+33*u,playing?"PAUSE":"PLAY",15*u,0xFF10232AU);
        rectangle(l.next,0xFF3A5268U);text(l.next.x+8*u,l.next.y+33*u,">>|",17*u,C_TEXT);
        text(l.left.x+12*u,l.left.y+l.left.h-15*u,"Volume "+std::to_string(volume)+"%   •   Audius",11*u,C_DIM);
        rectangle(l.searchBox,0xFF0B1929U);
        text(l.searchBox.x+12*u,l.searchBox.y+28*u,(typing?"_ ":"")+query,14*u,typing?C_MINT:C_TEXT,43);
        rectangle(l.searchGo,C_MINT);text(l.searchGo.x+8*u,l.searchGo.y+28*u,"SEARCH",13*u,0xFF0B2730U);
        text(l.right.x+14*u,l.right.y+75*u,"SEARCH RESULTS   /   "+std::to_string(tracks.size()),12*u,C_DIM);
        for(size_t i=0;i<l.rows.size();i++){
            size_t n=static_cast<size_t>(page*6)+i;
            if(n>=tracks.size())break;
            Rect r=l.rows[i];
            rectangle(r,static_cast<int>(n)==current?0xFF2D5261U:0xFF2A394BU);
            text(r.x+10*u,r.y+std::min(22*u,r.h*.47f),
                std::to_string(n+1)+". "+tracks[n].title,13*u,C_TEXT,42);
            text(r.x+10*u,r.y+std::min(39*u,r.h*.83f),tracks[n].artist,11*u,C_DIM,48);
        }
        rectangle(l.pagePrev,0xFF34495DU);rectangle(l.pageNext,0xFF34495DU);
        text(l.pagePrev.x+13*u,l.pagePrev.y+25*u,"<",18*u,C_TEXT);
        text(l.pageNext.x+13*u,l.pageNext.y+25*u,">",18*u,C_TEXT);
        text(l.right.x+12*u,l.right.y+l.right.h-23*u,
            std::to_string(page+1)+" / "+std::to_string(std::max<size_t>(1,(tracks.size()+5)/6)),12*u,C_DIM);
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
            std::vector<Lyric> found=searchLyrics(env,tr);
            {
                std::lock_guard lock(mu_);
                lyrics_=std::move(found);
                lyric_=lyrics_.empty()?"No synchronized lyrics":"...";
            }
        };
        auto lastPaint=std::chrono::steady_clock::now();
        while(true){
            Job job{};bool hasJob=false;
            {
                std::unique_lock lock(mu_);
                cv_.wait_for(lock,std::chrono::milliseconds(350),
                    [this]{return shutdown_||!jobs_.empty();});
                if(shutdown_)break;
                if(!jobs_.empty()){
                    job=std::move(jobs_.front());
                    jobs_.pop_front();hasJob=true;
                }
            }
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
                        lyric_=line.line;
                    }
                }
            }
            const auto now=std::chrono::steady_clock::now();
            if(now-lastPaint>std::chrono::milliseconds(400)){
                paint();lastPaint=now;
            }
        }
        player.release(env);
    }
};
} // namespace
PL_REGISTER_MOD(LeviMusic, LeviMusic::instance())
