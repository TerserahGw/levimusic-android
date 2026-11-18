# LeviMusic v1.2.3 — Release Candidate

Author: **ReaAurelis** | Android ARM64 Minecraft Bedrock via LeviLauncher.  
**Status:** CI-compiled, game-runtime behavior NOT confirmed.

## What's new

### SEARCH / PLAYLIST / LYRICS tabs
The full mod's right side is a single tabbed pane instead of separate permanent Search and Lyrics columns. The left side remains the current Music Player. Search provides music results and **+** action for adding songs to Up Next. Playlist has **UP NEXT** for the temporary recommendation radio and **SAVED** for existing user playlists. Lyrics shows the current synchronized LRC line and the lines around it. Pagination and buttons use dedicated slots so touch areas do not overlap.

### Autoplay starts from your selected result, not search ranking
If you search 'Another Love' and tap result #2, result #2 is the start of a **new temporary radio**, not a command to play search result #3 next. LeviMusic uses YouTube's related-video suggestions through NewPipeExtractor (with similarity-search fallback) to **try to keep three future songs ready**. The Up Next view shows the current song and the next three, and allows removing suggestions, selecting one to play, or adding a song from Search using **+**. The queue is refreshed as songs change.

Playlist tracks saved by the user continue in their explicit playlist order. A related recommendation is not necessarily exactly what Spotify or YouTube's proprietary recommendations would choose; the extraction API is unofficial and can stop working.

### Experimental true native 3D Lyrics (NO addon / NO per-world setup)
The full and standalone native .levipacks now contain `src/WorldLyrics3D.hpp`: an optional 3D Tessellator world renderer inspired by the GPL-3.0 BedrockTools renderer technique. It draws a camera-facing white vector-text billboard with black outline in **actual Minecraft world coordinates**. Each new LRC line receives a different anchor near the camera. It is entirely client-side, without scripts, world packs, entities or network packets, so it is designed to work in local worlds and on public/friends' servers without other players seeing it.

**Important: this is UNSAFE EXPERIMENTAL native rendering, OFF by default.** It has **not been tested in Minecraft**. Even if function signatures resolve, game version offsets can be incompatible and lead to a native crash/black screen. Use a local test world with backups before enabling. Never assume the feature works on every Minecraft build. If unsupported, 2D lyric popup remains available. The initial font is Latin/ASCII only (non-Latin lyrics show fallback glyphs), and the renderer does not yet test block occlusion to guarantee empty positions.

Enable: Mod Menu -> LeviMusic -> **Experimental true 3D world lyrics (native, risky)**. Leave the older `Legacy 2D lyric scatter` disabled. When disabled, the standard readable 2D lyric popup still works.

### Installation
- **Full version:** install both LeviMusic Bridge APK and the full `LeviMusic-1.2.3-arm64-v8a.levipack` file. Existing Bridge pairing and playlist storage should persist on a regular in-place signed APK update.
- **Standalone:** install `LeviMusic-Standalone-1.2.3-arm64-v8a.levipack` only. Requires a separately running **ChruPipe** self-hosted server (Ktor backend); configure server URL in LeviMusic. The separate built-in WebView remains an optional, visible browser experiment, not the native music backend. The editable 3-song radio queue requires the full Bridge variant.
- **Never load both native .levipack variants simultaneously.**

### Troubleshooting
- Up Next empty after starting radio: verify the Bridge APK is connected and the unofficial YouTube related-video extractor still functions. The queue is populated asynchronously and may contain fewer than three suggestions.
- 3D lyrics not visible: verify timed LRC exists, the experiment is enabled, and Minecraft's ARM64 renderer signatures match. Inspect logcat tag `LeviMusicWorld3D`. It may not support your Minecraft build.
- Crash/black screen after enabling 3D: disable the experimental option in LeviLauncher settings; 3D hooks require game-version-specific compatibility work. Avoid production worlds during testing.
- 3D UTF-8 characters replaced by '?': the initial native vector font only includes Latin/ASCII. The normal 2D HUD continues using system text rendering.
- In-game audio sources still follow platform terms. The app's NewPipeExtractor is unofficial; standalone ChruPipe needs a server running.

## Sources and acknowledgments
- [BedrockTools world-render sample](https://github.com/QYCottage/BedrockTools/blob/main/src/modules/visual/breadcrumbs.cpp) (GPLv3; referenced for render hooks, signatures, and offsets).
- [GitHub Releases](https://github.com/TerserahGw/levimusic-android/releases).
- [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).