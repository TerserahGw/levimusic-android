# LeviMusic v1.2.1 Release Candidate

Author: **ReaAurelis** | Platform: Minecraft Bedrock, LeviLauncher Android ARM64.

## Standalone: ChruPipe backend (Audius replaced)

ChruPipe (https://github.com/chukjosh/ChruPipe) is a GPL-3.0 self-hosted service with a Kotlin/Ktor backend powered by NewPipeExtractor. It is NOT a native .so library.

The Standalone .levipack now makes native JSON requests to its self-hosted HTTP API, specifically GET /search?q=QUERY&service=youtube and GET /stream?url=CONTENT_URL. It parses title, artist/uploader and artwork, resolves audio stream URLs, and plays with Android MediaPlayer via JNI. Existing LRCLIB timestamped lyrics remain available.

**ChruPipe server is REQUIRED and must already be running.** No Android Bridge APK is needed for this variant, but the backend is NOT packaged in the .levipack. The user must self-host ChruPipe via a compatible Java environment in Termux, an existing VPS, or another server. Unofficial extractor responses and playback are not guaranteed.

To configure:

1. Run the ChruPipe Ktor backend according to its README; by default the server listens on port 8080. The React/Vite browser frontend is optional when using the JSON API.
2. Install LeviMusic-Standalone-1.2.1-arm64-v8a.levipack. Never enable two LeviMusic native mod variants.
3. In the LeviMusic Mod Menu settings, set **ChruPipe server URL** to http://127.0.0.1:8080 for a backend running on the same Android device, or to a reachable HTTPS origin for your own remote server.
4. Search through the in-game music menu. If results are empty, check the ChruPipe service using its HTTP /search route.
5. The optional experimental WEB button still opens a separate visible WebView; it is not the ChruPipe engine.

No Audius search/streaming is used in the Standalone mode. ChruPipe access does not bypass paid plans, DRM, ads, country restrictions or upstream terms of service.

## New optional real-world 3D lyric renderer

The extra file LeviMusic-WorldLyrics3D-1.2.1.mcpack provides genuine Minecraft world-space text using @minecraft/server TextPrimitive, introduced in Bedrock 26.20. Unlike HUD 2D scatter, each line is a text primitive positioned several blocks in front of a player's camera with random lateral/vertical offsets and optional depth testing.

The pack explicitly sets visibleTo to just the player who triggered the text, so others do not see it. It creates no world entity, changes no blocks, fades the text in/out, and replaces the previous line when another line arrives.

**Install:** import the behavior pack, enable it for the desired world, and enable the Beta APIs experiment if prompted. Test in a command-enabled world:

    /scriptevent levimusic:lyric Hello from LeviMusic in true 3D
    /scriptevent levimusic:clear

**Important limitation:** this release does NOT yet automatically feed LRC lines from the native mod into the behavior pack. The Script API and native Preloader HUD have no already verified bridge. The new pack proves true 3D world rendering and client-restricted visibility through manual script events; automatic music synchronization is remaining integration work. The renderer is not included by default in the .levipack; it is explicitly opt-in as a separate addon. Avoid advertising automatic 3D karaoke until linked and tested.

Minecraft Docs:
- https://learn.microsoft.com/en-us/minecraft/creator/scriptapi/minecraft/server/textprimitive?view=minecraft-bedrock-experimental
- https://learn.microsoft.com/en-us/minecraft/creator/scriptapi/minecraft/server/primitiveshape?view=minecraft-bedrock-stable

## Full mod unchanged

The full LeviMusic-1.2.1-arm64-v8a.levipack + LeviMusic-Bridge-1.2.1.apk retains Android Bridge Media3, search/playlists and its unofficial YouTube extractor. It does not require ChruPipe. Permanent Android signing from 1.2.0 is retained for safe APK upgrades.

This release is still RC; compilation is not proof of successful playback and world-render testing. See FEATURES.md and the release notes.
