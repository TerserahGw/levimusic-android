# LeviMusic v1.1.0 Release Candidate

**Author:** ReaAurelis  
**Platforms:** Minecraft Bedrock Android using LeviLauncher v1.7.1 (ARM64) and optional LeviMusic Bridge APK.  
**Status:** v1.1.0 RC, not independently confirmed stable on a device.

## Install: choose one variant

Download from **[GitHub Releases](https://github.com/TerserahGw/levimusic-android/releases)** after the workflow completes.

**Full setup** (recommended):
1. Install `LeviMusic-Bridge-1.1.0.apk` and launch it.
2. Install `LeviMusic-1.1.0-arm64-v8a.levipack` through LeviLauncher.
3. In the companion settings, tap **Pair Minecraft (copy)**.
4. Open Minecraft, tap LeviMusic mini-player, then tap **PAIR** in the player header. The mod verifies the key and saves it for future launches.
5. Search/choose songs from the mod or Android app. The Bridge provides playlists, art, song status and media controls. Imported synchronized LRC lyrics are supported in the companion app.

**No companion** (limited):
- Install **`LeviMusic-Standalone-1.1.0-arm64-v8a.levipack`** without the APK.
- It defaults to public **Audius native streaming**, not a Chromium/WebView browser and **not** the official Spotify/YouTube Music catalog.
- **Do not enable both LeviMusic variants simultaneously.**

## What's new in v1.1.0
- One centered Play/Pause button; the redundant left Stop control was removed.
- Smooth 240ms menu fade/slide (disable animation from Mod Menu to reduce overhead), and slowly scrolling long UTF-8 song titles.
- Real playlists saved in the companion; search/add/remove/create/delete/play in Android, plus in-game create/filter/browse/play and an Add action using the last opened playlist.
- A playlist keeps its own next/previous queue even when the catalog is searched again.
- One-tap pairing via explicit clipboard copy + a **PAIR** button; verified key persisted in the native mod's private configuration folder. No unsecured automatically exposed secret.
- Timestamped lyrics: local .lrc import in the companion (press **LRC** while a song plays) and improved LRCLIB exact/structured/broad search.
- **Experimental** floating screen lyrics, OFF by default. It moves subtly around the HUD/game view, but it is not a real 3D world billboard and does not detect empty Minecraft blocks.

## Playlists from the Minecraft mod
Tap **PLAYLISTS** in the player header. The search field finds existing playlists and the **+** button creates one with that name. Tap a playlist to list its tracks and tap a song to play the playlist. Tap **SEARCH** to return to the song catalog. To add a searched song, first open your destination playlist once, return to SEARCH, select a search song and tap **+** beside the search results header. For bulk edits and deletion, use the Android app's playlist screen.

## Pairing
The companion uses the same app package as v1.0.x. You may update the APK in place if it was signed with a compatible key; otherwise keep the original until you have saved your playlists and key. The Mod Menu's manual **Bridge Key** field remains available as a fallback. Android may prevent clipboard access if the game is not foreground.

The local API binds to loopback `127.0.0.1:38771`; it requires the private per-installation token on all control/search/playlist endpoints. This is **not** a public HTTP API.

## Why no truly embedded YouTube browser in the mod?
The native `.levipack` is a C++ library loaded into LeviLauncher. Its exposed preloader interface provides native HUD and input drawing, not an Android WebView-hosting API. An integrated YouTube iframe/browser would also need a visible video player and follow YouTube's autoplay/background playback and audio separation rules. We do **not** disguise an Audius player as a browser or promise hidden YouTube audio playback. The standalone mod is an honest, usable Bridge-free alternative for the **Audius** catalog only.

Official API background: [YouTube IFrame Player](https://developers.google.com/youtube/iframe_api_reference), [YouTube API policies](https://developers.google.com/youtube/terms/developer-policies-guide).

## Lyric source strategy
1. Check locally imported synced LRC for the exact song/artist.
2. Query LRCLIB exact signature where possible (title, artist, duration).
3. Try more permissive LRCLIB search using cleaned title, artist inference and duration filtering.
4. If no reliable synced lyrics exist, show that they are unavailable instead of inventing timing. Plain lyrics do not become fake karaoke.

## Status, copyright & compatibility
- **RC, not Stable:** C++ and APK may compile but real-world touch, overlay layering, background playback, playlist skip and battery behavior require device verification.
- The embedded app player uses Media3 and the unofficial GPL-3.0 NewPipeExtractor engine; it does not provide a licensed, unmetered Spotify/YouTube Music subscription.
- Spotify/YouTube Music official sessions remain controlled only through Android MediaSession and may require their own plan.
- The HUD floating-text experiment operates in **2D screen coordinates**; a real 3D player-relative lyric billboard would need engine-version-specific camera projection hooks and separate testing.
- Source code: [C++](src/LeviMusic.cpp), [Android app](companion/app/src/main/java/com/reaaurelis/levimusic/bridge), [features](FEATURES.md).
- Licenses: [LICENSE](LICENSE), [Third-party licenses](THIRD_PARTY_LICENSES.md).

## Updating from Termux
```sh
cd ~/LeviMusic-Android-Source
git pull --ff-only origin main
```
