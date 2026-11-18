# LeviMusic Online 0.3.0 Beta

**Author:** ReaAurelis  
**Platform:** Minecraft Bedrock Android, LeviLauncher v1.7.1 (arm64-v8a)  
**Status:** Developer preview — Android runtime and bridge interaction still require hands-on tests.

## What's new

LeviMusic now has an **official-app media bridge**. Rather than pretending that Audius contains the Spotify or YouTube Music catalog, a separate Android app observes your existing **Spotify** or **YouTube Music** MediaSession using **Notification Access**. The in-game native HUD can show the real title/artist/position/duration and issue normal media controls. Audio comes from your official music application. The native mod and bridge communicate over a token-protected `127.0.0.1:38771` loopback socket. There is no third-party music scraping, DRM bypass, file downloading or custom commercial song streaming.

This approach uses your **own account and legitimate provider playback capabilities**; it **does not remove subscription limits, ads, geographic restrictions, skip limits or content restrictions**. YouTube Music/Spotify may stop audio in the background according to your account/plan and Android battery settings.

## Download two files from Releases

After the GitHub Actions workflow succeeds, download **both**:

- `LeviMusic-0.3.0-arm64-v8a.levipack` — the in-game mod
- `LeviMusic-Bridge-0.3.0.apk` — the native Android companion (beta APK, debug-signed)

The companion is an ordinary sideloadable APK, not part of the Minecraft mod loader. The files belong to the same version and must be installed separately.

## Setup on Android 16

1. Disable/remove the old LeviMusic mod from LeviLauncher.
2. Install the Bridge APK, open it and choose **Open Notification Access**. Allow access for LeviMusic Bridge. Android may require you to enable **Allow restricted settings** from its system App Info first when sideloading; review this sensitive permission carefully.
3. In Bridge, tap **Copy pairing key**. Keep the key private.
4. Install `LeviMusic-0.3.0-arm64-v8a.levipack` through LeviLauncher Mods.
5. Launch Minecraft, open LeviMusic Mod Menu settings. Verify **Spotify / YouTube Music Bridge = ON**, and paste the key into **Bridge Key**.
6. Start an actual track in your official Spotify or YouTube Music Android app, then return to Minecraft. Use the bottom-left LeviMusic mini-player to control the active MediaSession.
7. If the game won't start, disable the mod and provide the LeviLauncher and Minecraft version plus Android logcat.

**Important:** Search for and select tracks in the **official music app**. MediaSession is a playback control interface, **not a music-catalog search API**. Search and song-list browsing inside Minecraft are **not implemented** for Spotify/YouTube Music; the right panel shows synced lyrics and guidance instead of fake search results. In legacy Audius mode you can toggle **Spotify / YouTube Music Bridge = OFF**, but that mode has only the public Audius catalog.

## Improved synchronized lyrics

LeviMusic requests timestamped lyrics from LRCLIB for a track reported by the MediaSession. If multiple candidates exist, it tries to prefer a match with duration close to the current song; availability and accuracy are not guaranteed. A karaoke-style **lyric bubble** appears briefly above/below the compact HUD as the timed line changes, fading without permanently blocking gameplay. Settings include **Enable lyrics** and **Show lyric bubble below HUD**; automatic position fallback avoids the edge of the display. The full player also shows neighboring timed lyric lines in the right panel.

## Visual design

The interface follows the supplied Apple UI Designer guidelines as design inspiration only: quiet black/charcoal surfaces, soft grayscale hierarchy, restrained accents, slim progress bar, breathing room, and brief motion. No Apple or proprietary font/assets are bundled. Lucide icons remain under their existing third-party license. The HUD is rendered with LeviLauncher primitives and does **not** have true backdrop blur or guaranteed z-order over other mods.

## Architecture

- **LeviMusic mod:** C++20, Preloader SDK from LeviLauncher Android v1.7.1, JNI/worker thread, HUD drawing and input
- **LeviMusic Bridge:** Java Android `NotificationListenerService`, `MediaSessionManager`, `MediaController`
- **IPC:** loopback-only TCP HTTP-style protocol protected with a 48-hex-character random pairing key
- **Provider:** official installed Spotify or YouTube Music Android app, selected from the currently active media session
- **Lyrics:** LRCLIB matching and synchronized timestamps
- **Fallback:** legacy Audius mode, if manually selected

## Limitations

- **No truly free unlimited access to licensed music** is provided. Provider terms and subscription rules apply.
- No direct in-Minecraft Spotify/YouTube Music catalogue search, album list or playlist browser via MediaSession.
- Some providers may reject seek/skip or pause audio after losing foreground priority.
- Notification Access is a powerful permission that reveals notifications; grant it only if you trust the app. This beta bridge does not upload notification text or telemetry. It uses only active media session metadata.
- Android may suspend the companion or restrict background media playback. Battery optimizations and other mods may interfere.
- Changes to soundtrack metadata or missing lyrics may cause delays due to LRCLIB network lookups.
- The full UI and mini-player are native overlay primitives and may be covered by separately layered mod buttons.
- The APK is debug-signed for testing, not production distribution.
- The GitHub Actions workflow builds both packages; **compile success does not prove runtime success**.

## Links

- [GitHub Releases](https://github.com/TerserahGw/levimusic-android/releases)
- [Android MediaSessionManager](https://developer.android.com/reference/android/media/session/MediaSessionManager)
- [Spotify Developer Documentation](https://developer.spotify.com/documentation/)
- [YouTube API Services policies](https://developers.google.com/youtube/terms/api-services-terms-of-service)
- [LRCLIB](https://lrclib.net/)
- [LeviLauncher Android](https://github.com/LiteLDev/LeviLauncher-Android)

## Update from Termux

```sh
cd ~/LeviMusic-Android-Source
git pull --ff-only origin main
```
