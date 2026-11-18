# LeviMusic v1.1.2 Release Candidate

Author: **ReaAurelis** | ARM64 Minecraft Bedrock via LeviLauncher Android v1.7.1.

This release is a **Release Candidate**: successful automated compilation is not sufficient to confirm stable Android/Minecraft gameplay.

## Install

Full version: download both the **LeviMusic-Bridge-1.1.2.apk** Android music app and **LeviMusic-1.1.2-arm64-v8a.levipack** from [Releases](https://github.com/TerserahGw/levimusic-android/releases). Install the APK, start it, import the mod into LeviLauncher, copy the Pair Minecraft key in the app and tap PAIR in the in-game music player. The verified key is stored locally for later reconnects.

Standalone: install only **LeviMusic-Standalone-1.1.2-arm64-v8a.levipack**. It uses the public Audius catalog, not a built-in browser or licensed Spotify/YouTube Music streaming. Never activate two LeviMusic variants at once.

## 1.1.2 changes

### Three clear areas
The main menu now separates **Now Playing** (cover, title/artist, duration/seek, playback controls) on the left, **Synced Lyrics** (highlighted current LRC line and the neighboring lines) in the center, and **Search/Playlists/Music List** on the right. Lyrics remain visible while searching or opening playlists. Controls and playlist Add button have dedicated layout slots to reduce overlapping elements in landscape mode.

### Automatic next and recommendations
The Android companion now responds to Media3 PLAYER_STATE_ENDED. In playlist mode it advances to the next saved song, looping from the final song back to the first. Outside a playlist it follows the current search queue, then tries YouTube related items from NewPipeExtractor. If necessary, it falls back to a similarity search using the current title and artist. It excludes the current and recently played tracks, and can stop with a clear status if no recommendation is available. This is a best-effort unofficial engine, not a Spotify/YouTube subscription bypass or a guarantee of unlimited playback.

### Important distinction: world-space vs 2D lyrics
The user's requested floating lyrics are *real 3D text in the Minecraft world*, locally visible only, with a **new world location per line**. The pinned Preloader HUD API is 2D; it does not expose a documented stable world-render text hook or camera projection. **True 3D lyrics are not implemented in v1.1.2.** The existing optional 2D scatter is renamed to *Legacy 2D lyric scatter*, OFF by default. Its text jumps to a different screen location on each timestamped lyric, not a world coordinate. Real client-only 3D rendering needs tested Minecraft-version-specific native hooks.

## Notes and limitations
- Lyrics need matching timestamped LRC from LRCLIB or local import.
- NewPipeExtractor uses unofficial YouTube endpoints and can fail after service changes.
- Animation, search, playlist UI, audio focus, and backdrop overlay interactions require on-device tests.
- Playlist data is in the companion app's private data: avoid uninstalling without backing it up.
- Source GPL-3.0 with dependency credits in THIRD_PARTY_LICENSES.md.
