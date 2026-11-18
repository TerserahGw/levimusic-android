# LeviMusic 1.2.1 RC — Feature status

Author: ReaAurelis.

### Standalone — ChruPipe source
- Replaces runtime Audius discovery/streaming with a native ChruPipe self-hosted JSON API client. No Android Bridge APK.
- Requests /search and /stream from the configured Ktor/NewPipeExtractor backend; supports loopback HTTP on 127.0.0.1 and remote HTTPS.
- Uses Android MediaPlayer for the resolved sound URL and keeps cover/title/uploader, LRCLIB and transport controls.
- **Requires a separately running ChruPipe server**. The Kotlin Ktor service is not embedded into .levipack; this is NOT a self-contained offline extractor.
- No guarantee of unlimited licensed catalog access, or of YouTube extraction staying operational.

### 3D World Lyrics — optional, separate .mcpack
- A real 3D text object in Minecraft world, using the experimental @minecraft/server TextPrimitive API (not a 2D HUD trick).
- Each new script event removes the prior line and samples a new position in front of the user's camera.
- White text, transparent background, fade in and out, depth testing, and visibleTo restricted to the triggering player.
- Beta APIs/world pack must be manually enabled. No server entity is spawned.
- **Current protocol is manual /scriptevent only. Automatic LRC synchronization from the native player is NOT implemented.** Do not label this a finished feature.

### Existing full mode
- APK Bridge, 3-column player, search/playlists, artwork, timed LRCLIB lyrics, signed APK updates, mod icons and visual settings stay supported.
- Release Candidate (RC); on-device testing still required.
