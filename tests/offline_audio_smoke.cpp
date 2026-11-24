#include "../src/OfflineAudio.hpp"
#include "../src/LyricText.hpp"
#include <cassert>

int main(){
    using lm_offline::extension;
    assert(extension("....ftypM4A ","application/octet-stream")==".m4a");
    assert(extension("ID3some audio","application/octet-stream")==".mp3");
    assert(extension("OggSsome audio","application/octet-stream")==".ogg");
    assert(extension("<html>error</html>","text/html").empty());
    assert(lm_lyric::normalize("<00:01.00>You<00:01.50>Miss<00:02.00>Everything")
           =="You Miss Everything");
    assert(lm_lyric::normalize("YouMissEverything")=="You Miss Everything");
    assert(lm_lyric::normalize("夜に駆ける")=="夜に駆ける");
}
