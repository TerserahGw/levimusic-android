# LeviMusic v1.0.0 Release Candidate

**Author:** ReaAurelis  
**Platform:** LeviLauncher Android v1.7.1, ARM64, Minecraft Bedrock  
**Status:** RC, not verified stable on device.

## New standalone Android player

- Search video/music titles and artists directly from the LeviMusic Android application.
- Stream audio inside the app using AndroidX Media3 / ExoPlayer.
- Native dark Android UI, results list, current title/artist, play/pause, skip, and slim seekbar.
- New vector app launcher icon, no longer the default Android icon.
- Persistent foreground media playback service with MediaSession.
- Optional Notification Access service to connect Minecraft via authenticated local loopback.

Music extraction is from **NewPipeExtractor** and unofficial YouTube searches, not an official YouTube Music or Spotify integration. It may stop working due to changes or restrictions. The mod does not impose a plays-per-day quota, but service-level unrestricted use cannot be guaranteed. YouTube developer policies restrict isolating audio streams through the official API.

## Game controls

1. Install both assets from [Releases](https://github.com/TerserahGw/levimusic-android/releases):
   - LeviMusic-Bridge-1.0.0.apk (standalone music player)
   - LeviMusic-1.0.0-arm64-v8a.levipack (Minecraft native mod)
2. Open the app and **test standalone search/play** first.
3. Tap the settings icon, grant Notification Access for LeviMusic Bridge (sensitive permission, optional for standalone playback), and copy its private pairing key.
4. Import the levipack into LeviLauncher; paste the key under LeviMusic's Bridge Key setting.
5. In Minecraft, tap the mini-player: search directly from the right panel, tap a song to start it in the companion app, and use transport/seek controls.

**Note:** Game search results are unofficial YouTube-based media, not the Spotify catalogue. A separate existing Spotify or YouTube Music app session can still be controlled through Android MediaSession under the provider's subscription/background restrictions. The companion's player takes priority when active.

## Karaoke HUD

- Transparent lyric popup with white text and black multiple-draw-pass outline.
- Line persists from its timestamp until the next LRC timestamp rather than disappearing after five seconds.
- Gentle fades near timestamp boundaries.
- Lookup from LRCLIB with duration-based matching.
- Popup above or below HUD configurable in LeviLauncher Mod Menu.

## Honest limitations

The title 1.0.0 is a development milestone. The GitHub release is a **Release Candidate**, because compiler success is not proof of actual Android 16 playback or in-game touch behavior. Device testing, background audio tests, overlay priority and lyrics synchronization are still required before a truly stable release.

No DRM bypass or guaranteed unlimited commercial music playback. Media service providers may reject streams. Android may restrict background services. Unofficial NewPipeExtractor functionality may be affected by YouTube changes.

## Third-party acknowledgements & license

This app uses GPL-3.0-licensed [NewPipeExtractor](https://github.com/TeamNewPipe/NewPipeExtractor), AndroidX Media3/ExoPlayer and OkHttp. NewPipe-based architecture is informed by the [Kuro open-source project](https://github.com/ayashiiiyo/kuro). This project is licensed under [GPL-3.0](LICENSE). Lucide artwork is licensed ISC; see [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
