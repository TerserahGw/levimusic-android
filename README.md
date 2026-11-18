# LeviMusic v1.0.1 — Release Candidate

Author: **ReaAurelis**  
Platform: Android ARM64 with LeviLauncher Android v1.7.1 / Minecraft Bedrock  
Status: **Release Candidate**, because end-to-end testing on an Android phone and the Minecraft runtime has not been completed.

## What v1.0.1 changes

- **Bridge API independent of Notification Access.** The localhost service starts when the LeviMusic Android app opens or the playback service starts. Grant Notification Access only to control third-party Spotify/YouTube Music sessions; it is not required for the internal music player and Minecraft search.
- **Fix in-game search**: native requests may now wait up to 30 seconds for the online provider, instead of timing out after about 450 milliseconds. Error messages for missing pairing key, offline bridge, empty results and rejected playback are surfaced in the UI.
- **Next / Previous**: controls route through the companion app; improves queue fallback if a search result set has changed. At least two tracks must be in the queue for skip controls to advance.
- **Cover art**: Android companion uses image thumbnails in the search list and the current player's album tile. Native Minecraft HUD shows a small song cover in both mini-player and full Now Playing card. Fallback icon remains if artwork is missing or cannot load.
- **More reliable lyrics**: better LRCLIB request identification, song-title cleanup and artist inference from `Artist - Song` title formatting. Synced lyrics are still shown only when the provider has a good timestamped match.
- **API diagnostic routes** `/v1/health`, `/v1/queue`, `/v1/status`, `/v1/search?query=...`, `/v1/play?id=...`, `/v1/control?action=...`, all protected by the per-installation Bridge Key and bound only to `127.0.0.1:38771`.
- **No internet-facing API** and no public backend hosting. The app is the local API server for the Minecraft mod.

## Install and test (Android 16)

1. Remove or disable the older LeviMusic mod from LeviLauncher.
2. Install **LeviMusic-Bridge-1.0.1.apk** over the older companion. If Android refuses due to a changed app signature, do not uninstall until you copy your pairing key; uninstall/reinstall will reset the key.
3. Open the companion app. It initializes the bridge server and keeps an idle foreground playback service ready.
4. Search an artist/track **inside the Android app**, start playback and verify the current track title, cover and Next/Previous.
5. Install **LeviMusic-1.0.1-arm64-v8a.levipack** through LeviLauncher.
6. In Mod Menu → LeviMusic, ensure **Bridge mode enabled**, paste the private Bridge Key from the app's settings, start Minecraft.
7. In the in-game LeviMusic panel, tap Search. Enter a song title and press SEARCH; results may take several seconds on the first request. Tap a result to play it.
8. Test Play/Pause, Next/Previous, the seek bar, cover, lyrics and switching between the APK and Minecraft.

**Note**: Notification Access is optional for the built-in player and mod's search. It is required to control media sessions owned by other installed applications on Android.

## Lyric availability

LeviMusic queries LRCLIB for timestamped lyrics. Some songs, covers, remixes, game OSTs, livestreams and recordings with unusual YouTube titles have no reliable match. These will not display synchronized lyrics even if playback works. The mod does not generate fake lyrics. The no-background white lyric with black outline remains visible per LRC timestamp interval when a matching lyric exists.

## Important limitations and release status

- No guaranteed unlimited access to paid/commercial music catalogs. NewPipeExtractor-based YouTube searching is unofficial and can break due to service changes; Spotify playback is controlled through your official account and app where available.
- The image pipeline depends on reachability of cover image URLs and may fall back to the default icon. Runtime image registration has a safety cap to limit retained memory.
- The bridge is loopback-only and uses a pairing key, but local apps with access to the key can request playback controls.
- Cover art for external Spotify/YouTube Music sessions depends on the provider returning accessible artwork. The standalone LeviMusic catalog is the primary supported cover source.
- If Android suspends the companion service or your music app, the mod may temporarily show an unavailable state.
- GitHub Actions verifies both packages compile; it cannot prove stable in-game behavior, animation quality, song availability or correctness of lyric synchronization.
- The `v1.0.1` release is therefore intentionally marked **Release Candidate** until on-device testing confirms that all critical failures are fixed.

## Sources and licenses

- LeviLauncher Android / Preloader SDK: https://github.com/LiteLDev/LeviLauncher-Android
- NewPipeExtractor: https://github.com/TeamNewPipe/NewPipeExtractor (GPL-3.0)
- LRCLIB: https://lrclib.net
- Android Media3: https://developer.android.com/media/media3
- Lucide Icons (ISC); see `THIRD_PARTY_LICENSES.md`.
- The companion's source is in `companion/`, native Minecraft mod is in `src/`.

## Termux

```bash
cd ~/LeviMusic-Android-Source
git pull --ff-only origin main
```
