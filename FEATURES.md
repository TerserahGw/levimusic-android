# LeviMusic 1.2.4 — feature status

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

## Floating lyrics safety update

The native world renderer is suspended in v1.2.4 because version-specific hooks can freeze Minecraft. The option uses the safe, client-only 2D lyric scatter. True world-space 3D rendering is unavailable in this release.


## Other
- The full mod retains the Android Bridge APK with Search, Media3 playback, ChruPipe-free remote controls, saved playlists, artwork, and online LRCLIB lyrics.
- Standalone retains its externally hosted ChruPipe backend requirement and optional visible WebView; it does not provide full Bridge queue editing.
- Signed APK update chain retained from 1.2.0. Do not load full and standalone .levipacks together.
- Release Candidate until successfully tested in game; GitHub Actions compile success does not confirm runtime stability.
- v1.2.4: Android Home/Playlists/Settings, mini and expanded player, song options, ICU romanization default-on. Native 3D hook suspended; floating 2D fallback is client-only.
