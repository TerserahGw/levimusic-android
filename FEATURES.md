# LeviMusic 1.0.1 — Release Candidate

**Author: ReaAurelis**

- Independent companion APK with AndroidX Media3/ExoPlayer, native lightweight music library and track search using unofficial NewPipeExtractor.
- Independent authenticated localhost Bridge API; **Notification Access is no longer required for the built-in player, search and Minecraft control**.
- API routes: health, status, search, play by track ID, playlist/queue snapshot, media controls and seek.
- Longer timeouts for slow online search requests made inside Minecraft.
- Play/Pause and improved Next/Previous behavior based on the companion's track queue.
- Cover art displayed for searches and current song in the companion APK.
- Cover artwork for the current song in the compact Minecraft HUD and full now-playing panel when the artwork URL is accessible.
- LRCLIB synchronized lyrics lookup with client header and improved song/artist normalization; transparent white outline lyric HUD remains for its full timestamp window.
- No added playback limit; actual providers may restrict access.
- Native arm64-v8a `.levipack` for LeviLauncher Android and separate Android `.apk`.

**Known limitations:** Does not offer Spotify Premium or YouTube Music Premium for free, full catalogue guarantees, or guaranteed lyric coverage. API and runtime changes are a **Release Candidate**, not verified Stable without a real Android/Minecraft test.
