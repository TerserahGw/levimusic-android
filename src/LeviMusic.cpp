// LeviMusic for LeviLauncher Android, source-only beta 0.1.0
// Music discovery / playback: Audius public API (not Spotify/YouTube scraping).
// Lyrics: optional LRCLIB synchronized lyrics, only when available.
#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>
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

enum class Action { Search, PlayPause, Next, Previous, Stop, SetVolume };
struct Job { Action action; std::string query; };

bool clearException(JNIEnv* env, const char* step) {
    if (!env->ExceptionCheck()) return false;
    env->ExceptionClear();
    __android_log_print(ANDROID_LOG_WARN, TAG, "Java exception at %s", step);
    return true;
}

struct EnvScope {
    JavaVM* vm = nullptr;
    JNIEnv* env = nullptr;
    bool attached = false;
    explicit EnvScope(JavaVM* v) : vm(v) {
        if (!vm) return;
        if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) return;
        if (vm->functions->AttachCurrentThread(vm, reinterpret_cast<void**>(&env), nullptr) == JNI_OK) attached = true;
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

std::vector<Track> searchAudius(JNIEnv* env, const std::string& query) {
    std::vector<Track> tracks;
    if (query.empty()) return tracks;
    std::string text = getHttp(env, std::string(API) + "/tracks/search?query=" + encode(env, query) +
                                    "&limit=20&app_name=" + APP_NAME);
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
            if (!tr.id.empty() && !tr.title.empty()) tracks.push_back(std::move(tr));
            env->DeleteLocalRef(obj);
        }
        env->DeleteLocalRef(arrClass);
        env->DeleteLocalRef(array);
    }
    env->DeleteLocalRef(root);
    return tracks;
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
        jstring jurl = toJava(env, url);
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
};

std::string shorten(const std::string& s, size_t max) {
    if (s.size() <= max) return s;
    return s.substr(0, max > 3 ? max-3 : max) + "...";
}

class LeviMusic {
    ll::mod::NativeMod& self_;
    JavaVM* vm_ = nullptr;
    std::thread worker_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Job> jobs_;
    std::vector<Track> results_;
    std::vector<Lyric> lyrics_;
    std::string query_ = "lofi";
    std::string title_ = "LeviMusic | Cari musik melalui Mod Menu";
    std::string message_ = "Ketik judul, lalu tekan Cari";
    std::string lyricLine_;
    int index_ = -1;
    int volume_ = 75;
    bool hud_ = true;
    bool lyricsOn_ = true;
    bool stopping_ = false;

public:
    LeviMusic() : self_(*ll::mod::NativeMod::current()) {}
    static LeviMusic& instance() { static LeviMusic app; return app; }
    bool load() {
        vm_ = self_.getJavaVM();
        __android_log_print(ANDROID_LOG_INFO, TAG, "LeviMusic loaded; VM=%p", static_cast<void*>(vm_));
        return vm_ != nullptr;
    }
    bool enable() {
        {
            std::lock_guard lock(mu_);
            stopping_ = false;
        }
        worker_ = std::thread([this]() { run(); });
        bool ok = pl::modmenu::ModuleBuilder(MODULE, "LeviMusic Online")
            .modId(self_.getId())
            .description("Audius online search & streaming. Edit Search Query, then tap Cari. Lyrics via LRCLIB when available.")
            .defaultEnabled(true)
            .config("searchQuery", "Search Query / Judul Lagu", pl::modmenu::ConfigType::Text, "lofi")
            .config("volume", "Volume Musik", pl::modmenu::ConfigType::SliderInt, "75", "0", "100")
            .config("hud", "Tampilkan HUD", pl::modmenu::ConfigType::Toggle, "true")
            .config("lyrics", "Lirik Sinkron", pl::modmenu::ConfigType::Toggle, "true")
            .onConfigChanged([](std::string_view, std::string_view key, std::string_view value) {
                instance().onConfig(key, value);
            })
            .registerModule();
        auto button = [this](const char* id, const char* name, const char* label, Action action) {
            return pl::modmenu::ButtonBuilder(id, name).moduleId(MODULE)
                .modId(self_.getId()).label(label)
                .stylePreset(pl::modmenu::ButtonStylePreset::Accent)
                .styleColors(0xD0182538U, 0xFF50C8E8U, 0x8049CDDCU)
                .textColor(0xFFFFFFFFU).sizeScale(1.5f)
                .behavior(pl::modmenu::ButtonBehavior::Click)
                .onEvent([action](std::string_view, pl::modmenu::ButtonEvent event, float) {
                    if (event == pl::modmenu::ButtonEvent::Click) instance().enqueue(action);
                }).registerButton();
        };
        if (ok) ok = button("levimusic.search", "Cari lagu", "Cari", Action::Search) && ok;
        if (ok) ok = button("levimusic.prev", "Lagu sebelumnya", "<<", Action::Previous) && ok;
        if (ok) ok = button("levimusic.play", "Play / Pause", "Play", Action::PlayPause) && ok;
        if (ok) ok = button("levimusic.next", "Lagu berikutnya", ">>", Action::Next) && ok;
        if (ok) ok = button("levimusic.stop", "Stop musik", "Stop", Action::Stop) && ok;
        if (!ok) { disable(); return false; }
        drawHud();
        __android_log_print(ANDROID_LOG_INFO, TAG, "LeviMusic enabled");
        return true;
    }
    bool disable() {
        for (auto id : {"levimusic.search", "levimusic.prev", "levimusic.play", "levimusic.next", "levimusic.stop"})
            pl::modmenu::unregisterButton(id);
        pl::modmenu::submitDrawCommands(MODULE, std::span<const pl::modmenu::DrawCommand>{});
        pl::modmenu::unregisterModule(MODULE);
        {
            std::lock_guard lock(mu_);
            stopping_ = true;
            jobs_.clear();
        }
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
        return true;
    }
    bool unload() { return true; }

private:
    void enqueue(Action action) {
        std::lock_guard lock(mu_);
        if (!stopping_) {
            jobs_.push_back({action, action == Action::Search ? query_ : std::string{}});
            cv_.notify_one();
        }
    }
    void onConfig(std::string_view key, std::string_view value) {
        std::lock_guard lock(mu_);
        if (key == "searchQuery") query_ = std::string(value).substr(0, 160);
        if (key == "volume") {
            try { volume_ = std::clamp(std::stoi(std::string(value)), 0, 100); } catch (...) {}
            jobs_.push_back({Action::SetVolume, {}});
            cv_.notify_one();
        }
        if (key == "hud") hud_ = (value == "1" || value == "true");
        if (key == "lyrics") lyricsOn_ = (value == "1" || value == "true");
    }
    void status(std::string t, std::string msg) {
        {
            std::lock_guard lock(mu_);
            title_ = std::move(t);
            message_ = std::move(msg);
        }
        drawHud();
    }
    void drawHud() {
        std::string top, desc, lyric;
        bool visible;
        {
            std::lock_guard lock(mu_);
            top = title_; desc = message_;
            lyric = lyricsOn_ ? lyricLine_ : std::string{};
            visible = hud_;
        }
        std::vector<pl::modmenu::DrawCommand> cmds;
        if (visible) {
            pl::modmenu::DrawCommand rect;
            rect.type = pl::modmenu::DrawCommandType::RectFilled;
            rect.x = 15; rect.y = 55; rect.w = 420; rect.h = lyric.empty() ? 64 : 91;
            rect.color = 0xC0162230U;
            cmds.push_back(rect);
            auto line = [&cmds](const std::string& msg, float y, float size, uint32_t color) {
                pl::modmenu::DrawCommand cmd;
                cmd.type = pl::modmenu::DrawCommandType::Text;
                cmd.x = 27; cmd.y = y; cmd.size = size;
                cmd.color = color; cmd.text = shorten(msg, 67);
                cmds.push_back(std::move(cmd));
            };
            line(top, 75, 17, 0xFFFFFFFFU);
            line(desc, 102, 13, 0xFFC5D8E7U);
            if (!lyric.empty()) line(lyric, 129, 14, 0xFF78E7DEU);
        }
        pl::modmenu::submitDrawCommands(MODULE, cmds);
    }
    void run() {
        EnvScope scope(vm_);
        if (!scope) { status("LeviMusic", "Tidak dapat mengakses JavaVM"); return; }
        JNIEnv* env = scope.env;
        AndroidPlayer player;
        auto playIndex = [this, &player, env]() {
            Track tr;
            int vol = 75;
            {
                std::lock_guard lock(mu_);
                if (index_ < 0 || index_ >= static_cast<int>(results_.size())) return;
                tr = results_[static_cast<size_t>(index_)];
                vol = volume_;
                lyrics_.clear(); lyricLine_.clear();
            }
            status(tr.title, "Memuat " + tr.artist + " dari Audius...");
            const std::string streamUrl = std::string(API) + "/tracks/" + encode(env, tr.id) + "/stream?app_name=" + APP_NAME;
            if (!player.startUrl(env, streamUrl, vol)) {
                status(tr.title, "Gagal streaming. Coba lagu lain atau periksa internet.");
                return;
            }
            status(tr.title + " - " + tr.artist, "Sedang diputar secara online");
            auto found = searchLyrics(env, tr);
            {
                std::lock_guard lock(mu_);
                lyrics_ = std::move(found);
                lyricLine_ = lyrics_.empty() ? "Lirik sinkron tidak tersedia" : "...";
            }
            drawHud();
        };
        int lastLyricMs = -1000;
        while (true) {
            Job job{};
            bool hasJob = false;
            {
                std::unique_lock lock(mu_);
                cv_.wait_for(lock, std::chrono::milliseconds(400), [this] { return stopping_ || !jobs_.empty(); });
                if (stopping_) break;
                if (!jobs_.empty()) { job = std::move(jobs_.front()); jobs_.pop_front(); hasJob = true; }
            }
            if (hasJob) {
                switch (job.action) {
                case Action::Search: {
                    if (job.query.empty()) { status("LeviMusic", "Isi Search Query di Mod Menu"); break; }
                    status("Cari: " + job.query, "Menghubungi katalog Audius...");
                    auto found = searchAudius(env, job.query);
                    {
                        std::lock_guard lock(mu_);
                        results_ = std::move(found);
                        index_ = results_.empty() ? -1 : 0;
                    }
                    // Immediately play first result: zero manual downloads, zero local import.
                    if (index_ >= 0) playIndex();
                    else status("Pencarian", "Tidak ada hasil. Coba kata kunci lain.");
                    break;
                }
                case Action::PlayPause:
                    if (player.isPlaying(env)) { player.toggle(env); status(title_, "Dijeda"); }
                    else if (player.position(env) > 0) { player.toggle(env); status(title_, "Diputar"); }
                    else playIndex();
                    break;
                case Action::Next:
                case Action::Previous: {
                    {
                        std::lock_guard lock(mu_);
                        if (!results_.empty()) {
                            const int count = static_cast<int>(results_.size());
                            index_ = (index_ + (job.action == Action::Next ? 1 : count - 1)) % count;
                        }
                    }
                    playIndex();
                    break;
                }
                case Action::Stop:
                    player.release(env);
                    {
                        std::lock_guard lock(mu_);
                        lyrics_.clear(); lyricLine_.clear();
                    }
                    status("LeviMusic", "Pemutaran dihentikan");
                    break;
                case Action::SetVolume: {
                    int vol;
                    { std::lock_guard lock(mu_); vol = volume_; }
                    player.setVolume(env, vol);
                    break;
                }
                }
            }
            if (player.isPlaying(env)) {
                const int pos = player.position(env);
                if (pos > lastLyricMs + 300) {
                    lastLyricMs = pos;
                    std::string line;
                    {
                        std::lock_guard lock(mu_);
                        if (!lyrics_.empty()) {
                            for (const auto& l : lyrics_) {
                                if (l.ms > pos) break;
                                line = l.line;
                            }
                            if (line != lyricLine_) lyricLine_ = line;
                            else line.clear();
                        }
                    }
                    if (!line.empty()) drawHud();
                }
            }
        }
        player.release(env);
    }
};

} // namespace
PL_REGISTER_MOD(LeviMusic, LeviMusic::instance())
