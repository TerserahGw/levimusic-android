# LeviMusic 1.2.2 RC — feature status

## Three-way music library UI
- New two-column layout: Now Playing on the left, a single right-hand pane with **SEARCH / PLAYLIST / LYRICS tabs**.
- Search and Playlist no longer compete with permanent lyrics column, avoiding overlapping controls on landscape Android.
- Playlist tab switches **UP NEXT** (temporary queue) and **SAVED** (user playlists). Song rows and action icons have separate tap areas; five compact rows per page.
- Lyrics tab displays LRC timeline, highlighted active line and nearby lines. Existing popup lyrics remain when native 3D is inactive.

## Related-music radio and editable queue
- Selecting ANY search result creates a NEW radio containing exactly that selected song. It no longer uses all search results as the playback order.
- Android Bridge uses YouTube NewPipeExtractor related-video items, falling back to artist/title similarity search when needed.
- In the background, the player attempts to keep **three recommendations ahead**; the Playlist / Up Next screen shows current track plus up to three future items.
- Next and automatic end-of-song advance use the actual radio queue. Refreshes happen when songs change or an Up Next entry is removed.
- Right-hand Search result '+' inserts that track into the current Up Next; Playlist / Up Next 'X' removes a suggestion (and excludes it for the current radio). Tap a future item to play it.
- Saved user playlists still retain their own ordered queue and are not replaced by radio.
- Three recommendations are best effort: extractor results may be missing or blocked; the service never invents song availability.

## True native 3D world lyrics — EXPERIMENTAL, OFF by default
- **No behavior pack, per-world addon or Script API required.** The native .levipack includes an experimental client-side Minecraft world-renderer hook.
- Uses game ARM64 signatures, LevelRenderer/Tessellator and camera/player rotation to create white, black-outlined *actual world-space* vector glyphs.
- Each new timed lyric gets a seeded random new anchor ~4.6 blocks in front of the camera, with lateral and vertical variation. The old line is replaced at the timestamp change.
- No world entities, blocks or network packets; other players should not see text, including in friends' worlds or public servers, because it is rendered in the local process only.
- Missing signatures cause the renderer to stay disabled; 2D lyric popup remains available.
- **Runtime has not been proven on the user's Minecraft build.** Native render offsets and hooked functions change with game versions and could crash Minecraft even after signatures match. Test with a backed-up local world first and disable if unstable.
- Initial font is original ASCII/Latin 5x7 vector glyphs only: non-Latin/Unicode characters are approximated by question marks. The current experiment does not raycast walls to guarantee empty space, and visual depth/visibility must be tested.
- World lyric mode is gated behind `Experimental true 3D world lyrics (native, risky)` in the LeviMusic Mod Menu; OFF by default.

## Other
- The full mod retains the Android Bridge APK with Search, Media3 playback, ChruPipe-free remote controls, saved playlists, artwork, and online LRCLIB lyrics.
- Standalone retains its externally hosted ChruPipe backend requirement and optional visible WebView; it does not provide full Bridge queue editing.
- Signed APK update chain retained from 1.2.0. Do not load full and standalone .levipacks together.
- Release Candidate until successfully tested in game; GitHub Actions compile success does not confirm runtime stability.