// LeviMusic for LeviLauncher Android: Bridge and Aurelis-powered Standalone.
#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>
#include <pl/Input.hpp>
#include "LucideIcons.hpp"
#include "WorldLyrics3D.hpp"
#include "WordTiming.hpp"
#include "OfflineAudio.hpp"
#include "LyricText.hpp"
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
#include <map>
#include <iomanip>
#include <set>
#include <random>

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
    std::string imageId;
};
bool localTrack(const Track& track){return track.id.rfind("offline:",0)==0;}
bool allowedAudio(const std::filesystem::path& path){
    auto ext=path.extension().string();
    std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return std::tolower(c);});
    return ext==".mp3"||ext==".m4a"||ext==".aac"||ext==".ogg"||
        ext==".opus"||ext==".flac"||ext==".wav"||ext==".mp4"||ext==".webm";
}
std::vector<Track> offlineTracks(const std::filesystem::path& folder,const std::string& query={}){
    std::vector<Track> found;
    try{
        std::filesystem::create_directories(folder);
        for(const auto& entry:std::filesystem::directory_iterator(folder)){
            if(!entry.is_regular_file()||!allowedAudio(entry.path())||
                entry.file_size()==0||entry.file_size()>200'000'000)continue;
            auto name=entry.path().filename().string();
            if(name.size()>200||name.find('/')!=std::string::npos)continue;
            Track track{"offline:"+name,entry.path().stem().string(),"Local music",""};
            auto meta=entry.path();meta.replace_extension(".meta");
            if(std::filesystem::is_regular_file(meta)&&!std::filesystem::is_symlink(meta)&&
               std::filesystem::file_size(meta)<4000){
                std::ifstream source(meta);
                std::string title,artist,art;
                if(source>>std::quoted(title)>>std::quoted(artist)>>std::quoted(art)){
                    if(!title.empty())track.title=title;
                    if(!artist.empty())track.artist=artist;
                    if(art.rfind("https://",0)==0)track.art=art;
                }
            }
            if(!query.empty()&&query!="offline"&&
               track.title.find(query)==std::string::npos&&
               track.artist.find(query)==std::string::npos&&
               name.find(query)==std::string::npos)continue;
            found.push_back(std::move(track));
            if(found.size()>=300)break;
        }
    }catch(...){}
    std::sort(found.begin(),found.end(),[](const Track& a,const Track& b){return a.title<b.title;});
    return found;
}
struct Lyric {
    int ms = 0;
    std::string line;
    std::string romanized;
    std::string romanizedWorld;
    int endMs = 0;
    std::vector<lm_timing::WordCue> words;
};

enum class Action { Search, PlayPause, Next, Previous, Stop, SetVolume, Seek, Select, Pair, PlaylistList, PlaylistOpen, PlaylistPlay, PlaylistCreate, PlaylistRename, PlaylistDelete, PlaylistAdd, PlaylistRemove, OfflineSave, OpenBrowser, QueueRefresh, QueuePlay, QueueRemove, QueueAdd, RepeatCycle, ShuffleToggle };
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

// Rasterize text off the game render thread. Android Paint supplies the system
// font fallback (including complex shaping) before we turn coverage into quads.
bool rasterizeWorldText(JNIEnv* env,std::string_view line,int font,
                        std::span<const lm_timing::WordCue> words,
                        std::vector<lm_world3d::GlyphMesh>& result){
    result.clear();
    const auto parts=lm_world3d::unicodeParts(line,words);
    if(parts.empty())return false;
    jclass paintClass=env->FindClass("android/graphics/Paint");
    jclass faceClass=env->FindClass("android/graphics/Typeface");
    jclass canvasClass=env->FindClass("android/graphics/Canvas");
    jclass bitmapClass=env->FindClass("android/graphics/Bitmap");
    jclass configClass=env->FindClass("android/graphics/Bitmap$Config");
    if(clearException(env,"world font classes")||!paintClass||!faceClass||!canvasClass||
       !bitmapClass||!configClass)return false;
    auto paintCtor=env->GetMethodID(paintClass,"<init>","(I)V");
    auto setSize=env->GetMethodID(paintClass,"setTextSize","(F)V");
    auto setScale=env->GetMethodID(paintClass,"setTextScaleX","(F)V");
    auto setSkew=env->GetMethodID(paintClass,"setTextSkewX","(F)V");
    auto setBold=env->GetMethodID(paintClass,"setFakeBoldText","(Z)V");
    auto setFace=env->GetMethodID(paintClass,"setTypeface",
        "(Landroid/graphics/Typeface;)Landroid/graphics/Typeface;");
    auto setColor=env->GetMethodID(paintClass,"setColor","(I)V");
    auto measure=env->GetMethodID(paintClass,"measureText","(Ljava/lang/String;)F");
    auto hasGlyph=env->GetMethodID(paintClass,"hasGlyph","(Ljava/lang/String;)Z");
    auto faceCreate=env->GetStaticMethodID(faceClass,"create",
        "(Ljava/lang/String;I)Landroid/graphics/Typeface;");
    auto bitmapCreate=env->GetStaticMethodID(bitmapClass,"createBitmap",
        "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;");
    auto getPixels=env->GetMethodID(bitmapClass,"getPixels","([IIIIIII)V");
    auto recycle=env->GetMethodID(bitmapClass,"recycle","()V");
    auto canvasCtor=env->GetMethodID(canvasClass,"<init>","(Landroid/graphics/Bitmap;)V");
    auto draw=env->GetMethodID(canvasClass,"drawText",
        "(Ljava/lang/String;FFLandroid/graphics/Paint;)V");
    auto configField=env->GetStaticFieldID(configClass,"ARGB_8888",
        "Landroid/graphics/Bitmap$Config;");
    if(clearException(env,"world font methods")||!paintCtor||!setSize||!setScale||!setSkew||!setBold||!setFace||!setColor||
       !measure||!hasGlyph||!faceCreate||!bitmapCreate||!getPixels||!recycle||
       !canvasCtor||!draw||!configField)return false;
    jobject config=env->GetStaticObjectField(configClass,configField);
    jobject paint=env->NewObject(paintClass,paintCtor,3);
    static constexpr const char* families[]={"sans-serif","serif","monospace","sans-serif-condensed"};
    jstring family=toJava(env,families[std::clamp(font,0,3)]);
    jobject face=family?env->CallStaticObjectMethod(faceClass,faceCreate,family,0):nullptr;
    if(paint&&face){
        env->CallVoidMethod(paint,setSize,48.f);
        // Android may choose the same CJK fallback for several family names.
        // Give every choice a visible weight, slant, or width of its own.
        env->CallVoidMethod(paint,setScale,font==3?.82f:font==2?1.08f:1.f);
        env->CallVoidMethod(paint,setSkew,font==1?-.12f:0.f);
        env->CallVoidMethod(paint,setBold,font==2?JNI_TRUE:JNI_FALSE);
        env->CallVoidMethod(paint,setColor,jint(0xFFFFFFFFU));
        jobject old=env->CallObjectMethod(paint,setFace,face);
        if(old)env->DeleteLocalRef(old);
    }
    bool ok=config&&paint&&face&&!clearException(env,"world font setup");
    for(const auto& part:parts){
        if(!ok)break;
        jstring text=toJava(env,part);
        if(!text){ok=false;break;}
        // Missing font glyphs should be handled by romanized fallback, not boxes.
        if(!env->CallBooleanMethod(paint,hasGlyph,text))ok=false;
        const float measured=ok?env->CallFloatMethod(paint,measure,text):0.f;
        const int width=std::clamp(int(std::ceil(measured))+14,14,600);
        constexpr int height=68;
        jobject bitmap=ok?env->CallStaticObjectMethod(bitmapClass,bitmapCreate,
            width,height,config):nullptr;
        jobject canvas=bitmap?env->NewObject(canvasClass,canvasCtor,bitmap):nullptr;
        jintArray pixels=canvas?env->NewIntArray(width*height):nullptr;
        if(!bitmap||!canvas||!pixels||clearException(env,"world font bitmap"))ok=false;
        lm_world3d::GlyphMesh mesh;
        if(ok){
            env->CallVoidMethod(canvas,draw,text,6.f,56.f,paint);
            env->CallVoidMethod(bitmap,getPixels,pixels,0,width,0,0,width,height);
            if(clearException(env,"world font draw"))ok=false;
            else {
                std::vector<jint> rgba(static_cast<size_t>(width*height));
                env->GetIntArrayRegion(pixels,0,width*height,rgba.data());
                if(clearException(env,"world font coverage"))ok=false;
                else {
                    std::vector<uint8_t> alpha(rgba.size());
                    for(size_t i=0;i<rgba.size();++i)alpha[i]=uint8_t(uint32_t(rgba[i])>>24);
                    const bool complex=std::any_of(part.begin(),part.end(),
                        [](unsigned char ch){return ch>=128;});
                    const int sampleStep=complex&&lm_timing::utf8Length(part)<=2?1:2;
                    mesh=lm_world3d::rasterMesh(alpha,width,height,56,sampleStep);
                    if(mesh.pixels.empty())ok=false;
                }
            }
        }
        if(bitmap)env->CallVoidMethod(bitmap,recycle);
        if(pixels)env->DeleteLocalRef(pixels);
        if(canvas)env->DeleteLocalRef(canvas);
        if(bitmap)env->DeleteLocalRef(bitmap);
        env->DeleteLocalRef(text);
        if(ok)result.push_back(std::move(mesh));
    }
    if(face)env->DeleteLocalRef(face);
    if(family)env->DeleteLocalRef(family);
    if(paint)env->DeleteLocalRef(paint);
    if(config)env->DeleteLocalRef(config);
    env->DeleteLocalRef(configClass);env->DeleteLocalRef(bitmapClass);
    env->DeleteLocalRef(canvasClass);env->DeleteLocalRef(faceClass);
    env->DeleteLocalRef(paintClass);
    if(clearException(env,"world font cleanup"))ok=false;
    if(!ok||result.size()!=parts.size())result.clear();
    return !result.empty();
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
std::string getHttp(JNIEnv* env, const std::string& url,
                    const std::string& postBody={}) {
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
            if (readTimeout) env->CallVoidMethod(conn, readTimeout,
                url.find("aurelis-symphony.go.altare.global")!=std::string::npos?30000:8000);
            if(!postBody.empty()&&postBody.size()<24000){
                jclass http=env->FindClass("java/net/HttpURLConnection");
                auto method=http?env->GetMethodID(http,"setRequestMethod","(Ljava/lang/String;)V"):nullptr;
                auto enable=env->GetMethodID(connClass,"setDoOutput","(Z)V");
                auto header=env->GetMethodID(connClass,"setRequestProperty",
                    "(Ljava/lang/String;Ljava/lang/String;)V");
                auto output=env->GetMethodID(connClass,"getOutputStream","()Ljava/io/OutputStream;");
                jstring post=env->NewStringUTF("POST");
                jstring name=env->NewStringUTF("Content-Type");
                jstring mime=env->NewStringUTF("application/json; charset=utf-8");
                if(method)env->CallVoidMethod(conn,method,post);
                if(enable)env->CallVoidMethod(conn,enable,JNI_TRUE);
                if(header)env->CallVoidMethod(conn,header,name,mime);
                if(output){
                    jobject streamOut=env->CallObjectMethod(conn,output);
                    if(streamOut&&!clearException(env,"Aurelis POST stream")){
                        jclass outClass=env->FindClass("java/io/OutputStream");
                        auto write=outClass?env->GetMethodID(outClass,"write","([B)V"):nullptr;
                        auto close=outClass?env->GetMethodID(outClass,"close","()V"):nullptr;
                        jbyteArray data=env->NewByteArray(static_cast<jsize>(postBody.size()));
                        if(data){env->SetByteArrayRegion(data,0,static_cast<jsize>(postBody.size()),
                            reinterpret_cast<const jbyte*>(postBody.data()));
                            if(write)env->CallVoidMethod(streamOut,write,data);
                            env->DeleteLocalRef(data);
                        }
                        if(close)env->CallVoidMethod(streamOut,close);
                        if(outClass)env->DeleteLocalRef(outClass);
                        env->DeleteLocalRef(streamOut);
                    }
                }
                clearException(env,"Aurelis POST request");
                if(post)env->DeleteLocalRef(post);
                if(name)env->DeleteLocalRef(name);
                if(mime)env->DeleteLocalRef(mime);
                if(http)env->DeleteLocalRef(http);
            }
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
            const size_t limit=url.find("youtube.com/playlist?")!=std::string::npos?
                4'000'000:750'000;
            while (content.size() < limit) {
                jint n = env->CallIntMethod(stream, read, bytes);
                if (clearException(env, "read HTTPS") || n <= 0) break;
                if (content.size() + static_cast<size_t>(n) > limit) break;
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

// Stream directly to disk so a song never occupies the game process's heap.
std::filesystem::path downloadAudio(JNIEnv* env,const std::string& url,
                                    const std::filesystem::path& folder,
                                    const std::string& id){
    if(url.rfind("https://",0)!=0||id.size()<4||id.size()>80||
       !std::all_of(id.begin(),id.end(),[](unsigned char c){
           return std::isalnum(c)||c=='_'||c=='-';}))return {};
    try{std::filesystem::create_directories(folder);}catch(...){return {};}
    jclass urlClass=env->FindClass("java/net/URL");
    auto ctor=urlClass?env->GetMethodID(urlClass,"<init>","(Ljava/lang/String;)V"):nullptr;
    auto open=urlClass?env->GetMethodID(urlClass,"openConnection","()Ljava/net/URLConnection;"):nullptr;
    auto jurl=toJava(env,url);
    jobject address=ctor&&jurl?env->NewObject(urlClass,ctor,jurl):nullptr;
    jobject connection=address&&open?env->CallObjectMethod(address,open):nullptr;
    if(clearException(env,"offline connection"))connection=nullptr;
    std::filesystem::path saved;
    if(connection){
        jclass cl=env->FindClass("java/net/URLConnection");
        auto timeout=cl?env->GetMethodID(cl,"setConnectTimeout","(I)V"):nullptr;
        auto readTimeout=cl?env->GetMethodID(cl,"setReadTimeout","(I)V"):nullptr;
        auto contentType=cl?env->GetMethodID(cl,"getContentType","()Ljava/lang/String;"):nullptr;
        auto streamMethod=cl?env->GetMethodID(cl,"getInputStream","()Ljava/io/InputStream;"):nullptr;
        if(timeout)env->CallVoidMethod(connection,timeout,15000);
        if(readTimeout)env->CallVoidMethod(connection,readTimeout,30000);
        jstring type=contentType?(jstring)env->CallObjectMethod(connection,contentType):nullptr;
        std::string mime=type?fromJava(env,type):"";
        if(type)env->DeleteLocalRef(type);
        std::transform(mime.begin(),mime.end(),mime.begin(),[](unsigned char c){return std::tolower(c);});
        jobject stream=streamMethod?env->CallObjectMethod(connection,streamMethod):nullptr;
        if(clearException(env,"offline stream"))stream=nullptr;
        auto temporary=folder/(id+".part");
        bool valid=false;std::string signature;
        if(stream){
            jclass sc=env->FindClass("java/io/InputStream");
            auto read=sc?env->GetMethodID(sc,"read","([B)I"):nullptr;
            auto close=sc?env->GetMethodID(sc,"close","()V"):nullptr;
            jbyteArray buffer=env->NewByteArray(16384);
            std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
            size_t total=0;bool failed=!file||!read||!buffer;
            while(!failed){
                jint n=env->CallIntMethod(stream,read,buffer);
                if(clearException(env,"offline read")){failed=true;break;}
                if(n<0)break;
                if(n==0){failed=true;break;}
                total+=static_cast<size_t>(n);
                if(total>150'000'000){failed=true;break;}
                char bytes[16384];env->GetByteArrayRegion(buffer,0,n,reinterpret_cast<jbyte*>(bytes));
                if(clearException(env,"offline copy")){failed=true;break;}
                if(signature.empty())signature.assign(bytes,std::min<int>(n,32));
                file.write(bytes,n);
                if(!file){failed=true;break;}
            }
            file.close();
            if(close)env->CallVoidMethod(stream,close);
            clearException(env,"offline close");
            if(buffer)env->DeleteLocalRef(buffer);
            if(sc)env->DeleteLocalRef(sc);
            valid=!failed&&total>0;
            env->DeleteLocalRef(stream);
        }
        auto ext=lm_offline::extension(signature,mime);
        valid=valid&&!ext.empty();
        __android_log_print(ANDROID_LOG_INFO,TAG,"Offline audio type %s, format %s, valid %d",
            mime.c_str(),ext.c_str(),int(valid));
        if(valid){
            try{auto target=folder/(id+ext);
                std::filesystem::rename(temporary,target);saved=std::move(target);
            }catch(...){}
        }
        if(saved.empty()){try{std::filesystem::remove(temporary);}catch(...){}}
        if(cl)env->DeleteLocalRef(cl);
        env->DeleteLocalRef(connection);
    }
    if(address)env->DeleteLocalRef(address);
    if(jurl)env->DeleteLocalRef(jurl);
    if(urlClass)env->DeleteLocalRef(urlClass);
    return saved;
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


std::vector<Lyric> parseLrc(const std::string& raw);
#if defined(LEVI_STANDALONE)
// Standalone uses the owner's hosted Aurelis Symphony API by default.
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
[[maybe_unused]] std::vector<Track> searchChruPipe(JNIEnv* env,const std::string& server,
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
[[maybe_unused]] std::string streamChruPipe(JNIEnv* env,const std::string& server,
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
struct ImportedPlaylist {std::string name;std::vector<Track> tracks;};
bool youtubeUrl(const std::string& url){
    return url.rfind("https://www.youtube.com/",0)==0||url.rfind("https://youtube.com/",0)==0||
           url.rfind("https://m.youtube.com/",0)==0||url.rfind("https://music.youtube.com/",0)==0||
           url.rfind("https://youtu.be/",0)==0;
}
std::string youtubeVideoId(const std::string& url){
    if(!youtubeUrl(url))return {};
    size_t at=url.find("v=");size_t skip=2;
    if(at==std::string::npos){at=url.find("youtu.be/");skip=9;}
    if(at==std::string::npos){at=url.find("/shorts/");skip=8;}
    if(at==std::string::npos){at=url.find("/live/");skip=6;}
    if(at==std::string::npos)return {};
    auto id=url.substr(at+skip,11);
    if(id.size()!=11||!std::all_of(id.begin(),id.end(),[](unsigned char c){
        return std::isalnum(c)||c=='_'||c=='-';}))return {};
    return id;
}
Track resolveYoutubeTrack(JNIEnv* env,const std::string& url){
    const auto id=youtubeVideoId(url);if(id.empty())return {};
    Track track{id,id,"YouTube","https://i.ytimg.com/vi/"+id+"/hqdefault.jpg"};
    auto root=makeJson(env,getHttp(env,"https://www.youtube.com/oembed?format=json&url="+
        encode(env,"https://www.youtube.com/watch?v="+id)));
    if(root){
        auto title=optString(env,root,"title"),artist=optString(env,root,"author_name");
        auto art=optString(env,root,"thumbnail_url");
        if(!title.empty())track.title=title;if(!artist.empty())track.artist=artist;
        if(art.rfind("https://",0)==0)track.art=art;
        env->DeleteLocalRef(root);
    }
    return track;
}
ImportedPlaylist importYoutubePlaylist(JNIEnv* env,const std::string& url){
    ImportedPlaylist imported;
    if(url.size()>512||!youtubeUrl(url))return imported;
    const auto pos=url.find("list=");
    if(pos==std::string::npos)return imported;
    auto id=url.substr(pos+5);id=id.substr(0,id.find('&'));
    if(id.size()<10||id.size()>100||!std::all_of(id.begin(),id.end(),[](unsigned char c){
        return std::isalnum(c)||c=='_'||c=='-';}))return imported;
    imported.name="YouTube "+id.substr(0,16);
    const auto canonical="https://www.youtube.com/playlist?list="+id;
    auto root=makeJson(env,getHttp(env,
        "https://aurelis-symphony.go.altare.global/api/v1/playlists/import?url="+
        encode(env,canonical)));
    if(root){
        auto info=optObject(env,root,"playlist");
        if(info){auto name=optString(env,info,"title");
            if(!name.empty())imported.name=name;env->DeleteLocalRef(info);}
        auto entries=optArray(env,root,"tracks");
        if(entries){
            jclass cls=env->FindClass("org/json/JSONArray");
            auto length=cls?env->GetMethodID(cls,"length","()I"):nullptr;
            auto item=cls?env->GetMethodID(cls,"optJSONObject","(I)Lorg/json/JSONObject;"):nullptr;
            int count=length?env->CallIntMethod(entries,length):0;
            if(clearException(env,"playlist length"))count=0;
            std::set<std::string> seen;
            for(int i=0;i<std::min(count,300);++i){
                auto row=item?env->CallObjectMethod(entries,item,i):nullptr;
                if(!row||clearException(env,"playlist row"))continue;
                auto video=optString(env,row,"id");
                if(video.size()!=11){
                    auto link=optString(env,row,"url");auto at=link.find("v=");
                    if(at!=std::string::npos)video=link.substr(at+2,11);
                    else if((at=link.find("youtu.be/"))!=std::string::npos)
                        video=link.substr(at+9,11);
                }
                if(video.size()==11&&std::all_of(video.begin(),video.end(),[](unsigned char c){
                    return std::isalnum(c)||c=='_'||c=='-';})&&seen.insert(video).second){
                    auto title=optString(env,row,"title");
                    if(title.empty())title=optString(env,row,"name");
                    auto artist=optString(env,row,"artist");
                    if(artist.empty())artist=optString(env,row,"author");
                    auto art=optString(env,row,"artwork");
                    if(art.empty())art=optString(env,row,"thumbnail");
                    imported.tracks.push_back({video,title.empty()?video:title,
                        artist.empty()?"YouTube":artist,art.empty()?
                        "https://i.ytimg.com/vi/"+video+"/hqdefault.jpg":art});
                }
                env->DeleteLocalRef(row);
            }
            if(cls)env->DeleteLocalRef(cls);
            env->DeleteLocalRef(entries);
        }
        env->DeleteLocalRef(root);
        if(!imported.tracks.empty())return imported;
    }
    auto page=getHttp(env,"https://www.youtube.com/playlist?list="+id);
    if(page.empty())return imported;
    std::set<std::string> seen;
    const std::string marker="\"playlistVideoRenderer\":{";
    size_t at=0;
    while((at=page.find(marker,at))!=std::string::npos&&imported.tracks.size()<300){
        auto next=page.find(marker,at+marker.size());
        std::string_view block(page.data()+at,std::min<size_t>(
            std::min(next==std::string::npos?page.size():next,page.size())-at,5000));
        auto v=block.find("\"videoId\":\"");
        if(v!=std::string_view::npos){
            v+=11;auto end=block.find('"',v);
            if(end!=std::string_view::npos){
                std::string video(block.substr(v,end-v));
                if(video.size()==11&&seen.insert(video).second){
                    std::string title=video,artist="YouTube";
                    auto label=block.find("\"title\":");
                    if(label!=std::string_view::npos){
                        auto text=block.find("\"text\":\"",label);
                        if(text!=std::string_view::npos){
                            text+=8;auto stop=block.find('"',text);
                            if(stop!=std::string_view::npos&&stop-text<400)
                                title=std::string(block.substr(text,stop-text));
                        }
                    }
                    imported.tracks.push_back({video,title,artist,
                        "https://i.ytimg.com/vi/"+video+"/hqdefault.jpg"});
                }
            }
        }
        at+=marker.size();
    }
    return imported;
}
std::vector<Track> searchAurelis(JNIEnv* env,const std::string& server,
                                 const std::string& query,const std::string& artist={}){
    std::vector<Track> tracks;
    auto path=artist.empty()?"/api/v1/search?provider=youtube&q="+encode(env,query)+"&limit=24":
        "/api/v1/recommendations?track="+encode(env,query)+"&artist="+
        encode(env,artist)+"&limit=6";
    auto body=chruRequest(env,server,path);
    jobject root=makeJson(env,body);
    if(!root)return tracks;
    jobject array=optArray(env,root,"tracks");
    if(array){
        jclass cls=env->FindClass("org/json/JSONArray");
        jmethodID length=cls?env->GetMethodID(cls,"length","()I"):nullptr;
        jmethodID item=cls?env->GetMethodID(cls,"optJSONObject","(I)Lorg/json/JSONObject;"):nullptr;
        int count=length?env->CallIntMethod(array,length):0;
        if(clearException(env,"Aurelis search length"))count=0;
        for(int i=0;i<std::min(count,24);++i){
            jobject obj=item?env->CallObjectMethod(array,item,i):nullptr;
            if(!obj||clearException(env,"Aurelis search item"))continue;
            Track tr;
            tr.id=optString(env,obj,"id");
            tr.title=optString(env,obj,"title");
            tr.artist=optString(env,obj,"artist");
            tr.art=optString(env,obj,"artwork");
            if(tr.id.size()>=4&&tr.id.size()<=80&&
               tr.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==std::string::npos
               &&!tr.title.empty())tracks.push_back(std::move(tr));
            env->DeleteLocalRef(obj);
        }
        if(cls)env->DeleteLocalRef(cls);
        env->DeleteLocalRef(array);
    }
    env->DeleteLocalRef(root);
    return tracks;
}
std::string aurelisStream(JNIEnv* env,const std::string& server,const std::string& id,
                          bool download=false){
    if(id.empty()||id.size()>80||id.find_first_not_of(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)return {};
    jobject root=makeJson(env,chruRequest(env,server,"/api/v2/playback/"+id));
    if(!root)return {};
    const auto relative=optString(env,root,"streamUrl");
    const auto probe=optString(env,root,"probeUrl");
    env->DeleteLocalRef(root);
    if(relative.rfind("/demo/",0)!=0&&relative.rfind("/api/v2/audio/",0)!=0)return {};
    if(relative.find('?')!=std::string::npos||relative.find('#')!=std::string::npos||
       relative.find("..")!=std::string::npos)return {};
    if(!download&&!probe.empty()){
        if(probe.rfind("/api/v2/audio/",0)!=0||probe.find("..")!=std::string::npos)return {};
        jobject status=makeJson(env,chruRequest(env,server,probe));
        if(!status)return {};
        bool available=optString(env,status,"ok")=="true";
        env->DeleteLocalRef(status);
        if(!available)return {};
    }
    std::string base=server;
    while(!base.empty()&&base.back()=='/')base.pop_back();
    return base+relative;
}
std::vector<Lyric> aurelisLyrics(JNIEnv* env,const std::string& server,
                                const Track& tr,int durationMs){
    std::string path="/api/v1/lyrics?track="+encode(env,tr.title)+
        "&artist="+encode(env,tr.artist)+"&duration="+
        std::to_string(std::max(0,durationMs)/1000);
    std::vector<Lyric> output;
    auto root=makeJson(env,chruRequest(env,server,path));
    if(!root)return output;
    auto result=optObject(env,root,"result");
    auto lines=optArray(env,result,"lines");
    if(lines){
        jclass cls=env->FindClass("org/json/JSONArray");
        auto size=cls?env->GetMethodID(cls,"length","()I"):nullptr;
        auto item=cls?env->GetMethodID(cls,"optJSONObject","(I)Lorg/json/JSONObject;"):nullptr;
        int n=size?env->CallIntMethod(lines,size):0;
        if(clearException(env,"Aurelis lyric length"))n=0;
        for(int i=0;i<std::min(n,350);++i){
            jobject row=item?env->CallObjectMethod(lines,item,i):nullptr;
            if(!row||clearException(env,"Aurelis lyric row"))continue;
            std::string line=optString(env,row,"text");
            std::string seconds=optString(env,row,"time");
            std::vector<lm_timing::WordCue> timedWords;
            auto timed=optArray(env,row,"words");
            if(timed){
                jclass wordClass=env->FindClass("org/json/JSONArray");
                auto wordCount=wordClass?env->GetMethodID(wordClass,"length","()I"):nullptr;
                auto wordAt=wordClass?env->GetMethodID(wordClass,"optJSONObject","(I)Lorg/json/JSONObject;"):nullptr;
                int count=wordCount?env->CallIntMethod(timed,wordCount):0;
                if(clearException(env,"Aurelis timed word count"))count=0;
                bool valid=count>0&&count<=100;
                int previous=-1;
                for(int w=0;valid&&w<count;++w){
                    jobject token=wordAt?env->CallObjectMethod(timed,wordAt,w):nullptr;
                    if(!token||clearException(env,"Aurelis timed word")){valid=false;break;}
                    const std::string text=optString(env,token,"text");
                    const std::string start=optString(env,token,"start");
                    const std::string end=optString(env,token,"end");
                    env->DeleteLocalRef(token);
                    try{
                        const double from=std::stod(start),to=std::stod(end);
                        if(!std::isfinite(from)||!std::isfinite(to)||from<0||
                           to>14400||to<=from){valid=false;break;}
                        const int fromMs=static_cast<int>(std::round(from*1000));
                        const int toMs=static_cast<int>(std::round(to*1000));
                        if(text.empty()||toMs<=fromMs||fromMs<previous){valid=false;break;}
                        timedWords.push_back({fromMs,toMs,text});
                        previous=fromMs;
                    }catch(...){valid=false;break;}
                }
                if(!valid)timedWords.clear();
                if(wordClass)env->DeleteLocalRef(wordClass);
                env->DeleteLocalRef(timed);
            }
            // Some enhanced LRC providers strip spaces around word timestamps.
            // Rebuild Latin word boundaries from the timed words when the line
            // itself has lost them; preserve CJK lines and already spaced text.
            if(line.find(' ')==std::string::npos&&timedWords.size()>=3&&
               std::all_of(line.begin(),line.end(),[](unsigned char c){return c<128;})){
                std::string spaced;
                int singleLetters=0;
                for(const auto& word:timedWords){
                    std::string part=word.text;
                    auto first=part.find_first_not_of(" \t\r\n");
                    if(first==part.npos)continue;
                    auto last=part.find_last_not_of(" \t\r\n");
                    part=part.substr(first,last-first+1);
                    if(part.size()==1&&std::isalpha(static_cast<unsigned char>(part[0])))++singleLetters;
                    if(!spaced.empty()&&std::string(",.!?;:)]}").find(part.front())==std::string::npos&&
                       spaced.back()!='\''&&spaced.back()!='-')spaced+=' ';
                    spaced+=part;
                }
                std::string compact;
                for(unsigned char ch:spaced)if(!std::isspace(ch))compact+=static_cast<char>(ch);
                if(singleLetters*2<static_cast<int>(timedWords.size())&&spaced.size()<1000&&
                   compact==line)line=std::move(spaced);
            }
            try{
                if(!line.empty()&&!seconds.empty()&&seconds!="null"){
                    double at=std::stod(seconds);
                    if(std::isfinite(at)&&at>=0&&at<14400&&
                       (output.empty()||at*1000>=output.back().ms)){
                        Lyric lyric;
                        lyric.ms=static_cast<int>(std::round(at*1000));
                        lyric.line=lm_lyric::normalize(line);
                        const std::string end=optString(env,row,"endTime");
                        if(!end.empty()&&end!="null"){
                            double until=std::stod(end);
                            if(std::isfinite(until)&&until>at&&until<14400)
                                lyric.endMs=static_cast<int>(std::round(until*1000));
                        }
                        if(!timedWords.empty()&&timedWords.front().startMs>=lyric.ms-500&&
                           (lyric.endMs<=0||timedWords.back().endMs<=lyric.endMs+500))
                            lyric.words=std::move(timedWords);
                        output.push_back(std::move(lyric));
                    }
                }
            }catch(...){}
            env->DeleteLocalRef(row);
        }
        if(cls)env->DeleteLocalRef(cls);
        env->DeleteLocalRef(lines);
    }
    if(result)env->DeleteLocalRef(result);
    env->DeleteLocalRef(root);
    return output;
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
            std::string line = lm_lyric::normalize(row.substr(end + 1));
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
                const bool titleMatch=a==b || (std::min(a.size(),b.size())>=4 &&
                    (a.find(b)!=std::string::npos || b.find(a)!=std::string::npos));
                if(!titleMatch){env->DeleteLocalRef(obj);continue;}
                score+=a==b?30000:9000;
            } else {env->DeleteLocalRef(obj);continue;}
            if(!songArtist.empty()&&!actualArtist.empty()){
                const auto a=lower(actualArtist),b=lower(songArtist);
                score += a==b?18000:(a.find(b)!=std::string::npos ||
                                      b.find(a)!=std::string::npos?5500:-8000);
            }
            if(trackDurationMs > 0) {
                try {
                    int seconds = static_cast<int>(std::stod(optString(env,obj,"duration")));
                    int delta = std::abs(seconds * 1000 - trackDurationMs);
                    score += 30000 - delta;
                    if(delta > 15000) {env->DeleteLocalRef(obj);continue;}
                } catch(...) { score = -100000; }
            }
            env->DeleteLocalRef(obj);
            if(!lrc.empty() && score>0){
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
             "&artist="+encode(env,tr.artist)+"&duration="+std::to_string(duration)+
             "&id="+encode(env,tr.id),bridgeKey);
        if(!response.empty()) {
            jobject root=makeJson(env,response);
            if(root){
                std::string raw=optString(env,root,"syncedLyrics");
                std::string romanized=optString(env,root,"romanizedLyrics");
                env->DeleteLocalRef(root);
                auto local=parseLrc(raw);
                auto reading=parseLrc(romanized);
                size_t i=0,j=0;
                while(i<local.size()&&j<reading.size()){
                    if(std::abs(local[i].ms-reading[j].ms)<=15){
                        local[i].romanized=reading[j].line;++i;++j;
                    }else if(local[i].ms<reading[j].ms)++i;
                    else ++j;
                }
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
    // Bridge readings from the Japanese dictionary take precedence over generic ICU.
    // Never overwrite the original line; fallback avoids reading kanji as Mandarin.
    for(auto& lyric:lines)if(lyric.romanized.empty()){
        bool han=false;
        for(size_t i=0;i+2<lyric.line.size();++i){
            const auto a=static_cast<unsigned char>(lyric.line[i]);
            if(a>=0xe4&&a<=0xe9)han=true;
        }
        if(!han){
            auto result=romanizeLine(env,lyric.line);
            if(result!=lyric.line)lyric.romanized=std::move(result);
        }
    }
    for(auto& lyric:lines)if(!lyric.romanized.empty())
        lyric.romanizedWorld=romanizeLine(env,lyric.romanized);
}
#if defined(LEVI_STANDALONE)
std::string jsonQuoted(std::string_view value){
    std::string out="\"";
    for(unsigned char ch:value){
        if(ch=='\"'||ch=='\\'){out+='\\';out+=static_cast<char>(ch);}
        else if(ch=='\n')out+="\\n";
        else if(ch=='\r')out+="\\r";
        else if(ch=='\t')out+="\\t";
        else if(ch<32)out+=' ';
        else out+=static_cast<char>(ch);
    }
    return out+'\"';
}
void aurelisRomanize(JNIEnv* env,const std::string& server,
                     std::vector<Lyric>& lines,const std::string& language){
    std::string base=server;
    while(!base.empty()&&base.back()=='/')base.pop_back();
    if(base.rfind("https://",0)!=0)return;
    for(size_t first=0;first<lines.size();first+=48){
        size_t end=std::min(lines.size(),first+48);
        std::string body="{\"language\":"+jsonQuoted(language)+",\"texts\":[";
        for(size_t i=first;i<end;++i){
            if(i>first)body+=',';
            body+=jsonQuoted(lines[i].line.substr(0,320));
        }
        body+="]}";
        auto root=makeJson(env,getHttp(env,base+"/api/v2/romanize",body));
        if(!root)continue;
        auto list=optArray(env,root,"lines");
        if(list){
            jclass cls=env->FindClass("org/json/JSONArray");
            auto size=cls?env->GetMethodID(cls,"length","()I"):nullptr;
            auto item=cls?env->GetMethodID(cls,"optJSONObject","(I)Lorg/json/JSONObject;"):nullptr;
            int count=size?env->CallIntMethod(list,size):0;
            if(clearException(env,"Aurelis romanize length"))count=0;
            for(int n=0;n<count&&first+static_cast<size_t>(n)<end;++n){
                jobject row=item?env->CallObjectMethod(list,item,n):nullptr;
                if(!row||clearException(env,"Aurelis romanize row"))continue;
                auto reading=optString(env,row,"text");
                if(optString(env,row,"complete")=="true"&&
                   reading!=lines[first+static_cast<size_t>(n)].line&&
                   reading.find_first_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ")!=std::string::npos)
                    lines[first+static_cast<size_t>(n)].romanized=std::move(reading);
                env->DeleteLocalRef(row);
            }
            if(cls)env->DeleteLocalRef(cls);
            env->DeleteLocalRef(list);
        }
        env->DeleteLocalRef(root);
    }
}
#endif

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
                       path.rfind("/v1/lyrics?",0)==0?26000:
                       path.rfind("/v1/offline/save?",0)==0?180000:
                       path.rfind("/v1/playlist/import?",0)==0?45000:
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
    // Offline is an automatic Bridge library, just like Standalone's local
    // library. Keep it discoverable even while the Bridge repairs stale
    // playlist metadata from the actual downloaded/imported audio files.
    std::vector<std::string> names{"Offline"};
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
                if(!name.empty()&&std::none_of(names.begin(),names.end(),
                    [&](const std::string& saved){return saved==name;}))
                    names.push_back(std::move(name));
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
    bool shuffle=false;
    std::string id,title,artist,provider,message,art,repeatMode,queueMode;
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
    result.id=optString(env,root,"id");
    result.artist=optString(env,root,"artist");
    result.provider=optString(env,root,"provider");
    result.message=optString(env,root,"message");
    result.art=optString(env,root,"art");
    result.shuffle=optString(env,root,"shuffle")=="true";
    result.repeatMode=optString(env,root,"repeatMode");
    result.queueMode=optString(env,root,"queueMode");
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
    jobject echo_ = nullptr;
    jobject equalizer_ = nullptr;
    jobject reverb_ = nullptr;
    std::string playableUrl_;
    int volume_ = 75;
    int environment_ = 0;
    void releasePlayer(JNIEnv* env,jobject& object,const char* label){
        if(!object)return;
        jclass cls=env->GetObjectClass(object);
        auto release=env->GetMethodID(cls,"release","()V");
        if(release)env->CallVoidMethod(object,release);
        clearException(env,label);env->DeleteLocalRef(cls);
        env->DeleteGlobalRef(object);object=nullptr;
    }
    void releaseEffect(JNIEnv* env,jobject& effect,const char* label){
        if(!effect)return;
        jclass cls=env->GetObjectClass(effect);
        auto releaseFx=env->GetMethodID(cls,"release","()V");
        if(releaseFx)env->CallVoidMethod(effect,releaseFx);
        clearException(env,label);env->DeleteLocalRef(cls);
        env->DeleteGlobalRef(effect);effect=nullptr;
    }
    void releaseEnvironmentEffects(JNIEnv* env){
        releasePlayer(env,echo_,"cave PCM echo release");
        if(player_){
            jclass media=env->GetObjectClass(player_);
            auto attach=env->GetMethodID(media,"attachAuxEffect","(I)V");
            auto send=env->GetMethodID(media,"setAuxEffectSendLevel","(F)V");
            if(attach)env->CallVoidMethod(player_,attach,0);
            if(send)env->CallVoidMethod(player_,send,0.f);
            clearException(env,"MediaPlayer clear environment effect");
            env->DeleteLocalRef(media);
        }
        releaseEffect(env,equalizer_,"Equalizer release");
        releaseEffect(env,reverb_,"EnvironmentalReverb release");
    }
public:
    ~AndroidPlayer() = default;
    void release(JNIEnv* env) {
        releaseEnvironmentEffects(env);
        environment_=0;
        if (!player_) return;
        jclass cls = env->GetObjectClass(player_);
        jmethodID release = env->GetMethodID(cls, "release", "()V");
        if (release) env->CallVoidMethod(player_, release);
        clearException(env, "MediaPlayer release");
        env->DeleteLocalRef(cls);
        env->DeleteGlobalRef(player_);
        player_ = nullptr;
        playableUrl_.clear();
    }
    bool startUrl(JNIEnv* env, const std::string& url, int volume,
                  bool trustedFirstParty=false) {
        release(env);
        const std::string playableUrl=trustedFirstParty&&
            (url.rfind("https://",0)==0||url.rfind("/",0)==0)?
            url:resolveStreamUrl(env,url);
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
        if (ok) {playableUrl_=playableUrl;setVolume(env, volume);}
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
        if(echo_){
            jclass echoClass=env->GetObjectClass(echo_);
            if(!playing){
                auto seekTo=env->GetMethodID(echoClass,"seekTo","(I)V");
                auto start=env->GetMethodID(echoClass,"start","()V");
                if(seekTo)env->CallVoidMethod(echo_,seekTo,std::max(0,position(env)-135));
                if(start)env->CallVoidMethod(echo_,start);
            }else{
                auto pause=env->GetMethodID(echoClass,"pause","()V");
                if(pause)env->CallVoidMethod(echo_,pause);
            }
            env->DeleteLocalRef(echoClass);
        }
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
        volume_=std::clamp(volume,0,100);
        if (!player_) return;
        jclass cls = env->GetObjectClass(player_);
        jmethodID fn = env->GetMethodID(cls, "setVolume", "(FF)V");
        const float v = std::clamp(volume, 0, 100) / 100.f;
        if (fn) env->CallVoidMethod(player_, fn, v, v);
        if(echo_){
            jclass echoClass=env->GetObjectClass(echo_);
            auto echoVolume=env->GetMethodID(echoClass,"setVolume","(FF)V");
            if(echoVolume)env->CallVoidMethod(echo_,echoVolume,v*.24f,v*.24f);
            env->DeleteLocalRef(echoClass);
        }
        clearException(env, "volume");
        env->DeleteLocalRef(cls);
    }
    void setEnvironment(JNIEnv* env,int environment){
        environment=std::clamp(environment,0,2);
        if(environment==environment_&&
           (environment==0||(equalizer_&&reverb_)))return;
        releaseEnvironmentEffects(env);
        environment_=environment;if(!player_||environment==0)return;
        jclass media=env->GetObjectClass(player_);
        auto sessionFn=env->GetMethodID(media,"getAudioSessionId","()I");
        auto attach=env->GetMethodID(media,"attachAuxEffect","(I)V");
        auto send=env->GetMethodID(media,"setAuxEffectSendLevel","(F)V");
        const int session=sessionFn?env->CallIntMethod(player_,sessionFn):0;
        if(session<=0||clearException(env,"MediaPlayer audio session")){
            env->DeleteLocalRef(media);return;
        }

        jclass eq=env->FindClass("android/media/audiofx/Equalizer");
        auto eqCtor=eq?env->GetMethodID(eq,"<init>","(II)V"):nullptr;
        jobject localEq=eqCtor?env->NewObject(eq,eqCtor,0,session):nullptr;
        if(localEq&&!clearException(env,"Equalizer create")){
            equalizer_=env->NewGlobalRef(localEq);
            auto enable=env->GetMethodID(eq,"setEnabled","(Z)I");
            auto count=env->GetMethodID(eq,"getNumberOfBands","()S");
            auto rangeFn=env->GetMethodID(eq,"getBandLevelRange","()[S");
            auto center=env->GetMethodID(eq,"getCenterFreq","(S)I");
            auto set=env->GetMethodID(eq,"setBandLevel","(SS)V");
            const short bands=count?env->CallShortMethod(equalizer_,count):0;
            short limits[2]{-1500,1500};
            auto range=rangeFn?reinterpret_cast<jshortArray>(env->CallObjectMethod(equalizer_,rangeFn)):nullptr;
            if(range&&env->GetArrayLength(range)>=2)env->GetShortArrayRegion(range,0,2,limits);
            if(range)env->DeleteLocalRef(range);
            for(short band=0;set&&band<bands;++band){
                const int hz=center?env->CallIntMethod(equalizer_,center,band)/1000:band*1000;
                int requested=0;
                if(environment==2){
                    if(hz<250)requested=600;
                    else if(hz<800)requested=250;
                    else if(hz<2000)requested=-350;
                    else if(hz<4000)requested=-1250;
                    else requested=-2300;
                }else requested=hz<250?220:hz<1000?100:hz>6000?-280:0;
                const short level=static_cast<short>(std::clamp(requested,
                    static_cast<int>(limits[0]),static_cast<int>(limits[1])));
                env->CallVoidMethod(equalizer_,set,band,level);
            }
            if(enable)env->CallIntMethod(equalizer_,enable,JNI_TRUE);
        }
        if(localEq)env->DeleteLocalRef(localEq);
        if(eq)env->DeleteLocalRef(eq);
        clearException(env,"Equalizer configure");

        jclass rv=env->FindClass("android/media/audiofx/EnvironmentalReverb");
        auto rvCtor=rv?env->GetMethodID(rv,"<init>","(II)V"):nullptr;
        jobject localRv=rvCtor?env->NewObject(rv,rvCtor,0,0):nullptr;
        if(localRv&&!clearException(env,"EnvironmentalReverb create")){
            reverb_=env->NewGlobalRef(localRv);
            auto setShort=[&](const char* name,short value){
                auto method=env->GetMethodID(rv,name,"(S)V");
                if(method)env->CallVoidMethod(reverb_,method,value);
            };
            auto setInt=[&](const char* name,int value){
                auto method=env->GetMethodID(rv,name,"(I)V");
                if(method)env->CallVoidMethod(reverb_,method,value);
            };
            const bool underwater=environment==2;
            setShort("setRoomLevel",underwater?-2200:-250);
            setShort("setRoomHFLevel",underwater?-6500:-700);
            setInt("setDecayTime",underwater?650:3400);
            setShort("setDecayHFRatio",underwater?250:900);
            setShort("setReflectionsLevel",underwater?-2400:200);
            setInt("setReflectionsDelay",underwater?12:38);
            setShort("setReverbLevel",underwater?-1800:900);
            setInt("setReverbDelay",underwater?18:65);
            setShort("setDiffusion",underwater?650:1000);
            setShort("setDensity",underwater?700:1000);
            auto enable=env->GetMethodID(rv,"setEnabled","(Z)I");
            auto getId=env->GetMethodID(rv,"getId","()I");
            if(enable)env->CallIntMethod(reverb_,enable,JNI_TRUE);
            const int id=getId?env->CallIntMethod(reverb_,getId):0;
            if(id>0&&attach)env->CallVoidMethod(player_,attach,id);
            if(id>0&&send)env->CallVoidMethod(player_,send,underwater?.16f:1.f);
        }
        if(localRv)env->DeleteLocalRef(localRv);
        if(rv)env->DeleteLocalRef(rv);
        clearException(env,"EnvironmentalReverb configure");
        env->DeleteLocalRef(media);
        if(environment==1&&!playableUrl_.empty()){
            // A quiet delayed copy is an early reflection in the actual PCM
            // playback path. It remains audible in internal screen recordings
            // that omit Android auxiliary effects; EnvironmentalReverb supplies
            // the longer decay when the device supports it.
            jclass cls=env->FindClass("android/media/MediaPlayer");
            auto ctor=cls?env->GetMethodID(cls,"<init>","()V"):nullptr;
            jobject local=ctor?env->NewObject(cls,ctor):nullptr;
            if(local&&!clearException(env,"cave echo ctor")){
                echo_=env->NewGlobalRef(local);
                auto stream=env->GetMethodID(cls,"setAudioStreamType","(I)V");
                auto source=env->GetMethodID(cls,"setDataSource","(Ljava/lang/String;)V");
                auto prepare=env->GetMethodID(cls,"prepare","()V");
                auto seekTo=env->GetMethodID(cls,"seekTo","(I)V");
                auto volume=env->GetMethodID(cls,"setVolume","(FF)V");
                auto start=env->GetMethodID(cls,"start","()V");
                jstring url=toJava(env,playableUrl_);
                if(stream)env->CallVoidMethod(echo_,stream,3);
                if(source&&url)env->CallVoidMethod(echo_,source,url);
                if(prepare)env->CallVoidMethod(echo_,prepare);
                if(!clearException(env,"cave echo prepare")){
                    const int behind=std::max(0,position(env)-135);
                    if(seekTo)env->CallVoidMethod(echo_,seekTo,behind);
                    const float level=(volume_/100.f)*.24f;
                    if(volume)env->CallVoidMethod(echo_,volume,level,level);
                    if(start&&isPlaying(env))env->CallVoidMethod(echo_,start);
                    clearException(env,"cave echo start");
                }else releasePlayer(env,echo_,"cave echo cleanup");
                if(url)env->DeleteLocalRef(url);
            }
            if(local)env->DeleteLocalRef(local);
            if(cls)env->DeleteLocalRef(cls);
        }
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
        if(echo_){
            jclass echoClass=env->GetObjectClass(echo_);
            auto echoSeek=env->GetMethodID(echoClass,"seekTo","(I)V");
            if(echoSeek)env->CallVoidMethod(echo_,echoSeek,std::max(0,ms-135));
            env->DeleteLocalRef(echoClass);
        }
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

std::string automaticBridgeToken(JNIEnv* env){
    // The companion provider checks Binder's caller UID against Minecraft.
    // A separate Android app cannot read the key simply by calling the URI.
    std::vector<jobject> refs;
    auto keep=[&](jobject ref){if(ref)refs.push_back(ref);return ref;};
    auto done=[&](std::string value){
        clearException(env,"automatic bridge token");
        for(auto it=refs.rbegin();it!=refs.rend();++it)env->DeleteLocalRef(*it);
        return validToken(value)?value:std::string{};
    };
    jclass thread=(jclass)keep(env->FindClass("android/app/ActivityThread"));
    if(!thread||clearException(env,"bridge ActivityThread"))return done({});
    auto current=env->GetStaticMethodID(thread,"currentApplication","()Landroid/app/Application;");
    if(!current||clearException(env,"bridge currentApplication"))return done({});
    jobject app=keep(env->CallStaticObjectMethod(thread,current));
    if(!app||clearException(env,"bridge application"))return done({});
    jclass context=(jclass)keep(env->GetObjectClass(app));
    auto getResolver=env->GetMethodID(context,"getContentResolver","()Landroid/content/ContentResolver;");
    if(!getResolver||clearException(env,"bridge resolver method"))return done({});
    jobject resolver=keep(env->CallObjectMethod(app,getResolver));
    if(!resolver||clearException(env,"bridge resolver"))return done({});
    jclass resolverClass=(jclass)keep(env->GetObjectClass(resolver));
    auto call=env->GetMethodID(resolverClass,"call","(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Landroid/os/Bundle;)Landroid/os/Bundle;");
    if(!call||clearException(env,"bridge provider method"))return done({});
    jstring authority=(jstring)keep(env->NewStringUTF("com.reaaurelis.levimusic.bridge.token"));
    jstring method=(jstring)keep(env->NewStringUTF("getToken"));
    jobject answer=keep(env->CallObjectMethod(resolver,call,authority,method,nullptr,nullptr));
    if(!answer||clearException(env,"bridge provider call"))return done({});
    jclass bundle=(jclass)keep(env->GetObjectClass(answer));
    auto getString=env->GetMethodID(bundle,"getString","(Ljava/lang/String;)Ljava/lang/String;");
    if(!getString||clearException(env,"bridge token method"))return done({});
    jstring field=(jstring)keep(env->NewStringUTF("token"));
    jstring value=(jstring)keep(env->CallObjectMethod(answer,getString,field));
    if(!value||clearException(env,"bridge token value"))return done({});
    return done(fromJava(env,value));
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
std::string keyName(int code){
    if(code>=29&&code<=54)return std::string(1,char('A'+code-29));
    if(code>=131&&code<=142)return "F"+std::to_string(code-130);
    if(code==62)return "SPACE";
    if(code==66)return "ENTER";
    if(code==67)return "BACKSPACE";
    if(code==19)return "UP";if(code==20)return "DOWN";
    if(code==21)return "LEFT";if(code==22)return "RIGHT";
    return "KEY "+std::to_string(code);
}
struct Rect {
    float x{},y{},w{},h{};
    bool contains(float a,float b)const{return a>=x&&b>=y&&a<=x+w&&b<=y+h;}
};
struct UI {
    float w{},h{},s{};
    Rect mini,outer,left,lyrics,right,close,settings,pair,library,addSong,searchBox,searchGo,repeat,shuffle,prev,play,next,stop,seek,pagePrev,pageNext;
    Rect tabSearch,tabQueue,tabLyrics,queueNow,queueSaved,savedInput,savedCreate;
    std::vector<Rect> rows;
    bool ok()const{return w>0&&h>0;}
};
struct SettingRow {const char* key;const char* label;int page,low,high;bool toggle;};
#if defined(LEVI_STANDALONE)
constexpr std::array<SettingRow,26> quickSettings{{
#else
constexpr std::array<SettingRow,28> quickSettings{{
#endif
    {"worldLyrics3D","3D Floating Lyrics",0,0,1,true},
    {"lyricTrailFollow","Follow player and rise",0,0,1,true},
    {"romanizeLyrics","Romanized subtitles",0,0,1,true},
    {"hudLyrics","Show HUD lyrics",0,0,1,true},
    {"hudLyricPosition","HUD lyric: UL / UR / DL / DR",0,0,3,false},
    {"animation","Smooth animation",0,0,1,true},
    {"lyricDistance","Distance (0.1 block)",1,12,60,false},
    {"lyricHeight","Spawn height (0.1 block)",1,0,30,false},
    {"lyricVerticalRange","Vertical range (0.1 block)",1,0,15,false},
    {"lyricFontSize","Text size (%)",1,60,200,false},
    {"lyricScatterFov","Scatter FOV (degrees)",1,0,140,false},
    {"firstPlacement","Layout: grid / fan / stagger",1,0,2,false},
    {"otherDistance","Distance (0.1 block)",2,18,80,false},
    {"otherHeight","Spawn height (0.1 block)",2,0,40,false},
    {"otherVerticalRange","Vertical range (0.1 block)",2,0,15,false},
    {"otherFontSize","Text size (%)",2,60,200,false},
    {"otherArcSpread","Line arc (degrees)",2,60,330,false},
    {"otherPlacement","Layout: arc / ring / scatter",2,0,2,false},
    {"worldFont","Font: Sans / Serif / Mono / Condensed",3,0,3,false},
    {"lyricPalette","Colors: Pearl / Pastel / Rainbow / Neon",3,0,3,false},
    {"worldTextScript","Text: Original / Romanized",3,0,1,false},
    {"hudSize","Compact HUD size (%)",3,55,125,false},
    {"hudX","Compact HUD horizontal (%)",3,0,100,false},
    {"hudY","Compact HUD vertical (%)",3,0,100,false},
#if !defined(LEVI_STANDALONE)
    {"lyricBeats","3D Lyrics Beats (Bridge audio)",4,0,1,true},
#endif
    {"cinematicAfk","Cinematic AFK View [26.66 Only]",4,0,1,true},
    {"cinematicDelay","AFK delay (seconds)",4,10,60,false},
#if !defined(LEVI_STANDALONE)
    {"immersiveSound","Immersive Sound [Experimental]",4,0,1,true},
#endif
}};
inline Rect settingsTab(const UI& l,int page){
    const float u=l.s,gap=4*u,w=(l.outer.w-48*u-5*gap)/6.f;
    return {l.outer.x+24*u+float(page)*(w+gap),l.outer.y+58*u,w,34*u};
}
inline Rect settingsRow(const UI& l,int index){
    const float u=l.s,step=std::min(48*u,(l.outer.h-157*u)/6.f);
    return {l.outer.x+24*u,l.outer.y+107*u+float(index)*step,
            l.outer.w-48*u,step-5*u};
}
inline Rect settingsAdvanced(const UI& l){
    const float u=l.s;return {l.outer.x+l.outer.w-180*u,
        l.outer.y+l.outer.h-43*u,155*u,30*u};
}
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
    p.repeat={mid-132*u,bottom+5*u,34*u,34*u};
    p.shuffle={mid+98*u,bottom+5*u,34*u,34*u};
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
    std::thread lyricWorker_;
    std::thread drawWorker_;
    std::mutex mu_;
    std::mutex paintMu_;
    std::string rasterLine_;
    int rasterFont_=-1;
    size_t rasterWordCount_=0;
    bool rasterValid_=false;
    std::vector<lm_world3d::GlyphMesh> rasterMeshes_;
    std::condition_variable cv_;
    std::deque<Job> jobs_;
    struct LyricRequest {
        Track track;int duration=0;std::string key;uint64_t serial=0;
        bool standalone=false,rebuild=false;
        std::string server,language;
        std::vector<Lyric> original;
    };
    LyricRequest lyricRequest_;
    uint64_t lyricSerial_=0;
    bool lyricPending_=false;
    std::condition_variable lyricCv_;
    std::vector<Track> results_;
    std::vector<Track> searchCache_;
    std::vector<Track> queueTracks_;
    std::vector<Track> playlistOrder_;
    bool standaloneQueueSelection_=false;
    bool standalonePlaylistQueue_=false;
    int rightTab_=0; // 0 Search, 1 Playlist / Up Next, 2 Lyrics
    std::vector<std::string> playlistNames_;
    std::string activePlaylist_;
    std::string renameTarget_,pendingDelete_;
    int libraryMode_=0; // 0 Search, 1 saved playlists, 2 saved tracks, 3 Up Next radio/playlist queue
    std::vector<Lyric> lyrics_, lyricsSource_;
    std::string query_="lofi", title_="LeviMusic", artist_="Online Music";
    std::string info_="Tap to open your music library", lyric_, apiKey_, bridgeKey_;
#if defined(LEVI_STANDALONE)
    std::string chruBase_="https://aurelis-symphony.go.altare.global";
    std::string romanizationLanguage_="auto";
#endif
    std::string coverUrl_,coverImageId_;
    int coverCount_=0;
    std::map<std::string,std::string> coverImages_;
    int selected_=-1,page_=0,volume_=75,elapsed_=0,total_=0;
    int repeatMode_=1; // 0 off, 1 all, 2 one; playlists loop by default.
    bool shuffle_=false,bridgePlaylistQueue_=false;
    bool open_=false,typing_=false,hud_=true,lyricsOn_=true,playing_=false,shutdown_=false;
    bool settingsOpen_=false;int settingsPage_=0;
    std::atomic<bool> cinematicAfk_{false},immersiveSound_{false};
    std::atomic<int> cinematicDelay_{15};
    std::array<int,4> keybinds_{{139,140,138,137}}; // F8 open, F9 next, F7 previous, F6 play/pause.
    int bindingAction_=-1;
    std::set<int> pressedKeys_;
    std::set<int> menuPointers_;
    std::atomic<bool> enabled_{false};
    std::atomic<int> hudSize_{84},hudX_{3},hudY_{93},panelTop_{15};
    std::atomic<int> hudLyricPosition_{0};
    std::atomic<bool> hudLyrics_{true};
    std::atomic<bool> animation_{true};
    std::atomic<bool> browserEnabled_{false},worldLyrics3D_{true},romanizeLyrics_{true};
    std::atomic<int> worldFont_{3},worldTextScript_{0};
    std::atomic<bool> lyricsRebuild_{false};
    std::map<std::string,std::string> savedSettings_;
    std::map<std::string,std::vector<Track>> localPlaylists_;
    bool restoringSettings_=false;
#if defined(LEVI_STANDALONE)
    std::atomic<bool> useBridge_{false},lyricsBelow_{false},floatingLyrics_{false};
#else
    std::atomic<bool> useBridge_{true},lyricsBelow_{false},floatingLyrics_{false};
#endif
    std::chrono::steady_clock::time_point lyricChanged_{std::chrono::steady_clock::now()};
    std::chrono::steady_clock::time_point bridgePositionStamp_{std::chrono::steady_clock::now()};
    std::chrono::steady_clock::time_point playerPositionStamp_{std::chrono::steady_clock::now()};
    std::chrono::steady_clock::time_point panelToggled_{std::chrono::steady_clock::now()};
    std::string playbackError_;

public:
    LeviMusic():self_(*ll::mod::NativeMod::current()){}
    static LeviMusic& instance(){static LeviMusic mod;return mod;}
    bool load(){
        vm_=self_.getJavaVM();
        try{
            auto settingsFile=self_.getConfigDir()/"settings.conf";
            std::ifstream settings(settingsFile);
            if(!settings){settings.clear();settings.open(self_.getConfigDir()/"settings.conf.bak");}
            std::string row;
            while(std::getline(settings,row)){
                auto eq=row.find('=');
                if(eq!=std::string::npos&&eq<40&&row.size()<512)
                    savedSettings_[row.substr(0,eq)]=row.substr(eq+1);
            }
            if(savedSettings_.empty()){
                std::ifstream backup(self_.getConfigDir()/"settings.conf.bak");
                while(std::getline(backup,row)){
                    auto eq=row.find('=');
                    if(eq!=std::string::npos&&eq<40&&row.size()<512)
                        savedSettings_[row.substr(0,eq)]=row.substr(eq+1);
                }
            }
            if(savedSettings_.find("hudLyricPosition")==savedSettings_.end())
                savedSettings_["hudLyricPosition"]=
                    savedSettings_["lyricBelow"]=="true"||savedSettings_["lyricBelow"]=="1"?"2":"0";
#if defined(LEVI_STANDALONE)
            // These experiments are intentionally Standard-only. Ignore stale
            // values left by an older Standalone installation.
            savedSettings_.erase("lyricBeats");
            savedSettings_.erase("immersiveSound");
            if(savedSettings_["chruServer"].empty()||
               savedSettings_["chruServer"]=="http://127.0.0.1:8080")
                savedSettings_["chruServer"]="https://aurelis-symphony.go.altare.global";
#endif
            std::ifstream keyFile(self_.getConfigDir()/"bridge.token");
            std::string saved;
            if(keyFile>>saved && validToken(saved))bridgeKey_=saved;
            if(!readLocalPlaylists(self_.getConfigDir()/"playlists.dat"))
                readLocalPlaylists(self_.getConfigDir()/"playlists.dat.bak");
            std::filesystem::create_directories(self_.getConfigDir()/"music");
        }catch(...){}
        __android_log_print(ANDROID_LOG_INFO,TAG,"LeviMusic v1.6.15 beta loaded, vm=%p",(void*)vm_);
        return vm_!=nullptr;
    }
    bool enable(){
        {
            std::lock_guard lock(mu_);
            shutdown_=false;open_=false;settingsOpen_=false;typing_=false;menuPointers_.clear();
        }
        restoringSettings_=true;
        bool ok=pl::modmenu::ModuleBuilder(MODULE,"LeviMusic - Aurelis Online")
            .modId(self_.getId()).defaultEnabled(true).hideInHudEditor(true)
            .description("Native music HUD. Bridge mode controls Spotify or YouTube Music via companion Android app.")
            .config("volume","Volume",pl::modmenu::ConfigType::SliderInt,remembered("volume","75"),"0","100")
#if defined(LEVI_STANDALONE)
            .config("useBridge","Use Android Bridge (optional)",pl::modmenu::ConfigType::Toggle,remembered("useBridge","false"))
#else
            .config("useBridge","Use Android Bridge",pl::modmenu::ConfigType::Toggle,remembered("useBridge","true"))
#endif
            .config("bridgeKey","Bridge Key (copy from companion app)",pl::modmenu::ConfigType::Text,"")
            .config("lyricBelow","Show lyric bubble below HUD",pl::modmenu::ConfigType::Toggle,remembered("lyricBelow","false"))
            .config("floatingLyrics","Legacy 2D lyric scatter",pl::modmenu::ConfigType::Toggle,remembered("floatingLyrics","false"))
            .config("worldLyrics3D","3D Floating Lyrics (client-only)",pl::modmenu::ConfigType::Toggle,remembered("worldLyrics3D","true"))
            .config("worldTextScript","3D Script (0 Original / 1 Romanized)",pl::modmenu::ConfigType::SliderInt,remembered("worldTextScript","0"),"0","1")
            .config("worldFont","3D Font (0 Sans / 1 Serif Italic / 2 Mono Bold / 3 Condensed)",pl::modmenu::ConfigType::SliderInt,remembered("worldFont","3"),"0","3")
            .config("lyricPalette","3D Colors (0 Pearl / 1 Pastel / 2 Rainbow / 3 Neon)",pl::modmenu::ConfigType::SliderInt,remembered("lyricPalette","2"),"0","3")
            .config("lyricDistance","First Person Distance (0.1 blocks)",pl::modmenu::ConfigType::SliderInt,remembered("lyricDistance","22"),"12","60")
            .config("lyricHeight","First Person Height (0.1 blocks)",pl::modmenu::ConfigType::SliderInt,remembered("lyricHeight","0"),"0","30")
            .config("lyricVerticalRange","First Person Vertical Scatter (+/- 0.1 blocks)",pl::modmenu::ConfigType::SliderInt,remembered("lyricVerticalRange","2"),"0","15")
            .config("lyricFontSize","First Person Font Size (%)",pl::modmenu::ConfigType::SliderInt,remembered("lyricFontSize","115"),"60","200")
            .config("lyricScatterFov","First Person Scatter FOV (degrees)",pl::modmenu::ConfigType::SliderInt,remembered("lyricScatterFov","60"),"0","140")
            .config("firstPlacement","First Person Layout (0 grid / 1 fan / 2 staggered)",pl::modmenu::ConfigType::SliderInt,remembered("firstPlacement","0"),"0","2")
            .config("otherDistance","Other Cameras Distance (0.1 blocks)",pl::modmenu::ConfigType::SliderInt,remembered("otherDistance","18"),"18","80")
            .config("otherHeight","Other Cameras Height (0.1 blocks)",pl::modmenu::ConfigType::SliderInt,remembered("otherHeight","0"),"0","40")
            .config("otherVerticalRange","Other Cameras Vertical Scatter (+/- 0.1 blocks)",pl::modmenu::ConfigType::SliderInt,remembered("otherVerticalRange","4"),"0","15")
            .config("otherFontSize","Other Cameras Font Size (%)",pl::modmenu::ConfigType::SliderInt,remembered("otherFontSize","140"),"60","200")
            .config("otherArcSpread","Other Cameras Line Arc (degrees)",pl::modmenu::ConfigType::SliderInt,remembered("otherArcSpread","220"),"60","330")
            .config("otherPlacement","Other Cameras Layout (0 arc / 1 ring / 2 scattered ring)",pl::modmenu::ConfigType::SliderInt,remembered("otherPlacement","2"),"0","2")
            .config("lyricTrailFollow","3D Lyrics Follow Player and Rise",pl::modmenu::ConfigType::Toggle,remembered("lyricTrailFollow","true"))
#if !defined(LEVI_STANDALONE)
            .config("lyricBeats","Experimental 3D Lyrics Beats (Bridge audio)",pl::modmenu::ConfigType::Toggle,remembered("lyricBeats","false"))
#endif
            .config("romanizeLyrics","Romanize lyrics (Latin pronunciation)",pl::modmenu::ConfigType::Toggle,remembered("romanizeLyrics","true"))
#if defined(LEVI_STANDALONE)
            .config("embeddedBrowser","Experimental embedded WebView (visible video)",pl::modmenu::ConfigType::Toggle,remembered("embeddedBrowser","false"))
#endif
#if defined(LEVI_STANDALONE)
            .config("chruServer","Aurelis Symphony API URL (hosted default)",
                    pl::modmenu::ConfigType::Text,remembered("chruServer","https://aurelis-symphony.go.altare.global"))
            .config("romanizeLanguage","Romanization language (auto / ja / ko / zh)",
                    pl::modmenu::ConfigType::Text,remembered("romanizeLanguage","auto"))
#else
            .config("apiKey","Audius API Key (optional)",pl::modmenu::ConfigType::Text,remembered("apiKey",""))
#endif
            .config("hud","Show compact HUD",pl::modmenu::ConfigType::Toggle,remembered("hud","true"))
            .config("lyrics","Enable lyrics",pl::modmenu::ConfigType::Toggle,remembered("lyrics","true"))
            .config("hudLyrics","Show 2D lyrics near compact HUD",pl::modmenu::ConfigType::Toggle,remembered("hudLyrics","true"))
            .config("hudLyricPosition","HUD Lyrics Position (0 Up Left / 1 Up Right / 2 Down Left / 3 Down Right)",pl::modmenu::ConfigType::SliderInt,remembered("hudLyricPosition","0"),"0","3")
            .config("hudSize","HUD Size (%)",pl::modmenu::ConfigType::SliderInt,remembered("hudSize","84"),"55","125")
            .config("hudX","HUD Horizontal Position (0-100%)",pl::modmenu::ConfigType::SliderInt,remembered("hudX","3"),"0","100")
            .config("hudY","HUD Vertical Position (0-100%)",pl::modmenu::ConfigType::SliderInt,remembered("hudY","93"),"0","100")
            .config("panelTop","Menu Distance from Top (%)",pl::modmenu::ConfigType::SliderInt,remembered("panelTop","15"),"12","32")
            .config("animation","Subtle Animations",pl::modmenu::ConfigType::Toggle,remembered("animation","true"))
            .onConfigChanged([](std::string_view,std::string_view k,std::string_view v){
                instance().setConfig(k,v);
            }).registerModule();
        if(!ok){restoringSettings_=false;return false;}
        for(const auto& [key,value]:savedSettings_)setConfig(key,value);
        lm_cinematic::request(cinematicAfk_,cinematicDelay_);
#if defined(LEVI_STANDALONE)
        lm_world3d::experimentalBeats=false;
        immersiveSound_=false;
        lm_immersive::request(false);
#else
        lm_immersive::request(immersiveSound_);
#endif
        lm_world3d::request(worldLyrics3D_||cinematicAfk_||immersiveSound_,true);
        restoringSettings_=false;
        static std::once_flag registered;
        std::call_once(registered,[](){
            pl::input::registerTouchCallback([](const pl::input::TouchEvent& e){return instance().touch(e);});
            pl::input::registerTextInputCallback([](const pl::input::TextInputEvent& e){return instance().input(e);});
            pl::input::registerKeyCallback([](const pl::input::KeyEvent& e){return instance().key(e);});
        });
        levimusic_icons::registerAll();
        enabled_=true;
        lyricWorker_=std::thread([this](){processLyrics();});
        worker_=std::thread([this](){process();});
        paint();
        drawWorker_=std::thread([this](){processDraw();});
        return true;
    }
    bool disable(){
        enabled_=false;
        lm_cinematic::shutdown();
        lm_immersive::request(false);
        {std::lock_guard lock(mu_);shutdown_=true;typing_=false;jobs_.clear();
            menuPointers_.clear();pressedKeys_.clear();bindingAction_=-1;++lyricSerial_;}
        cv_.notify_all();lyricCv_.notify_all();
        if(drawWorker_.joinable())drawWorker_.join();
        if(worker_.joinable())worker_.join();
        if(lyricWorker_.joinable())lyricWorker_.join();
        lm_world3d::shutdown();
        {std::lock_guard paintLock(paintMu_);
            pl::modmenu::submitDrawCommands(MODULE,std::span<const pl::modmenu::DrawCommand>{});}
        pl::modmenu::unregisterModule(MODULE);
        return true;
    }
    bool unload(){lm_world3d::unload();return true;}
private:
    void processDraw(){
        auto next=std::chrono::steady_clock::now();
        while(enabled_){
            paint();
            next+=std::chrono::milliseconds(animation_?33:250);
            if(next<std::chrono::steady_clock::now())next=std::chrono::steady_clock::now();
            std::this_thread::sleep_until(next);
        }
    }
    std::string remembered(const char* key,const char* fallback)const{
        auto it=savedSettings_.find(key);
        return it==savedSettings_.end()||it->second.empty()?fallback:it->second;
    }
    void persistSetting(std::string_view k,std::string_view v){
        if(restoringSettings_||k=="bridgeKey"||k.size()>40||v.size()>350||v.find('\n')!=v.npos)return;
        savedSettings_[std::string(k)]=std::string(v);
        try{
            std::filesystem::create_directories(self_.getConfigDir());
            auto path=self_.getConfigDir()/"settings.conf";
            auto backup=self_.getConfigDir()/"settings.conf.bak";
            auto tmp=self_.getConfigDir()/"settings.conf.tmp";
            std::ofstream file(tmp,std::ios::trunc);
            for(const auto& [key,value]:savedSettings_)file<<key<<'='<<value<<'\n';
            file.flush();if(!file)return;
            file.close();if(!file)return;
            if(std::filesystem::exists(path)){
                std::filesystem::remove(backup);
                std::filesystem::rename(path,backup);
            }
            try{std::filesystem::rename(tmp,path);}
            catch(...){if(std::filesystem::exists(backup))std::filesystem::rename(backup,path);throw;}
        }catch(...){}
    }
    bool readLocalPlaylists(const std::filesystem::path& path){
        try{
            if(!std::filesystem::is_regular_file(path)||
               std::filesystem::file_size(path)>4'000'000)return false;
            std::ifstream file(path,std::ios::binary);
            std::string header;size_t count=0;
            if(!(file>>header>>count)||header!="LMPL1"||count>40)return false;
            std::map<std::string,std::vector<Track>> loaded;
            for(size_t i=0;i<count;++i){
                std::string name;size_t size=0;
                if(!(file>>std::quoted(name)>>size)||name.empty()||name.size()>120||size>300)
                    return false;
                auto& songs=loaded[name];
                for(size_t j=0;j<size;++j){
                    Track track;
                    if(!(file>>std::quoted(track.id)>>std::quoted(track.title)>>
                         std::quoted(track.artist)>>std::quoted(track.art))||
                       track.id.empty()||track.id.size()>350||track.title.size()>400||
                       track.artist.size()>350||track.art.size()>1000)return false;
                    songs.push_back(std::move(track));
                }
            }
            if(loaded.size()!=count)return false;
            localPlaylists_=std::move(loaded);return true;
        }catch(...){return false;}
    }
    bool saveLocalPlaylists(){
        try{
            auto dir=self_.getConfigDir();std::filesystem::create_directories(dir);
            auto path=dir/"playlists.dat",tmp=dir/"playlists.dat.tmp";
            auto backup=dir/"playlists.dat.bak";
            std::ofstream file(tmp,std::ios::binary|std::ios::trunc);
            file<<"LMPL1 "<<localPlaylists_.size()<<'\n';
            for(const auto& [name,songs]:localPlaylists_){
                file<<std::quoted(name)<<' '<<songs.size()<<'\n';
                for(const auto& song:songs)
                    file<<std::quoted(song.id)<<' '<<std::quoted(song.title)<<' '
                        <<std::quoted(song.artist)<<' '<<std::quoted(song.art)<<'\n';
            }
            file.flush();if(!file)return false;
            file.close();if(!file||std::filesystem::file_size(tmp)>4'000'000)return false;
            if(std::filesystem::exists(path)){
                std::filesystem::remove(backup);std::filesystem::rename(path,backup);
            }
            try{std::filesystem::rename(tmp,path);}
            catch(...){if(std::filesystem::exists(backup))std::filesystem::rename(backup,path);throw;}
            return true;
        }catch(...){return false;}
    }
    void processLyrics(){
        EnvScope scope(vm_);
        if(!scope)return;
        while(true){
            LyricRequest req;
            {
                std::unique_lock lock(mu_);
                lyricCv_.wait(lock,[this]{return shutdown_||lyricPending_;});
                if(shutdown_)return;
                req=lyricRequest_;lyricPending_=false;
            }
            std::vector<Lyric> found;
            if(req.rebuild)found=std::move(req.original);
            else {
#if defined(LEVI_STANDALONE)
                if(req.standalone)found=aurelisLyrics(scope.env,req.server,req.track,req.duration);
#endif
                if(found.empty())found=searchLyrics(scope.env,req.track,req.duration,req.key);
            }
            auto shown=found;
            if(romanizeLyrics_){
#if defined(LEVI_STANDALONE)
                if(req.standalone)aurelisRomanize(scope.env,req.server,shown,req.language);
#endif
                romanizeLyrics(scope.env,shown);
            }
            {
                std::lock_guard lock(mu_);
                if(shutdown_||req.serial!=lyricSerial_)continue;
                lyricsSource_=std::move(found);lyrics_=std::move(shown);
                lyric_.clear();
                for(const auto& item:lyrics_){
                    if(item.ms>elapsed_)break;
                    lyric_=item.line;
                }
                lyricChanged_=std::chrono::steady_clock::now();
            }
        }
    }
    void queueLyrics(const Track& track,int duration,const std::string& key,bool standalone=false){
        {
            std::lock_guard lock(mu_);
            lyricRequest_={track,duration,key,++lyricSerial_};
            lyricRequest_.standalone=standalone;
#if defined(LEVI_STANDALONE)
            lyricRequest_.server=chruBase_;
            lyricRequest_.language=romanizationLanguage_;
#endif
            lyricPending_=true;
        }
        lyricCv_.notify_one();
    }
    void setConfig(std::string_view k,std::string_view v){
        persistSetting(k,v);
        if(k=="romanizeLyrics"){
            romanizeLyrics_=v=="true"||v=="1";
            lyricsRebuild_=true;
            cv_.notify_all();
        } else if(k=="worldLyrics3D") {
            worldLyrics3D_=v=="true"||v=="1";
            lm_world3d::request(worldLyrics3D_||cinematicAfk_||immersiveSound_,true);
        } else if(k=="cinematicAfk") {
            cinematicAfk_=v=="true"||v=="1";
            lm_cinematic::request(cinematicAfk_,cinematicDelay_);
            lm_world3d::request(worldLyrics3D_||cinematicAfk_||immersiveSound_,true);
        } else if(k=="cinematicDelay") {
            try{cinematicDelay_=std::clamp(std::stoi(std::string(v)),10,60);}catch(...){}
            lm_cinematic::request(cinematicAfk_,cinematicDelay_);
        } else if(k=="immersiveSound") {
#if defined(LEVI_STANDALONE)
            immersiveSound_=false;
            lm_immersive::request(false);
#else
            immersiveSound_=v=="true"||v=="1";
            lm_immersive::request(immersiveSound_);
            lm_world3d::request(worldLyrics3D_||cinematicAfk_||immersiveSound_,true);
#endif
        } else if(k=="worldTextScript") {
            try{worldTextScript_=std::clamp(std::stoi(std::string(v)),0,1);}catch(...){}
        } else if(k=="worldFont") {
            try{worldFont_=std::clamp(std::stoi(std::string(v)),0,3);}catch(...){}
        } else if(k=="lyricPalette") {
            try{lm_world3d::lyricPalette=std::clamp(std::stoi(std::string(v)),0,3);}catch(...){}
        } else if(k=="lyricDistance") {
            try{lm_world3d::lyricDistanceTenths=std::clamp(std::stoi(std::string(v)),12,60);}catch(...){}
        } else if(k=="lyricHeight") {
            try{lm_world3d::lyricHeightTenths=std::clamp(std::stoi(std::string(v)),0,30);}catch(...){}
        } else if(k=="lyricVerticalRange") {
            try{lm_world3d::lyricVerticalRangeTenths=std::clamp(std::stoi(std::string(v)),0,15);}catch(...){}
        } else if(k=="lyricFontSize") {
            try{lm_world3d::lyricFontPercent=std::clamp(std::stoi(std::string(v)),60,200);}catch(...){}
        } else if(k=="lyricTrailFollow") {
            lm_world3d::lyricTrailFollow=v=="true"||v=="1";
        } else if(k=="lyricBeats") {
#if defined(LEVI_STANDALONE)
            lm_world3d::experimentalBeats=false;
#else
            lm_world3d::experimentalBeats=v=="true"||v=="1";
            cv_.notify_all();
#endif
        } else if(k=="lyricScatterFov") {
            try{lm_world3d::lyricScatterFov=std::clamp(std::stoi(std::string(v)),0,140);}catch(...){}
        } else if(k=="firstPlacement") {
            try{lm_world3d::firstPlacement=std::clamp(std::stoi(std::string(v)),0,2);}catch(...){}
        } else if(k=="otherDistance") {
            try{lm_world3d::otherDistanceTenths=std::clamp(std::stoi(std::string(v)),18,80);}catch(...){}
        } else if(k=="otherHeight") {
            try{lm_world3d::otherHeightTenths=std::clamp(std::stoi(std::string(v)),0,40);}catch(...){}
        } else if(k=="otherVerticalRange") {
            try{lm_world3d::otherVerticalRangeTenths=std::clamp(std::stoi(std::string(v)),0,15);}catch(...){}
        } else if(k=="otherFontSize") {
            try{lm_world3d::otherFontPercent=std::clamp(std::stoi(std::string(v)),60,200);}catch(...){}
        } else if(k=="otherArcSpread") {
            try{lm_world3d::otherArcSpread=std::clamp(std::stoi(std::string(v)),60,330);}catch(...){}
        } else if(k=="otherPlacement") {
            try{lm_world3d::otherPlacement=std::clamp(std::stoi(std::string(v)),0,2);}catch(...){}
        } else if(k=="embeddedBrowser") {
            browserEnabled_=v=="true"||v=="1";
        } else if(k=="floatingLyrics") {
            floatingLyrics_=v=="true"||v=="1";
        } else if(k=="useBridge") {
            useBridge_=v=="true"||v=="1";
        } else if(k=="lyricBelow") {
            lyricsBelow_=v=="true"||v=="1";
            if(savedSettings_.find("hudLyricPosition")==savedSettings_.end())
                hudLyricPosition_=lyricsBelow_?2:0;
        } else if(k=="hudLyrics") {
            hudLyrics_=v=="true"||v=="1";
        } else if(k=="hudLyricPosition") {
            try{hudLyricPosition_=std::clamp(std::stoi(std::string(v)),0,3);}catch(...){}
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
        } else if(k=="keyOpen"||k=="keyNext"||k=="keyPrevious"||k=="keyPlay") {
            const int index=k=="keyOpen"?0:k=="keyNext"?1:k=="keyPrevious"?2:3;
            try{keybinds_[static_cast<size_t>(index)]=std::clamp(std::stoi(std::string(v)),1,288);}catch(...){}
        } else {
            std::lock_guard lock(mu_);
#if defined(LEVI_STANDALONE)
            if(k=="romanizeLanguage"){
                if(v=="auto"||v=="ja"||v=="ko"||v=="zh")
                    romanizationLanguage_=std::string(v);
                lyricsRebuild_=true;
            }else if(k=="chruServer"){
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
    void loadTrackCovers(JNIEnv* env,std::vector<Track>& songs){
        size_t loaded=0;
        for(auto& song:songs){
            if(song.art.rfind("https://",0)!=0||song.art.size()>2048)continue;
            if(auto it=coverImages_.find(song.art);it!=coverImages_.end()){
                song.imageId=it->second;continue;
            }
            if(coverCount_>=120||loaded>=12)break;
            const std::string id="levimusic.row."+std::to_string(++coverCount_);
            if(registerCover(env,song.art,id)){
                coverImages_[song.art]=id;song.imageId=id;++loaded;
            }
        }
    }
    int quickValue(const SettingRow& row){
        const std::string_view k=row.key;
        if(k=="worldLyrics3D")return worldLyrics3D_;
        if(k=="lyricTrailFollow")return lm_world3d::lyricTrailFollow;
        if(k=="lyricBeats")return lm_world3d::experimentalBeats;
        if(k=="cinematicAfk")return cinematicAfk_;
        if(k=="cinematicDelay")return cinematicDelay_;
        if(k=="immersiveSound")return immersiveSound_;
        if(k=="romanizeLyrics")return romanizeLyrics_;
        if(k=="animation")return animation_;
        if(k=="hudLyrics")return hudLyrics_;
        if(k=="hudLyricPosition")return hudLyricPosition_;
        if(k=="lyricDistance")return lm_world3d::lyricDistanceTenths;
        if(k=="lyricHeight")return lm_world3d::lyricHeightTenths;
        if(k=="lyricVerticalRange")return lm_world3d::lyricVerticalRangeTenths;
        if(k=="lyricFontSize")return lm_world3d::lyricFontPercent;
        if(k=="lyricScatterFov")return lm_world3d::lyricScatterFov;
        if(k=="firstPlacement")return lm_world3d::firstPlacement;
        if(k=="otherDistance")return lm_world3d::otherDistanceTenths;
        if(k=="otherHeight")return lm_world3d::otherHeightTenths;
        if(k=="otherVerticalRange")return lm_world3d::otherVerticalRangeTenths;
        if(k=="otherFontSize")return lm_world3d::otherFontPercent;
        if(k=="otherArcSpread")return lm_world3d::otherArcSpread;
        if(k=="otherPlacement")return lm_world3d::otherPlacement;
        if(k=="worldFont")return worldFont_;
        if(k=="lyricPalette")return lm_world3d::lyricPalette;
        if(k=="worldTextScript")return worldTextScript_;
        if(k=="hudSize")return hudSize_;
        if(k=="hudX")return hudX_;
        if(k=="hudY")return hudY_;
        return row.low;
    }
    bool touch(const pl::input::TouchEvent& e){
        if(!enabled_)return false;
        lm_cinematic::noteActivity();
        UI l=measure(hudSize_,hudX_,hudY_,panelTop_);
        if(!l.ok())return false;
        bool opened,hud,settingsOpen;int settingsPage;
        {std::lock_guard lock(mu_);opened=open_;hud=hud_;
            settingsOpen=settingsOpen_;settingsPage=settingsPage_;}
        // Android actions: DOWN=0, UP=1, MOVE=2, CANCEL=3,
        // POINTER_DOWN=5 and POINTER_UP=6. Only pointers that started on our
        // UI are consumed. A joystick pointer that predates opening the menu
        // must still reach Minecraft on UP/CANCEL so movement cannot stick.
        if(e.action==0||e.action==5){
            const bool capture=opened||(hud&&l.mini.contains(e.x,e.y));
            if(capture){std::lock_guard lock(mu_);menuPointers_.insert(e.pointerId);}
            return capture;
        }
        bool captured=false;
        {std::lock_guard lock(mu_);captured=menuPointers_.contains(e.pointerId);}
        if(!captured)return false;
        if(e.action==2)return true;
        if(e.action==3){std::lock_guard lock(mu_);menuPointers_.erase(e.pointerId);return true;}
        if(e.action!=1&&e.action!=6)return true;
        {std::lock_guard lock(mu_);menuPointers_.erase(e.pointerId);}
        if(!opened){
            if(hud&&l.mini.contains(e.x,e.y)){
                {std::lock_guard lock(mu_);open_=true;panelToggled_=std::chrono::steady_clock::now();}
                paint();return true;
            }
            return false;
        }
        if(settingsOpen){
            if(l.close.contains(e.x,e.y)||!l.outer.contains(e.x,e.y)){
                std::lock_guard lock(mu_);settingsOpen_=false;bindingAction_=-1;
            }else if(settingsAdvanced(l).contains(e.x,e.y)){
                {std::lock_guard lock(mu_);settingsOpen_=false;open_=false;
                    panelToggled_=std::chrono::steady_clock::now();}
                pl::modmenu::requestOpenMenu();
            }else{
                bool tab=false;
                for(int page=0;page<6;++page)if(settingsTab(l,page).contains(e.x,e.y)){
                    std::lock_guard lock(mu_);settingsPage_=page;bindingAction_=-1;tab=true;break;
                }
                if(!tab){
                    if(settingsPage==5){
                        for(int i=0;i<4;++i)if(settingsRow(l,i).contains(e.x,e.y)){
                            std::lock_guard lock(mu_);bindingAction_=i;break;
                        }
                    }
                    int index=0;
                    for(const auto& row:quickSettings){
                        if(row.page!=settingsPage)continue;
                        const Rect area=settingsRow(l,index++);
                        if(!area.contains(e.x,e.y))continue;
                        const int old=quickValue(row);
                        int value=old;
                        if(row.toggle)value=1-old;
                        else if(e.x>area.x+area.w-36*l.s)value=old+1;
                        else if(e.x>area.x+area.w-76*l.s)value=old-1;
                        else if(e.x>area.x+area.w*.52f)
                            value=row.low+int(std::round((row.high-row.low)*
                                std::clamp((e.x-(area.x+area.w*.52f)) /
                                           (area.w*.48f-80*l.s),0.f,1.f)));
                        value=std::clamp(value,row.low,row.high);
                        if(value!=old)setConfig(row.key,std::to_string(value));
                        break;
                    }
                }
            }
            paint();return true;
        }
        if(l.close.contains(e.x,e.y)||!l.outer.contains(e.x,e.y)){
            {std::lock_guard lock(mu_);open_=false;typing_=false;panelToggled_=std::chrono::steady_clock::now();}
            pl::input::hideKeyboard();
        }else if(l.settings.contains(e.x,e.y)){
            {std::lock_guard lock(mu_);settingsOpen_=true;settingsPage_=0;typing_=false;}
            pl::input::hideKeyboard();
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
        else if(l.repeat.contains(e.x,e.y))addJob(Action::RepeatCycle);
        else if(l.shuffle.contains(e.x,e.y))addJob(Action::ShuffleToggle);
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
                addJob(Action::PlaylistList);
            }else if(tab==1 && mode==1 && l.savedInput.contains(e.x,e.y)){
                {std::lock_guard lock(mu_);query_.clear();typing_=true;}
                pl::input::showKeyboard();
            }else if(tab==1 && mode==1 && l.savedCreate.contains(e.x,e.y)){
                std::string name;
                {std::lock_guard lock(mu_);name=query_;typing_=false;}
                pl::input::hideKeyboard();
                std::string old;
                {std::lock_guard lock(mu_);old=renameTarget_;renameTarget_.clear();}
                addJob(old.empty()?Action::PlaylistCreate:Action::PlaylistRename,
                    old.empty()?name:old+"\n"+name);
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
                bool plus=false,minus=false,playlistAdd=false,playlistRemove=false,offlineSave=false;
                bool renamePlaylist=false,deletePlaylist=false;
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
                        playlistAdd=tab==0 && !plus &&
                            e.x>l.rows[i].x+l.rows[i].w-83*l.s;
                        const bool saveOffline=tab==0 && !plus && !playlistAdd &&
                            e.x>l.rows[i].x+l.rows[i].w-124*l.s;
                        minus=tab==1 && mode==3 && index>0
                            && e.x>l.rows[i].x+l.rows[i].w-42*l.s;
                        playlistRemove=tab==1 && mode==2 &&
                            e.x>l.rows[i].x+l.rows[i].w-42*l.s;
                        if(tab==0){id=results_[static_cast<size_t>(index)].id;
                            if(saveOffline)offlineSave=true;}
                        else if(mode==3)id=queueTracks_[static_cast<size_t>(index)].id;
                        else if(mode==1)name=playlistNames_[static_cast<size_t>(index)];
                        if(tab==1&&mode==1){
                            deletePlaylist=e.x>l.rows[i].x+l.rows[i].w-35*l.s;
                            renamePlaylist=!deletePlaylist&&e.x>l.rows[i].x+l.rows[i].w-70*l.s;
                        }
                        else if(mode==2)id=results_[static_cast<size_t>(index)].id;
                        break;
                    }
                }
                if(selected>=0){
                    if(tab==0)addJob(plus?Action::QueueAdd:
                                      playlistAdd?Action::PlaylistAdd:
                                      offlineSave?Action::OfflineSave:Action::Select,id);
                    else if(mode==3)addJob(minus?Action::QueueRemove:Action::QueuePlay,id);
                    else if(mode==1){
                        if(renamePlaylist){
                            {std::lock_guard lock(mu_);renameTarget_=name;query_=name;typing_=true;}
                            pl::input::showKeyboard();
                        }else if(deletePlaylist){
                            bool confirmed=false;
                            {std::lock_guard lock(mu_);
                                confirmed=pendingDelete_==name;pendingDelete_=confirmed?"":name;}
                            if(confirmed)addJob(Action::PlaylistDelete,name);
                            else update("Tap X again to delete",name,"Playlist removal requires confirmation");
                        }else addJob(Action::PlaylistOpen,name);
                    }
                    else if(mode==2)addJob(playlistRemove?Action::PlaylistRemove:
                                           Action::PlaylistPlay,playlistRemove?id:
                                           std::to_string(selected));
                }
            }
        }
        paint();
        return true;
    }
    bool input(const pl::input::TextInputEvent& e){
        if(!enabled_)return false;
        lm_cinematic::noteActivity();
        bool editing=false,submit=false;std::string query;
        {
            std::lock_guard lock(mu_);
            editing=open_&&typing_;
            if(editing){
                for(char ch:e.text){
                    if(ch=='\n'||ch=='\r'){submit=true;break;}
                    if(query_.size()<2048)query_+=ch;
                }
                if(submit)typing_=false;
                query=query_;
            }
        }
        if(editing){if(submit){
            pl::input::hideKeyboard();
            int mode;{std::lock_guard lock(mu_);mode=libraryMode_;}
            std::string old;
            {std::lock_guard lock(mu_);old=renameTarget_;renameTarget_.clear();}
            addJob(mode==1?(old.empty()?Action::PlaylistCreate:Action::PlaylistRename):Action::Search,
                old.empty()?query:old+"\n"+query);
        }paint();}
        return editing;
    }
    bool key(const pl::input::KeyEvent& e){
        if(!enabled_)return false;
        lm_cinematic::noteActivity();
        int bind=-1;
        {
            std::lock_guard lock(mu_);
            bind=bindingAction_;
            if(!e.isKeyDown){
                const bool was=pressedKeys_.erase(e.keyCode)>0;
                return was||bind>=0;
            }
        }
        if(bind>=0){
            {std::lock_guard lock(mu_);if(!pressedKeys_.insert(e.keyCode).second)return true;}
            static constexpr const char* keys[]={"keyOpen","keyNext","keyPrevious","keyPlay"};
            {std::lock_guard lock(mu_);bindingAction_=-1;}
            setConfig(keys[bind],std::to_string(e.keyCode));
            return true;
        }
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
            std::string old;
            {std::lock_guard lock(mu_);old=renameTarget_;renameTarget_.clear();}
            addJob(mode==1?(old.empty()?Action::PlaylistCreate:Action::PlaylistRename):Action::Search,
                old.empty()?query:old+"\n"+query);
        }paint();}
        if(editing)return true;
        int action=-1;
        {std::lock_guard lock(mu_);
            for(int i=0;i<4;++i)if(keybinds_[static_cast<size_t>(i)]==e.keyCode){action=i;break;}}
        if(action>=0){std::lock_guard lock(mu_);if(!pressedKeys_.insert(e.keyCode).second)return true;}
        if(action==0){
            {std::lock_guard lock(mu_);open_=!open_;settingsOpen_=false;typing_=false;
                panelToggled_=std::chrono::steady_clock::now();}
            pl::input::hideKeyboard();paint();return true;
        }
        if(action==1){addJob(Action::Next);return true;}
        if(action==2){addJob(Action::Previous);return true;}
        if(action==3){addJob(Action::PlayPause);return true;}
        return false;
    }
    void paint(){
        std::lock_guard paintLock(paintMu_);
        if(!enabled_)return;
        UI l=measure(hudSize_,hudX_,hudY_,panelTop_);
        if(!l.ok())return;
        std::string title,artist,info,query,lyric,coverImage;
        std::vector<Track> tracks,queueTracks;
        std::vector<std::string> playlistNames;
        std::string activePlaylist;
        std::vector<Lyric> timedLyrics;
        float lyricAge=100.f;
        int current,page,elapsed,total,volume,libraryMode,rightTab,settingsPage,repeatMode,bindingAction;
        bool open,showHud,playing,typing,settingsOpen,shuffle,bridgePlaylist,playlistQueue;
        std::array<int,4> keybinds{};
        float panelAge=1.f;
        {
            std::lock_guard lock(mu_);
            title=title_;artist=artist_;info=info_;query=query_;lyric=lyricsOn_?lyric_:"";coverImage=coverImageId_;
            tracks=results_;queueTracks=queueTracks_;playlistNames=playlistNames_;
            activePlaylist=activePlaylist_;libraryMode=libraryMode_;rightTab=rightTab_;
            settingsPage=settingsPage_;settingsOpen=settingsOpen_;
            timedLyrics=lyrics_;
            panelAge=std::chrono::duration<float>(std::chrono::steady_clock::now()-panelToggled_).count();
            lyricAge=std::chrono::duration<float>(std::chrono::steady_clock::now()-lyricChanged_).count();
            current=selected_;page=page_;
            elapsed=elapsed_;total=total_;volume=volume_;
            open=open_;showHud=hud_;playing=playing_;typing=typing_;
            repeatMode=repeatMode_;shuffle=shuffle_;bridgePlaylist=bridgePlaylistQueue_;
            playlistQueue=standalonePlaylistQueue_;
            keybinds=keybinds_;bindingAction=bindingAction_;
            if(useBridge_ && playing_) {
                auto age=std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now()-bridgePositionStamp_).count();
                elapsed=static_cast<int>(std::min<int64_t>(total>0?total:INT32_MAX,
                    static_cast<int64_t>(elapsed)+std::clamp<int64_t>(age,0,1300)));
            }else if(!useBridge_&&playing_){
                const auto age=std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now()-playerPositionStamp_).count();
                elapsed=static_cast<int>(std::min<int64_t>(total>0?total:INT32_MAX,
                    static_cast<int64_t>(elapsed)+std::clamp<int64_t>(age,0,30000)));
            }
        }
        lm_cinematic::setPlaying(playing);
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
        if(worldLyrics3D_){
            std::string worldLine;
            bool worldHasReading=false;
            const Lyric* worldItem=nullptr;
            const std::vector<lm_timing::WordCue>* wordCues=nullptr;
            int cueStart=std::max(0,elapsed-500),cueEnd=elapsed+3000;
            for(size_t i=0;i<timedLyrics.size();++i){
                const auto& item=timedLyrics[i];
                if(item.ms<=elapsed){
                    worldLine=item.line;
                    worldItem=&item;
                    wordCues=&item.words;
                    worldHasReading=false;
                    cueStart=item.ms;
                    cueEnd=i+1<timedLyrics.size()?timedLyrics[i+1].ms:
                        (total>item.ms?total:item.ms+3500);
                    if(item.endMs>item.ms)cueEnd=std::min(cueEnd,item.endMs);
                    if(worldTextScript_==1&&romanizeLyrics_&&!item.romanized.empty()){
                        worldLine=item.romanizedWorld.empty()?item.romanized:item.romanizedWorld;
                        worldHasReading=true;
                    }
                }
            }
            const int font=worldFont_.load();
            if(worldLine!=rasterLine_||font!=rasterFont_||
               rasterWordCount_!=(wordCues?wordCues->size():0)){
                rasterLine_=worldLine;rasterFont_=font;
                rasterWordCount_=wordCues?wordCues->size():0;
                rasterMeshes_.clear();rasterValid_=false;
                if(!worldLine.empty()){
                    EnvScope scope(vm_);
                    if(scope)rasterValid_=rasterizeWorldText(scope.env,worldLine,font,
                        wordCues?std::span<const lm_timing::WordCue>(*wordCues):
                                 std::span<const lm_timing::WordCue>{},rasterMeshes_);
                }
            }
            if(!rasterValid_){
                if(!worldHasReading&&worldItem&&romanizeLyrics_&&
                   !worldItem->romanizedWorld.empty())
                    worldLine=worldItem->romanizedWorld;
                else if(std::any_of(worldLine.begin(),worldLine.end(),
                    [](unsigned char ch){return ch>=128;}))worldLine.clear();
            }
            lm_world3d::publish((lyricsOn_ && wordCues)?worldLine:std::string{},
                                 cueStart,cueEnd,elapsed,playing,
                                 wordCues?std::span<const lm_timing::WordCue>(*wordCues):
                                          std::span<const lm_timing::WordCue>{},
                                 rasterValid_?std::span<const lm_world3d::GlyphMesh>(rasterMeshes_):
                                              std::span<const lm_world3d::GlyphMesh>{},
                                 uint32_t(font)*2U+uint32_t(worldTextScript_.load()));
        }
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
        if(open&&settingsOpen){
            box({0,0,l.w,l.h},0xAA080B11U,0.f);
            box({l.outer.x,l.outer.y+4*u,l.outer.w,l.outer.h},0x99000000U,18.f);
            box(l.outer,0xF51A1D23U,18.f);
            box({l.outer.x+21*u,l.outer.y+17*u,3*u,25*u},0xFF9EDCE7U,2.f);
            text(l.outer.x+34*u,l.outer.y+35*u,"AURELIS / SETTINGS",18*u,C_TEXT,32);
            text(l.outer.x+34*u,l.outer.y+52*u,"Changes are saved automatically",9.2f*u,C_DIM,48);
            box(l.close,0xFF41444CU,8.f);
            icon("x",l.close.x+6*u,l.close.y+6*u,18*u);
            static constexpr const char* tabs[]={"GENERAL","FIRST PERSON","OTHER CAMERAS","STYLE & HUD","EXPERIMENTAL","KEYBINDS"};
            for(int n=0;n<6;++n){
                Rect tab=settingsTab(l,n);const bool active=n==settingsPage;
                box(tab,active?0xFF415661U:0xFF292D34U,8.f);
                text(tab.x+11*u,tab.y+22*u,tabs[n],10.3f*u,
                     active?C_TEXT:C_DIM,24);
            }
            int index=0;
            for(const auto& item:quickSettings){
                if(item.page!=settingsPage)continue;
                const Rect row=settingsRow(l,index++);
                box(row,index%2?0xFF292D35U:0xFF252930U,9.f);
                text(row.x+14*u,row.y+row.h*.60f,item.label,11.5f*u,C_TEXT,45);
                int value=quickValue(item);
                if(item.toggle){
                    const Rect pill{row.x+row.w-70*u,row.y+6*u,57*u,row.h-12*u};
                    box(pill,value?0xFF47747DU:0xFF41454EU,11.f);
                    text(pill.x+13*u,pill.y+pill.h*.70f,value?"ON":"OFF",10.5f*u,C_TEXT,4);
                }else{
                    const float left=row.x+row.w*.52f;
                    const float barWidth=row.w*.48f-80*u;
                    box({left,row.y+row.h*.57f,barWidth,3*u},0xFF48515AU,2.f);
                    box({left,row.y+row.h*.57f,
                        barWidth*float(value-item.low)/float(std::max(1,item.high-item.low)),3*u},
                        0xFF9EDCE7U,2.f);
                    text(row.x+row.w-71*u,row.y+row.h*.63f,"-",14*u,C_TEXT,2);
                    text(row.x+row.w-31*u,row.y+row.h*.63f,"+",14*u,C_TEXT,2);
                    text(left,row.y+row.h*.36f,std::to_string(value),9.8f*u,C_DIM,8);
                }
            }
            if(settingsPage==5){
                static constexpr const char* labels[]={"Open Music Player","Next Song","Previous Song","Pause / Resume Song"};
                for(int i=0;i<4;++i){
                    const Rect row=settingsRow(l,i);
                    box(row,i%2?0xFF252930U:0xFF292D35U,9.f);
                    text(row.x+14*u,row.y+row.h*.60f,labels[i],11.5f*u,C_TEXT,40);
                    const bool waiting=bindingAction==i;
                    const std::string shown=waiting?"PRESS A KEY":keyName(keybinds[static_cast<size_t>(i)]);
                    box({row.x+row.w-145*u,row.y+6*u,132*u,row.h-12*u},
                        waiting?0xFF47747DU:0xFF3B4149U,8.f);
                    text(row.x+row.w-132*u,row.y+row.h*.60f,shown,10.4f*u,C_TEXT,19);
                }
            }
            const Rect advanced=settingsAdvanced(l);
            box(advanced,0xFF3A414AU,8.f);
            text(advanced.x+13*u,advanced.y+20*u,"ADVANCED SETTINGS",10*u,C_TEXT,22);
            pl::modmenu::submitDrawCommands(MODULE,out);
            return;
        }
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
            // Keep synchronized lyrics above the compact HUD even when world text is active.
            if(lyricsOn_ && hudLyrics_ && !timedLyrics.empty() && elapsed>=0) {
                int active=-1;
                for(size_t i=0;i<timedLyrics.size();i++) {
                    if(timedLyrics[i].ms>elapsed)break;
                    active=static_cast<int>(i);
                }
                if(active>=0) {
                    auto drawLyric=[&](int index,float alpha){
                        const Lyric& item=timedLyrics[static_cast<size_t>(index)];
                        if(item.line.empty()||alpha<=0.f)return;
                        const int hudPosition=hudLyricPosition_.load();
                        const bool below=hudPosition>=2;
                        const bool alignRight=(hudPosition&1)!=0;
                        float subtitleY=below?l.mini.y+l.mini.h+26*u:l.mini.y-16*u;
                        float subtitleX=l.mini.x+3*u;
                        if(floatingLyrics_ && !worldLyrics3D_){
                            // Client-only 2D fallback, per-line location (not 3D world text).
                            // True world-space rendering needs version-specific camera hooks.
                            const uint32_t seed=static_cast<uint32_t>(item.ms)*2654435761U+0x9E3779B9U;
                            const float px=static_cast<float>(((seed^(seed>>13))*1274126177U)%1000U)/1000.f;
                            const float py=static_cast<float>(((seed>>7)*747796405U)%1000U)/1000.f;
                            subtitleX=std::clamp(l.w*(.18f+px*.47f),18*u,l.w*.67f);
                            subtitleY=std::clamp(l.h*(.32f+py*.40f),44*u,l.h-36*u);
                        }
                        if(!(floatingLyrics_ && !worldLyrics3D_) && (subtitleY<25*u||subtitleY>l.h-12*u))
                            subtitleY=below?l.mini.y-16*u:l.mini.y+l.mini.h+26*u;
                        if(subtitleY>23*u&&subtitleY<l.h-10*u){
                            const std::string visible=lm_timing::revealedText(item.line,item.words,elapsed);
                            if(visible.empty())return;
                            const size_t shown=lm_timing::revealedCount(item.words,elapsed);
                            const bool entering=!item.words.empty()&&shown>0;
                            const float blend=entering?lm_world3d::smoothstep(
                                float(elapsed-item.words[shown-1].startMs)/180.f):1.f;
                            const std::string prior=entering?lm_timing::revealedText(
                                item.line,item.words,item.words[shown-1].startMs-1):std::string{};
                            const float lyricSize=((floatingLyrics_ && !worldLyrics3D_)?15.8f:14.4f)*u;
                            auto alignedX=[&](std::string_view value,float size){
                                if(!alignRight||(floatingLyrics_&&!worldLyrics3D_))return subtitleX;
                                const float width=float(lm_timing::utf8Length(value))*size*.56f;
                                return std::max(3*u,l.mini.x+l.mini.w-3*u-width);
                            };
                            if(!prior.empty()&&blend<1.f)
                                outlineText(alignedX(prior,lyricSize),subtitleY,prior,lyricSize,alpha*(1.f-blend));
                            outlineText(alignedX(visible,lyricSize),subtitleY,visible,lyricSize,alpha*blend);
                            if(romanizeLyrics_&&!item.romanized.empty()&&subtitleY+15*u<l.h-10*u){
                                const std::string priorReading=!prior.empty()?lm_timing::revealedText(
                                    item.romanized,item.words,item.words[shown-1].startMs-1):std::string{};
                                const std::string reading=lm_timing::revealedText(item.romanized,item.words,elapsed);
                                if(!priorReading.empty()&&blend<1.f)
                                    outlineText(alignedX(priorReading,10.1f*u),subtitleY+15*u,
                                        priorReading,10.1f*u,alpha*.78f*(1.f-blend));
                                outlineText(alignedX(reading,10.1f*u),subtitleY+15*u,
                                    reading,10.1f*u,alpha*.78f*blend);
                            }
                        }
                    };
                    const Lyric& item=timedLyrics[static_cast<size_t>(active)];
                    int end=active+1<static_cast<int>(timedLyrics.size())
                           ?timedLyrics[static_cast<size_t>(active+1)].ms
                           :(total>item.ms?total:INT32_MAX);
                    if(item.endMs>item.ms)end=std::min(end,item.endMs);
                    if(active>0 && elapsed-item.ms<620 && elapsed>=item.ms)
                        drawLyric(active-1,1.f-lm_world3d::smoothstep((elapsed-item.ms)/620.f));
                    if(end==INT32_MAX || elapsed<end+620) {
                        float fadeIn=lm_world3d::smoothstep((elapsed-item.ms)/270.f);
                        float fadeOut=end==INT32_MAX?1.f:1.f-
                            lm_world3d::smoothstep((elapsed-end)/620.f);
                        drawLyric(active,std::clamp(fadeIn*fadeOut,0.f,1.f));
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
        text(l.outer.x+57*u,l.outer.y+33*u,"LEVI MUSIC - AURELIS ONLINE",15*u,C_TEXT);
        text(l.outer.x+57*u,l.outer.y+48*u,"v1.6.15 BETA",9.2f*u,C_DIM);
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
        if(worldLyrics3D_)
            text(l.left.x+17*u,l.left.y+149*u,lm_world3d::status(),9.2f*u,C_MINT,48);

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
        box(l.repeat,repeatMode?0xFF49616CU:0xFF2B3138U,9.f);
        icon("repeat-2",l.repeat.x+8*u,l.repeat.y+8*u,18*u);
        if(repeatMode==2)text(l.repeat.x+24*u,l.repeat.y+12*u,"1",7.5f*u,C_TEXT,2);
        const bool canShuffle=bridgePlaylist||playlistQueue;
        box(l.shuffle,shuffle?0xFF49616CU:(canShuffle?0xFF2B3138U:0xFF20242AU),9.f);
        icon("shuffle",l.shuffle.x+8*u,l.shuffle.y+8*u,18*u);
        icon("volume-2",l.left.x+16*u,l.left.y+l.left.h-14*u,12*u);
        text(l.left.x+34*u,l.left.y+l.left.h-4*u,
             streamingBridge?("AUTOPLAY NEXT • BRIDGE"):
#if defined(LEVI_STANDALONE)
             ("AURELIS • "+std::to_string(volume)+"%"),
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
                const float suffixWidth=addControl?125*u:(isRadio&&n>0)||isSaved?45*u:12*u;
                const size_t maxChars=static_cast<size_t>(
                    std::max(13.f,(row.w-16*u-suffixWidth)/(7.1f*u)));
                float labelX=row.x+10*u;
                if(!songs[n].imageId.empty()){
                    pl::modmenu::DrawCommand art;
                    art.type=pl::modmenu::DrawCommandType::Image;art.imageId=songs[n].imageId;
                    art.x=row.x+7*u;art.y=row.y+4*u;art.w=30*u;art.h=30*u;art.color=0xFFFFFFFFU;
                    out.push_back(std::move(art));labelX=row.x+44*u;
                }else{disc(row.x+22*u,row.y+row.h*.5f,11*u,0xFF3B4048U);labelX=row.x+40*u;}
                text(labelX,row.y+std::min(row.h-5*u,15*u),
                     prefix+songs[n].title,10.9f*u,C_TEXT,maxChars);
                text(labelX,row.y+std::min(row.h-3*u,30*u),
                     songs[n].artist.empty()?"Unknown artist":songs[n].artist,8.8f*u,C_DIM,maxChars);
                if(isRadio && n>0){
                    icon("x",row.x+row.w-28*u,row.y+5*u,18*u);
                }else if(isSaved){
                    icon("x",row.x+row.w-28*u,row.y+5*u,18*u);
                }else if(addControl){
                    text(row.x+row.w-119*u,row.y+std::min(row.h-3*u,19*u),
                         "DL",10*u,C_TEXT,3);
                    text(row.x+row.w-73*u,row.y+std::min(row.h-3*u,19*u),
                         "P+",11*u,C_TEXT,3);
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
                const float step=std::max(33*u,
                    std::min(49*u,(l.right.h-111*u)/6.f));
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
                    if(romanizeLyrics_&&!timedLyrics[static_cast<size_t>(j)].romanized.empty())
                        text(l.right.x+22*u,y+12*u,timedLyrics[static_cast<size_t>(j)].romanized,
                             9.1f*u,C_DIM,lineLength);
                }
                text(l.right.x+16*u,l.right.y+l.right.h-12*u,
                     "LRC "+std::to_string(std::max(0,activeLine+1))+
                     " / "+std::to_string(count),10*u,C_DIM,36);
            }
        }else if(rightTab==0){
            box(l.searchBox,0xFF282B30U,8.f);
            text(l.searchBox.x+10*u,l.searchBox.y+22*u,
                 query.empty()?"Search title, song URL, or public playlist URL...":
                    ((typing?"_ ":"")+query),
                 11.9f*u,typing?C_TEXT:C_DIM,63);
            box(l.searchGo,0xFF50555EU,8.f);
            icon("search",l.searchGo.x+13*u,l.searchGo.y+7*u,20*u);
            text(l.right.x+15*u,l.right.y+106*u,
                 "SEARCH RESULTS  •  Tap play | DL offline | P+ playlist | + queue",
                 10*u,C_DIM,75);
            if(tracks.empty()){
                text(l.right.x+18*u,l.right.y+169*u,
                     "Search for a song to start your radio.",12*u,C_DIM,57);
            }else drawSongs(tracks,false,true,false);
        }else if(rightTab==1){
            box(l.queueNow,libraryMode==3?0xFF50555EU:0xFF2C3037U,7.f);
            text(l.queueNow.x+11*u,l.queueNow.y+20*u,
                 "UP NEXT",10.5f*u,libraryMode==3?C_TEXT:C_DIM,15);
            box(l.queueSaved,(libraryMode==1||libraryMode==2)?0xFF50555EU:0xFF2C3037U,7.f);
            text(l.queueSaved.x+10*u,l.queueSaved.y+20*u,
                 "PLAYLISTS",10.5f*u,
                 libraryMode!=3?C_TEXT:C_DIM,14);
            if(libraryMode==3){
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
                     typing?("_ "+query):(query.empty()?"Name or YouTube playlist URL...":query),
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
                    text(row.x+row.w-61*u,row.y+std::min(19*u,row.h-3*u),"R",11*u,C_DIM,2);
                    text(row.x+row.w-27*u,row.y+std::min(19*u,row.h-3*u),"X",11*u,C_DIM,2);
                }
                drawPage(playlistNames.size(),"PLAYLISTS");
            }else{
                text(l.right.x+16*u,l.right.y+103*u,
                     activePlaylist.empty()?"SAVED PLAYLIST":activePlaylist,
                     12.6f*u,C_TEXT,56);
                drawSongs(tracks,false,false,true);
                text(l.right.x+16*u,l.right.y+l.right.h-15*u,
                     "Tap song to play • X removes it • Search P+ adds songs",
                     9.4f*u,C_DIM,70);
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
                if(!standalonePlaylistQueue_&&!standaloneQueueSelection_){
                    queueTracks_.clear();queueTracks_.push_back(tr);
                }
                else if(queueTracks_.empty()||queueTracks_.front().id!=tr.id){
                    queueTracks_.erase(std::remove_if(queueTracks_.begin(),queueTracks_.end(),
                        [&](const Track& item){return item.id==tr.id;}),queueTracks_.end());
                    queueTracks_.insert(queueTracks_.begin(),tr);
                }
                standaloneQueueSelection_=false;
                vol=volume_;key=apiKey_;playing_=false;elapsed_=0;total_=0;
                lyrics_.clear();lyricsSource_.clear();lyric_.clear();++lyricSerial_;
            }
            const bool offline=localTrack(tr);
#if defined(LEVI_STANDALONE)
            std::string server;
            {std::lock_guard lock(mu_);server=chruBase_;}
            std::string mediaUrl;
            if(offline){
                const auto name=tr.id.substr(8);
                const auto folder=self_.getConfigDir()/"music";
                auto path=folder/name;
                if(name.empty()||name=="."||name==".."||name.find('/')!=std::string::npos||
                   name.find('\\')!=std::string::npos||!allowedAudio(path)||
                   std::filesystem::is_symlink(path)||!std::filesystem::is_regular_file(path)){
                    update(tr.title,tr.artist,"Local music file unavailable");return;
                }
                mediaUrl=path.string();
            }else{
                update(tr.title,tr.artist,"Checking Aurelis audio stream...");
                mediaUrl=aurelisStream(env,server,tr.id);
            }
            if(mediaUrl.empty()) {
                update(tr.title,tr.artist,"Aurelis stream unavailable for this song");
                return;
            }
            bool ok=player.startUrl(env,mediaUrl,vol,true);
            if(!ok){
                update(tr.title,tr.artist,"Android could not play this Aurelis stream");
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
                playerPositionStamp_=std::chrono::steady_clock::now();
            }
#if defined(LEVI_STANDALONE)
            update(tr.title,tr.artist,offline?"Playing local file":"Aurelis Symphony streaming");
            if(!tr.art.empty()){
                std::vector<Track> one{tr};loadTrackCovers(env,one);
                std::lock_guard lock(mu_);coverImageId_=one.front().imageId;coverUrl_=tr.art;
            }
#else
            update(tr.title,tr.artist,"Streaming online (Audius)");
#endif
            if(offline){
                auto path=self_.getConfigDir()/"music"/tr.id.substr(8);
                path.replace_extension(".lrc");
                std::vector<Lyric> local;
                if(std::filesystem::is_regular_file(path)&&std::filesystem::file_size(path)<500'000){
                    std::ifstream file(path);std::string raw((std::istreambuf_iterator<char>(file)),{});
                    local=parseLrc(raw);
                }
                std::lock_guard lock(mu_);
                lyricsSource_=local;lyrics_=std::move(local);lyric_.clear();
                lyricChanged_=std::chrono::steady_clock::now();
            }else queueLyrics(tr,player.duration(env),key,
#if defined(LEVI_STANDALONE)
                        true
#else
                        false
#endif
            );
#if defined(LEVI_STANDALONE)
            bool needSuggestions;
            {std::lock_guard lock(mu_);
                needSuggestions=!standalonePlaylistQueue_&&queueTracks_.size()<4;}
            if(needSuggestions&&!offline){
                std::string server;{std::lock_guard lock(mu_);server=chruBase_;}
                auto suggestions=searchAurelis(env,server,tr.title,tr.artist);
                std::lock_guard lock(mu_);
                for(auto& item:suggestions){
                    if(queueTracks_.size()>=4)break;
                    if(std::none_of(queueTracks_.begin(),queueTracks_.end(),
                       [&](const Track& saved){return saved.id==item.id;}))
                        queueTracks_.push_back(std::move(item));
                }
            }
#endif
        };
        bool standaloneWasPlaying=false;
        auto lastBridge=std::chrono::steady_clock::now()-std::chrono::seconds(2);
        auto lastAutoBridge=std::chrono::steady_clock::now()-std::chrono::seconds(10);
        auto lastQueue=std::chrono::steady_clock::now()-std::chrono::seconds(3);
#if !defined(LEVI_STANDALONE)
        auto lastBeat=std::chrono::steady_clock::now()-std::chrono::seconds(3);
        auto lastImmersiveSent=std::chrono::steady_clock::now()-std::chrono::seconds(4);
        bool beatSent=false;
        int lastImmersive=-1;
#endif
        bool sawWorld=false,lastInWorld=false,pausedForWorld=false,pausedBridge=false;
        std::string previousId,previousTitle,previousArtist,previousProvider,previousArt;
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
            // Hook preparation is separate from playback and never scans the
            // entire game library during a music command.
            bool musicPlaying=false;
            {std::lock_guard lock(mu_);musicPlaying=playing_;}
            lm_cinematic::setPlaying(musicPlaying);
            lm_cinematic::diagnose();
#if defined(LEVI_STANDALONE)
            lm_world3d::request(worldLyrics3D_||cinematicAfk_,true);
#else
            lm_world3d::request(worldLyrics3D_||cinematicAfk_||immersiveSound_,true);
            const int desiredEnvironment=immersiveSound_?
                static_cast<int>(lm_immersive::current()):0;
            if(!useBridge_)player.setEnvironment(env,desiredEnvironment);
#endif
            const bool inWorld=lm_world3d::inWorld();
            if(!sawWorld&&inWorld){sawWorld=true;lastInWorld=true;}
            else if(sawWorld&&inWorld!=lastInWorld){
                if(!inWorld){
                    if(useBridge_){
                        std::string key;bool wasPlaying;
                        {std::lock_guard lock(mu_);key=bridgeKey_;wasPlaying=playing_;}
                        if(wasPlaying&&!key.empty()&&
                           !bridgeGet("/v1/control?action=pause",key).empty()){
                            pausedForWorld=true;pausedBridge=true;
                        }
                    }else if(player.isPlaying(env)){
                        player.toggle(env);pausedForWorld=true;pausedBridge=false;
                        std::lock_guard lock(mu_);playing_=false;
                    }
                    __android_log_print(ANDROID_LOG_INFO,TAG,
                        "world exited; playback auto-pause=%d",int(pausedForWorld));
                }else if(pausedForWorld){
                    if(pausedBridge){
                        std::string key;{std::lock_guard lock(mu_);key=bridgeKey_;}
                        if(!key.empty())bridgeGet("/v1/control?action=play",key);
                    }else if(!player.isPlaying(env)&&player.position(env)>0)player.toggle(env);
                    pausedForWorld=false;
                    __android_log_print(ANDROID_LOG_INFO,TAG,"world entered; playback resumed");
                }
                lastInWorld=inWorld;
            }
            if(lyricsRebuild_.exchange(false)){
#if defined(LEVI_STANDALONE)
                if(!useBridge_){
                    std::lock_guard lock(mu_);
                    if(!lyricsSource_.empty()){
                        lyricRequest_={};
                        lyricRequest_.original=lyricsSource_;
                        lyricRequest_.rebuild=true;
                        lyricRequest_.standalone=true;
                        lyricRequest_.server=chruBase_;
                        lyricRequest_.language=romanizationLanguage_;
                        lyricRequest_.serial=++lyricSerial_;
                        lyricPending_=true;
                        lyricCv_.notify_one();
                    }
                }else
#endif
                {
                std::vector<Lyric> raw;
                {std::lock_guard lock(mu_);raw=lyricsSource_;}
                if(romanizeLyrics_){
                    romanizeLyrics(env,raw);
                }
                {
                    std::lock_guard lock(mu_);
                    lyrics_=std::move(raw);
                    lyric_.clear();
                    for(const auto& item:lyrics_){
                        if(item.ms>elapsed_)break;
                        lyric_=item.line;
                    }
                    lyricChanged_=std::chrono::steady_clock::now();
                }
                }
            }
            if(useBridge_) {
                std::string key;
                {std::lock_guard lock(mu_);key=bridgeKey_;}
#if !defined(LEVI_STANDALONE)
                const auto immersiveNow=std::chrono::steady_clock::now();
                if(!key.empty()&&(desiredEnvironment!=lastImmersive||
                   immersiveNow-lastImmersiveSent>std::chrono::seconds(3))){
                    const auto value=lm_immersive::name(
                        static_cast<lm_immersive::Environment>(desiredEnvironment));
                    if(!bridgeGet("/v1/immersive?environment="+encode(env,value),key).empty()){
                        lastImmersive=desiredEnvironment;
                        lastImmersiveSent=immersiveNow;
                    }
                }
#endif
                if(std::chrono::steady_clock::now()-lastAutoBridge>std::chrono::seconds(8)){
                    lastAutoBridge=std::chrono::steady_clock::now();
                    const std::string found=automaticBridgeToken(env);
                    if(validToken(found)&&found!=key&&!bridgeGet("/v1/health",found).empty()){
                        key=found;
                        try{
                            std::filesystem::create_directories(self_.getConfigDir());
                            std::ofstream file(self_.getConfigDir()/"bridge.token",std::ios::trunc);
                            if(file)file<<found;
                        }catch(...){}
                        {std::lock_guard lock(mu_);bridgeKey_=found;}
                        update("LeviMusic connected","Android Bridge","Connected automatically");
                    }
                }
#if defined(LEVI_STANDALONE)
            lm_world3d::experimentalBeats=false;
            lm_world3d::beatState=0;
#else
            const bool wantBeat=lm_world3d::experimentalBeats.load()&&worldLyrics3D_.load()
                &&useBridge_.load();
            if(!wantBeat)lm_world3d::beatState=0;
            else if(key.empty())lm_world3d::beatState=2;
            auto beatNow=std::chrono::steady_clock::now();
            if(!key.empty()&&(wantBeat!=beatSent||
                (wantBeat&&beatNow-lastBeat>std::chrono::milliseconds(180)))){
                std::string payload=bridgeGet(wantBeat?"/v1/beat?enabled=1":"/v1/beat?enabled=0",key);
                lastBeat=beatNow;
                if(!payload.empty()){
                    beatSent=wantBeat;
                    auto root=makeJson(env,payload);
                    if(root){
                        const bool available=optString(env,root,"available")=="true";
                        const bool needPermission=optString(env,root,"permissionRequired")=="true";
                        lm_world3d::beatState=needPermission?1:(available?3:2);
                        try{lm_world3d::beatStrength=available?
                            std::clamp(std::stof(optString(env,root,"strength")),0.f,1.f):0.f;}
                        catch(...){lm_world3d::beatStrength=0.f;}
                        if(available)try{
                            auto next=std::stoull(optString(env,root,"beatCount"));
                            if(next!=lm_world3d::beatPulseSerial.load()){
                                lm_world3d::beatPulseSerial=next;
                                lm_world3d::beatPulseTime=lm_world3d::nowMs();
                            }
                        }catch(...){}
                        if(available)lm_world3d::beatUpdated=lm_world3d::nowMs();
                        env->DeleteLocalRef(root);
                    }
                }
            }
#endif
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
                            loadTrackCovers(env,songs);
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
                            bool importing=job.query.rfind("https://",0)==0;
                            auto reply=importing?bridgeGet("/v1/playlist/import?url="+
                                encode(env,job.query),key):bridgeGet("/v1/playlist/create?name="+
                                encode(env,job.query),key);
                            auto root=makeJson(env,reply);
                            bool accepted=root&&optString(env,root,"accepted")=="true";
                            std::string importedName=importing&&root?optString(env,root,"name"):job.query;
                            if(root)env->DeleteLocalRef(root);
                            if(!accepted)update("Playlist creation failed",job.query,"Check name or app");
                            auto names=parsePlaylistNames(env,bridgeGet("/v1/playlists",key));
                            {std::lock_guard lock(mu_);playlistNames_=std::move(names);
                                libraryMode_=1;page_=0;query_.clear();selected_=-1;
                                if(accepted)activePlaylist_=importedName;
                            }
                            break;
                        }
                        case Action::PlaylistRename:
                        case Action::PlaylistDelete:{
                            std::string old=job.query,newName;
                            if(job.action==Action::PlaylistRename){
                                auto split=old.find('\n');if(split==std::string::npos)break;
                                newName=old.substr(split+1);old.resize(split);
                            }
                            if(old=="Offline"){
                                update("Offline playlist is automatic",old,"Manage saved tracks instead");break;
                            }
                            auto path=job.action==Action::PlaylistRename?
                                "/v1/playlist/rename?name="+encode(env,old)+"&newName="+encode(env,newName):
                                "/v1/playlist/delete?name="+encode(env,old);
                            auto reply=bridgeGet(path,key);
                            auto root=makeJson(env,reply);
                            bool accepted=root&&optString(env,root,"accepted")=="true";
                            if(root)env->DeleteLocalRef(root);
                            if(!accepted){update("Playlist change failed",old,"Check the Bridge app");break;}
                            auto names=parsePlaylistNames(env,bridgeGet("/v1/playlists",key));
                            {std::lock_guard lock(mu_);playlistNames_=std::move(names);
                                if(activePlaylist_==old)activePlaylist_=newName;
                                libraryMode_=1;page_=0;query_.clear();selected_=-1;}
                            update("Playlist updated",old,"Saved locally");break;
                        }
                        case Action::PlaylistAdd: {
                            std::string name,id=job.query;
                            {std::lock_guard lock(mu_);name=activePlaylist_;}
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
                        case Action::OfflineSave:{
                            if(key.empty()){update("Bridge unavailable","Offline","Open the Bridge app");break;}
                            update("Saving offline",job.query,"Downloading audio and available lyrics...");
                            auto reply=bridgeGet("/v1/offline/save?id="+encode(env,job.query),key);
                            auto root=makeJson(env,reply);
                            bool accepted=root&&optString(env,root,"accepted")=="true";
                            auto message=root?optString(env,root,"message"):"Bridge download timed out";
                            if(root)env->DeleteLocalRef(root);
                            update(accepted?"Saved to Offline":"Offline save failed",job.query,message);
                            break;
                        }
                        case Action::PlaylistRemove: {
                            std::string name;
                            {std::lock_guard lock(mu_);name=activePlaylist_;}
                            if(name.empty()||job.query.empty())break;
                            auto reply=bridgeGet("/v1/playlist/remove?name="+encode(env,name)
                                +"&id="+encode(env,job.query),key);
                            auto root=makeJson(env,reply);
                            bool accepted=root&&optString(env,root,"accepted")=="true";
                            if(root)env->DeleteLocalRef(root);
                            if(accepted){
                                auto songs=parseBridgeSearch(env,bridgeGet(
                                    "/v1/playlist/tracks?name="+encode(env,name),key));
                                loadTrackCovers(env,songs);
                                std::lock_guard lock(mu_);
                                results_=std::move(songs);
                                page_=std::min(page_,std::max(0,int((results_.size()+4)/5)-1));
                                selected_=-1;
                            }else update("Remove failed",name,"Retry from the playlist");
                            break;
                        }
                        case Action::QueueRefresh:{
                            if(key.empty())break;
                            const auto payload=bridgeGet("/v1/queue",key);
                            if(!payload.empty()){
                                auto songs=parseBridgeSearch(env,payload);
                                loadTrackCovers(env,songs);
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
                            loadTrackCovers(env,updated);
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
                        case Action::RepeatCycle: action="repeat_cycle";break;
                        case Action::ShuffleToggle: {
                            bool allowed;{std::lock_guard lock(mu_);allowed=bridgePlaylistQueue_;}
                            if(!allowed){update("Shuffle needs a playlist","Now Playing",
                                "Start a saved playlist before enabling shuffle");break;}
                            action="shuffle_toggle";break;
                        }
                        case Action::Seek: action="seek&position="+job.query;break;
                        case Action::Search: {
                            if(job.query.empty()){
                                std::lock_guard lock(mu_);
                                results_.clear();selected_=-1;page_=0;
                            } else if(!key.empty()){
                                std::string query=job.query.substr(0,job.query.rfind("https://",0)==0?512:160);
                                update("Searching",job.query,"Searching via LeviMusic companion...");
                                std::string reply=bridgeGet("/v1/search?query="+encode(env,query),key);
                                auto found=parseBridgeSearch(env,reply);
                                loadTrackCovers(env,found);
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
                            previousId.clear();previousTitle.clear();previousArtist.clear();previousProvider.clear();previousArt.clear();
                        }
                        lastBridgeConnected=false;
                        {
                            std::lock_guard lock(mu_);
                            title_="LeviMusic";
                            artist_="Spotify / YouTube Music";
                            info_=key.empty()?"Paste Bridge Key in LeviMusic settings":
                                  "Start LeviMusic Bridge; grant Notification Access";
                            playing_=false;elapsed_=0;total_=0;lyric_.clear();lyrics_.clear();lyricsSource_.clear();coverImageId_.clear();coverImageId_.clear();++lyricSerial_;
                        }
                    } else if(!state.available) {
                        lastBridgeConnected=true;
                        previousId.clear();previousTitle.clear();previousArtist.clear();previousProvider.clear();
                        {
                            std::lock_guard lock(mu_);
                            title_="Choose a song";
                            artist_="Spotify / YouTube Music";
                            info_="Start music in the official app";
                            playing_=false;elapsed_=0;total_=0;lyric_.clear();lyrics_.clear();lyricsSource_.clear();++lyricSerial_;
                        }
                    } else {
                        lastBridgeConnected=true;
                        const bool changed=state.id!=previousId||state.title!=previousTitle ||
                                          state.artist!=previousArtist || state.provider!=previousProvider;
                        if(changed) {
                            previousId=state.id;
                            previousTitle=state.title;previousArtist=state.artist;previousProvider=state.provider;
                            {
                                std::lock_guard lock(mu_);
                                lyrics_.clear();lyricsSource_.clear();lyric_.clear();
                            }
                        }
                        {
                            std::lock_guard lock(mu_);
                            title_=state.title.empty()?"Unknown track":state.title;
                            artist_=state.artist.empty()?state.provider:state.artist;
                            info_=state.provider+" • Music session connected";
                            playing_=state.playing;elapsed_=state.elapsed;total_=state.duration;
                            repeatMode_=state.repeatMode=="one"?2:state.repeatMode=="off"?0:1;
                            shuffle_=state.shuffle;bridgePlaylistQueue_=state.queueMode=="playlist";
                            bridgePositionStamp_=std::chrono::steady_clock::now();
                        }
                        if(state.art!=previousArt || changed){
                            previousArt=state.art;
                            bool good=false;std::string imageId;
                            if(!state.art.empty()){
                                Track cover;cover.art=state.art;std::vector<Track> one{cover};
                                loadTrackCovers(env,one);imageId=one.front().imageId;good=!imageId.empty();
                            }
                            {
                                std::lock_guard lock(mu_);
                                coverImageId_=good?imageId:"";
                                coverUrl_=state.art;
                            }
                        }
                        if(changed && !state.title.empty()) {
                            Track track;
                            track.id=state.id;track.title=state.title;track.artist=state.artist;
                            queueLyrics(track,state.duration,key);
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
                        update("Searching",job.query,"Checking local music and Aurelis Symphony...");
                        std::string server;
                        {std::lock_guard lock(mu_);server=chruBase_;}
                        std::vector<Track> found;
                        if(youtubeUrl(job.query)&&job.query.find("list=")!=std::string::npos){
                            found=importYoutubePlaylist(env,job.query).tracks;
                        }else if(youtubeUrl(job.query)){
                            auto direct=resolveYoutubeTrack(env,job.query);
                            if(!direct.id.empty())found.push_back(std::move(direct));
                        }else{
                            found=offlineTracks(self_.getConfigDir()/"music",job.query);
                            if(job.query!="offline"){
                                auto online=searchAurelis(env,server,job.query);
                                found.insert(found.end(),online.begin(),online.end());
                            }
                        }
#else
                        update("Searching",job.query,"Contacting Audius...");
                        std::string key;
                        {std::lock_guard lock(mu_);key=apiKey_;}
                        auto found=searchAudiusFrom(env,job.query,API,key);
                        if(found.empty())
                            found=searchAudiusFrom(env,job.query,"https://discoveryprovider.audius.co/v1",key);
#endif
                        loadTrackCovers(env,found);
                        {
                            std::lock_guard lock(mu_);
                            results_=std::move(found);
                            searchCache_=results_;
                            selected_=-1;
                            page_=0;
                        }
                        if(!searchCache_.empty())update("Search results",job.query,
                            std::to_string(searchCache_.size())+" songs found");
#if defined(LEVI_STANDALONE)
                        else update("Aurelis search unavailable / no results",job.query,
                                    "Check internet or try a different song");
#else
                        else update("No results",job.query,"Please check internet/API key");
#endif
                        break;
                    }
                    case Action::Select:{
#if defined(LEVI_STANDALONE)
                        {std::lock_guard lock(mu_);standalonePlaylistQueue_=false;shuffle_=false;playlistOrder_.clear();}
#endif
                        playSelected();break;
                    }
#if defined(LEVI_STANDALONE)
                    case Action::OfflineSave:{
                        Track track;std::string server;
                        {std::lock_guard lock(mu_);
                            auto found=std::find_if(results_.begin(),results_.end(),
                                [&](const Track& item){return item.id==job.query;});
                            if(found!=results_.end())track=*found;
                            server=chruBase_;
                        }
                        if(track.id.empty()||localTrack(track)||track.id.size()>80){
                            update("Offline save unavailable",track.title,"Choose an online search result");break;
                        }
                        auto folder=self_.getConfigDir()/"music";
                        auto existing=offlineTracks(folder);
                        if(std::any_of(existing.begin(),existing.end(),[&](const Track& t){
                            return t.id.rfind("offline:"+track.id+".",0)==0;})){
                            update("Already saved offline",track.title,"Open Playlists > Offline");break;
                        }
                        update("Saving offline",track.title,"Downloading audio...");
                        auto source=aurelisStream(env,server,track.id,true);
                        auto saved=downloadAudio(env,source,folder,track.id);
                        if(saved.empty()){
                            update("Offline save failed",track.title,"Audio unavailable or unsupported format");break;
                        }
                        try{
                            std::ofstream meta(folder/(track.id+".meta"),std::ios::trunc);
                            meta<<std::quoted(track.title)<<' '
                                <<std::quoted(track.artist)<<' '<<std::quoted(track.art)<<'\n';
                        }catch(...){}
                        update("Saving lyrics",track.title,"Checking synchronized LRC...");
                        auto lyrics=aurelisLyrics(env,server,track,0);
                        if(lyrics.empty())lyrics=searchLyrics(env,track);
                        if(!lyrics.empty()){
                            auto target=folder/(track.id+".lrc");
                            std::ofstream lrc(target,std::ios::trunc);
                            for(const auto& line:lyrics){
                                int hundredths=std::max(0,line.ms)/10;
                                lrc<<'['<<std::setfill('0')<<std::setw(2)<<hundredths/6000
                                   <<':'<<std::setw(2)<<(hundredths/100)%60
                                   <<'.'<<std::setw(2)<<hundredths%100<<']'
                                   <<lm_lyric::normalize(line.line)<<'\n';
                            }
                        }
                        update("Saved to Offline",track.title,
                            lyrics.empty()?"Audio saved; no synchronized lyrics found":
                                           "Audio and synchronized lyrics saved");
                        break;
                    }
#endif
                    case Action::PlayPause:
                        if(player.isPlaying(env)){
                            player.toggle(env);{std::lock_guard lock(mu_);playing_=false;}
                        }else if(player.position(env)>0){
                            player.toggle(env);{std::lock_guard lock(mu_);playing_=true;}
                        }else playSelected();
                        break;
                    case Action::RepeatCycle:{
                        std::lock_guard lock(mu_);repeatMode_=repeatMode_==1?2:repeatMode_==2?0:1;
                        break;
                    }
                    case Action::ShuffleToggle:{
                        bool okay=false;
                        {
                            std::lock_guard lock(mu_);
                            if(standalonePlaylistQueue_&&!playlistOrder_.empty()&&selected_>=0&&selected_<int(results_.size())){
                                shuffle_=!shuffle_;
                                Track current=results_[static_cast<size_t>(selected_)];
                                results_=playlistOrder_;
                                if(shuffle_){
                                    std::erase_if(results_,[&](const Track& item){return item.id==current.id;});
                                    std::mt19937 rng(static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
                                    std::shuffle(results_.begin(),results_.end(),rng);
                                    results_.insert(results_.begin(),current);
                                }
                                selected_=shuffle_?0:int(std::find_if(results_.begin(),results_.end(),
                                    [&](const Track& item){return item.id==current.id;})-results_.begin());
                                queueTracks_.clear();const int n=static_cast<int>(results_.size());
                                for(int i=0;i<n&&i<40;++i)queueTracks_.push_back(
                                    results_[static_cast<size_t>((selected_+i)%n)]);
                                okay=true;
                            }
                        }
                        if(!okay)update("Shuffle needs a playlist","Now Playing",
                            "Start a saved playlist before enabling shuffle");
                        break;
                    }
                    case Action::Previous:
                    case Action::Next:{
                        {
                            std::lock_guard lock(mu_);
#if defined(LEVI_STANDALONE)
                            if(standalonePlaylistQueue_&&!results_.empty()){
                                const int n=static_cast<int>(results_.size());
                                selected_=(selected_+(job.action==Action::Next?1:n-1))%n;
                                queueTracks_.clear();
                                for(int i=0;i<n&&i<40;++i)
                                    queueTracks_.push_back(results_[static_cast<size_t>((selected_+i)%n)]);
                                standaloneQueueSelection_=false;page_=selected_/5;
                            }else if(job.action==Action::Next&&queueTracks_.size()>1){
                                Track next=queueTracks_[1];queueTracks_.erase(queueTracks_.begin());
                                results_.push_back(next);selected_=static_cast<int>(results_.size())-1;
                                standaloneQueueSelection_=true;page_=selected_/5;
                            }else
#endif
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
                            playing_=false;elapsed_=0;total_=0;lyrics_.clear();lyricsSource_.clear();lyric_.clear();
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
#if defined(LEVI_STANDALONE)
                    case Action::PlaylistList:{
                        std::vector<std::string> names;
                        names.push_back("Offline");
                        for(const auto& item:localPlaylists_)names.push_back(item.first);
                        {std::lock_guard lock(mu_);playlistNames_=std::move(names);
                            libraryMode_=1;page_=0;selected_=-1;results_.clear();}
                        break;
                    }
                    case Action::PlaylistCreate:{
                        std::string name=job.query;
                        if(name.rfind("https://",0)==0){
                            auto imported=importYoutubePlaylist(env,name);
                            if(imported.tracks.empty()){
                                update("Playlist import failed","YouTube","Public playlist unavailable or empty");break;
                            }
                            name=imported.name;
                            if(name.size()>96)name.resize(96);
                            if(localPlaylists_.contains(name)){
                                update("Playlist already imported",name,"Rename it before importing again");break;
                            }
                            localPlaylists_[name]=std::move(imported.tracks);
                            if(!saveLocalPlaylists()){
                                localPlaylists_.erase(name);update("Playlist save failed",name,"Check storage");break;
                            }
                            std::vector<std::string> names{"Offline"};
                            for(const auto& item:localPlaylists_)names.push_back(item.first);
                            {std::lock_guard lock(mu_);playlistNames_=std::move(names);
                                activePlaylist_=name;libraryMode_=1;page_=0;query_.clear();}
                            update("Playlist imported",name,"Saved locally with "+
                                std::to_string(localPlaylists_[name].size())+" tracks");break;
                        }
                        name.erase(0,name.find_first_not_of(" \t\r\n"));
                        auto end=name.find_last_not_of(" \t\r\n");
                        if(end!=std::string::npos)name.erase(end+1);
                        if(name.empty()||name.size()>96||name.find_first_of("\r\n")!=std::string::npos){
                            update("Playlist name invalid","Library","Enter a shorter name");break;
                        }
                        if(localPlaylists_.size()>=40&&!localPlaylists_.contains(name)){
                            update("Playlist limit reached",name,"Maximum 40 playlists");break;
                        }
                        if(localPlaylists_.contains(name)){
                            update("Playlist exists",name,"Open it from the list");break;
                        }
                        localPlaylists_[name]={};
                        if(!saveLocalPlaylists()){
                            localPlaylists_.erase(name);
                            update("Playlist save failed",name,"Check free storage");break;
                        }
                        std::vector<std::string> names{"Offline"};
                        for(const auto& item:localPlaylists_)names.push_back(item.first);
                        {std::lock_guard lock(mu_);playlistNames_=std::move(names);
                            activePlaylist_=name;libraryMode_=1;page_=0;query_.clear();selected_=-1;}
                        update("Playlist created",name,"Open it or tap P+ in Search to add songs");
                        break;
                    }
                    case Action::PlaylistRename:{
                        auto split=job.query.find('\n');
                        if(split==std::string::npos)break;
                        auto old=job.query.substr(0,split),name=job.query.substr(split+1);
                        if(old=="Offline"||name.empty()||name.size()>96||
                            localPlaylists_.contains(name)||name=="Offline"){
                            update("Rename unavailable",old,"Choose a unique playlist name");break;
                        }
                        auto node=localPlaylists_.extract(old);
                        if(node.empty())break;
                        node.key()=name;localPlaylists_.insert(std::move(node));
                        if(!saveLocalPlaylists()){
                            auto revert=localPlaylists_.extract(name);
                            revert.key()=old;localPlaylists_.insert(std::move(revert));
                            update("Rename failed",old,"Check storage");break;
                        }
                        std::vector<std::string> names{"Offline"};
                        for(const auto& p:localPlaylists_)names.push_back(p.first);
                        {std::lock_guard lock(mu_);playlistNames_=std::move(names);
                            if(activePlaylist_==old)activePlaylist_=name;query_.clear();}
                        update("Playlist renamed",name,"Saved locally");break;
                    }
                    case Action::PlaylistDelete:{
                        if(job.query=="Offline")break;
                        auto node=localPlaylists_.extract(job.query);
                        if(node.empty())break;
                        if(!saveLocalPlaylists()){
                            localPlaylists_.insert(std::move(node));
                            update("Delete failed",job.query,"Check storage");break;
                        }
                        std::vector<std::string> names{"Offline"};
                        for(const auto& p:localPlaylists_)names.push_back(p.first);
                        {std::lock_guard lock(mu_);playlistNames_=std::move(names);
                            if(activePlaylist_==job.query)activePlaylist_.clear();}
                        update("Playlist deleted",job.query,"Songs were not deleted");break;
                    }
                    case Action::PlaylistOpen:{
                        if(job.query=="Offline"){
                            auto songs=offlineTracks(self_.getConfigDir()/"music");
                            loadTrackCovers(env,songs);
                            std::lock_guard lock(mu_);results_=std::move(songs);
                            activePlaylist_="Offline";libraryMode_=2;page_=0;selected_=-1;
                            break;
                        }
                        auto it=localPlaylists_.find(job.query);
                        if(it==localPlaylists_.end())break;
                        auto songs=it->second;loadTrackCovers(env,songs);
                        {std::lock_guard lock(mu_);results_=std::move(songs);activePlaylist_=job.query;
                            libraryMode_=2;page_=0;selected_=-1;}
                        break;
                    }
                    case Action::PlaylistAdd:{
                        std::string name;Track track;
                        {std::lock_guard lock(mu_);name=activePlaylist_;
                            auto it=std::find_if(searchCache_.begin(),searchCache_.end(),
                                [&](const Track& song){return song.id==job.query;});
                            if(it!=searchCache_.end())track=*it;
                        }
                        auto it=localPlaylists_.find(name);
                        if(it==localPlaylists_.end()||track.id.empty()){
                            update("Choose a playlist","Library","Create or open a playlist, then tap P+");break;
                        }
                        auto& songs=it->second;
                        if(songs.size()>=300){update("Playlist full",name,"Maximum 300 songs");break;}
                        if(std::none_of(songs.begin(),songs.end(),
                            [&](const Track& song){return song.id==track.id;})){
                            songs.push_back(track);
                            if(!saveLocalPlaylists()){
                                songs.pop_back();update("Playlist save failed",name,"Check free storage");break;
                            }
                        }
                        update("Added to playlist",name,track.title);break;
                    }
                    case Action::PlaylistRemove:{
                        std::string name;
                        {std::lock_guard lock(mu_);name=activePlaylist_;}
                        auto it=localPlaylists_.find(name);
                        if(it==localPlaylists_.end())break;
                        auto before=it->second;
                        std::erase_if(it->second,[&](const Track& song){return song.id==job.query;});
                        if(!saveLocalPlaylists()){
                            it->second=std::move(before);
                            update("Remove failed",name,"Check free storage");break;
                        }
                        {std::lock_guard lock(mu_);results_=it->second;
                            page_=std::min(page_,std::max(0,int((results_.size()+4)/5)-1));selected_=-1;}
                        break;
                    }
                    case Action::PlaylistPlay:{
                        int index=-1;try{index=std::stoi(job.query);}catch(...){}
                        std::string name;{std::lock_guard lock(mu_);name=activePlaylist_;}
                        std::vector<Track> songs;
                        if(name=="Offline")songs=offlineTracks(self_.getConfigDir()/"music");
                        else if(auto it=localPlaylists_.find(name);it!=localPlaylists_.end())songs=it->second;
                        loadTrackCovers(env,songs);
                        if(index<0||index>=int(songs.size()))break;
                        {std::lock_guard lock(mu_);results_=std::move(songs);selected_=index;
                            standaloneQueueSelection_=false;standalonePlaylistQueue_=true;
                            playlistOrder_=results_;shuffle_=false;
                            queueTracks_.clear();
                            const int n=static_cast<int>(results_.size());
                            for(int i=0;i<n&&i<40;++i)
                                queueTracks_.push_back(results_[static_cast<size_t>((selected_+i)%n)]);
                        }
                        playSelected();break;
                    }
                    case Action::QueueAdd:{
                        std::lock_guard lock(mu_);
                        auto it=std::find_if(searchCache_.begin(),searchCache_.end(),
                            [&](const Track& item){return item.id==job.query;});
                        if(it!=searchCache_.end()&&queueTracks_.size()<40&&
                           std::none_of(queueTracks_.begin(),queueTracks_.end(),
                           [&](const Track& item){return item.id==job.query;}))
                            queueTracks_.push_back(*it);
                        break;
                    }
                    case Action::QueueRemove:{
                        std::lock_guard lock(mu_);
                        if(queueTracks_.size()>1)
                            queueTracks_.erase(std::remove_if(queueTracks_.begin()+1,queueTracks_.end(),
                                [&](const Track& item){return item.id==job.query;}),queueTracks_.end());
                        break;
                    }
                    case Action::QueuePlay:{
                        bool found=false;
                        {std::lock_guard lock(mu_);
                         auto it=std::find_if(queueTracks_.begin(),queueTracks_.end(),
                            [&](const Track& item){return item.id==job.query;});
                         if(it!=queueTracks_.end()){
                             Track next=*it;
                             queueTracks_.erase(queueTracks_.begin(),it);
                             results_.push_back(next);selected_=static_cast<int>(results_.size())-1;
                             standaloneQueueSelection_=true;standalonePlaylistQueue_=false;
                             page_=selected_/5;found=true;
                         }}
                        if(found)playSelected();
                        break;
                    }
#endif
                    case Action::Pair:
#if !defined(LEVI_STANDALONE)
                    case Action::PlaylistList:case Action::PlaylistOpen:
                    case Action::PlaylistPlay:case Action::PlaylistCreate:case Action::PlaylistRename:
                    case Action::PlaylistDelete:case Action::PlaylistAdd:
                    case Action::PlaylistRemove:
#endif
                    case Action::QueueRefresh:
#if !defined(LEVI_STANDALONE)
                    case Action::QueuePlay:case Action::QueueRemove:case Action::QueueAdd:
#endif
                        break;
                }
            }
            bool playing=player.isPlaying(env);
            int pos=player.position(env),dur=player.duration(env);
#if defined(LEVI_STANDALONE)
            if(!playing&&standaloneWasPlaying&&dur>0&&pos>=dur-1500){
                bool hasNext,repeatOne;
                {std::lock_guard lock(mu_);
                    hasNext=standalonePlaylistQueue_||queueTracks_.size()>1;
                    repeatOne=repeatMode_==2;}
                if(repeatOne)playSelected();
                else if(hasNext)addJob(Action::Next);
            }
            standaloneWasPlaying=playing;
#endif
            {
                std::lock_guard lock(mu_);
                playing_=playing;elapsed_=pos;
                playerPositionStamp_=std::chrono::steady_clock::now();
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
            } // Native Standalone or legacy Audius mode.
        }
        if(useBridge_){
            std::string key;{std::lock_guard lock(mu_);key=bridgeKey_;}
            if(!key.empty())bridgeGet("/v1/game-exit",key);
        }
        player.release(env);
    }
};
} // namespace
PL_REGISTER_MOD(LeviMusic, LeviMusic::instance())


