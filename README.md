# LeviMusic v1.2.4

Author: **ReaAurelis** | Android ARM64 Minecraft Bedrock via LeviLauncher.  
**Status:** CI-compiled, game-runtime behavior NOT confirmed.

## What's new

### SEARCH / PLAYLIST / LYRICS tabs
The full mod's right side is a single tabbed pane instead of separate permanent Search and Lyrics columns. The left side remains the current Music Player. Search provides music results and **+** action for adding songs to Up Next. Playlist has **UP NEXT** for the temporary recommendation radio and **SAVED** for existing user playlists. Lyrics shows the current synchronized LRC line and the lines around it. Pagination and buttons use dedicated slots so touch areas do not overlap.

### Autoplay starts from your selected result, not search ranking
If you search 'Another Love' and tap result #2, result #2 is the start of a **new temporary radio**, not a command to play search result #3 next. LeviMusic uses YouTube's related-video suggestions through NewPipeExtractor (with similarity-search fallback) to **try to keep three future songs ready**. The Up Next view shows the current song and the next three, and allows removing suggestions, selecting one to play, or adding a song from Search using **+**. The queue is refreshed as songs change.

Playlist tracks saved by the user continue in their explicit playlist order. A related recommendation is not necessarily exactly what Spotify or YouTube's proprietary recommendations would choose; the extraction API is unofficial and can stop working.

### Floating lyrics in 1.2.4
The native world renderer remains in source but is suspended because its version-specific Minecraft hooks can freeze the game. The setting now displays a client-only 2D floating lyric overlay when synchronized lyrics are available. True 3D world lyrics need a verified renderer integration and are not provided by this release.

### Installation
- **Full version:** install both LeviMusic Bridge APK and the full `LeviMusic-1.2.4-arm64-v8a.levipack` file. Existing Bridge pairing and playlist storage should persist on a regular in-place signed APK update.
- **Standalone:** install `LeviMusic-Standalone-1.2.4-arm64-v8a.levipack` only. Requires a separately running **ChruPipe** self-hosted server (Ktor backend); configure server URL in LeviMusic. The separate built-in WebView remains an optional, visible browser experiment, not the native music backend. The editable 3-song radio queue requires the full Bridge variant.
- **Never load both native .levipack variants simultaneously.**

### Troubleshooting
- Up Next empty after starting radio: verify the Bridge APK is connected and the unofficial YouTube related-video extractor still functions. The queue is populated asynchronously and may contain fewer than three suggestions.
- In-game audio sources still follow platform terms. The app's NewPipeExtractor is unofficial; standalone ChruPipe needs a server running.

## Sources and acknowledgments
- [BedrockTools world-render sample](https://github.com/QYCottage/BedrockTools/blob/main/src/modules/visual/breadcrumbs.cpp) (GPLv3; referenced for render hooks, signatures, and offsets).
- [GitHub Releases](https://github.com/TerserahGw/levimusic-android/releases).
- [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
## v1.2.4

The Android companion now has Home, Playlists and Settings tabs, a persistent mini player, an expanded player with Up Next and Lyrics, and a three-dot song menu for Play, Add to Up Next and Add to playlist. Romanization is on by default, using Android ICU for supported non-Latin scripts; it can be turned off in Settings or the in-game mod config. The world-space native hook is suspended after reports of a frozen player; the experimental toggle displays client-only 2D floating subtitles until a Minecraft version-specific native renderer is validated. The 3D feature is not available in this build.
