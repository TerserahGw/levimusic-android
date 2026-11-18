# LeviMusic 1.2.0 RC — source changes

Author: ReaAurelis. Target: Minecraft Bedrock ARM64 under LeviLauncher Android v1.7.1.

## 1. APK update / signing reliability
- Build pipeline now supports a **permanent Android release signing key** from protected GitHub Actions Secrets, with an increased Android versionCode (120).
- Never rotate signing keys on ordinary releases. CI refuses to publish a new GitHub Release when protected signing secrets are absent; it may still output developer-only debug APK artifacts.
- One-time setup is provided as scripts/setup_apk_signing_termux.sh and requires authenticated GitHub CLI on Termux.
- Previously installed debug-signed versions may require ONE final reinstall because Android does not treat mismatched certificates as compatible. Backup old playlists first.
- Companion v1.2.0 adds **Export backup** and **Import backup** controls to My Playlists. Older running Bridge can be backed up using scripts/backup_playlists_old_termux.sh.

## 2. Standalone built-in browser — experimental (default OFF)
- A new **WEB** button is available in the Standalone menu. Toggle Experimental embedded WebView first.
- Native code loads its own browser.dex packaged in the .levipack via Android DexClassLoader, without an additional APK.
- The original Java component attaches a visible Android WebView to Minecraft Activity, with URL/search field, back, forward, refresh and close.
- The browser starts at https://m.youtube.com. Video remains visible; no audio-only extraction, DRM avoidance or background bypass. Website playback requires user action and is provider-controlled.
- The WebView is **not** rendered onto Minecraft's 3D world geometry. It is an Android UI panel inside the Minecraft Activity.
- Native Standalone HUD's play/search/lyrics queue still operates through Audius, **not** the WebView website. No fake claim of YouTube/Spotify native playback integration.
- ActivityThread reflection is nonpublic Android API and may be blocked by Android 16; opt-in testing is required.

## 3. 3D floating lyrics — research stage
- Analysis of supplied WebScreen 1.0.23 binary found WebView/SurfaceTexture/GLES concepts.
- Analysis of supplied LHolo 26.60.1 binary found client-only world rendering hooks including LevelRendererPlayer::renderBlockEntities and Tessellator.
- A client-only, camera-facing per-line 3D lyric renderer is technically plausible and has a detailed implementation plan in docs/WORLD_LYRICS_3D.md.
- **Real 3D world-space lyrics are NOT implemented in this RC**. Binary mods have no documented public renderer API, and unverified Minecraft render offsets could crash the game.
- Existing 2D lyric scatter stays optional and is not labeled 3D.

## Quality/status
- This is a release candidate; compilation verifies packages, not runtime behavior or compatibility on every Android device.
- Only one LeviMusic .levipack variant should be enabled at a time. The Bridge APK is for the full catalog/player and playlist functions.
- Music provider and YouTube terms remain in force; no claim of unlimited free commercial streaming.