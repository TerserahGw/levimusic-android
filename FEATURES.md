# LeviMusic 1.2.12 — feature status

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

The opt-in native renderer draws lyrics as camera-facing glyphs at a fixed position in the Minecraft world. A new timestamp chooses a new nearby location. Rendering is local to LeviLauncher; there are no blocks, entities, world packs or packets, so multiplayer players do not see it. The audio playback worker does not scan Minecraft signatures or wait for game-render calls.

This experimental path uses an exact ELF Build ID when available, or resolves all seven native signatures from one audited Android layout on a later hotfix. A partial or altered match leaves 3D inactive, with a visible status. The 3D switch never silently draws 2D; the separate Legacy 2D option is available. A compiled build cannot establish that it works on a real device; test both the visuals and Play/Pause/Next in game.


## Other
- The full mod retains the Android Bridge APK with Search, Media3 playback, ChruPipe-free remote controls, saved playlists, artwork, and online LRCLIB lyrics.
- Standalone retains its externally hosted ChruPipe backend requirement and optional visible WebView; it does not provide full Bridge queue editing.
- Signed APK update chain retained from 1.2.0. Do not load full and standalone .levipacks together.
- The standard release remains stable; the 3D setting is individually labeled Experimental and defaults OFF. GitHub Actions compilation does not confirm runtime stability.
- v1.2.4: Android Home/Playlists/Settings, mini and expanded player, song options, ICU romanization default-on. Native 3D hook suspended; floating 2D fallback is client-only.
- v1.2.5: World Analysis-style native billboard glyph renderer, exact game-build checks, asynchronous preparation, client-only world positioning, 2D fallback on unsupported builds.
- v1.2.6: Fixed romanization in Bridge mode and immediate re-render on toggle; added verified signature-based hotfix matching, and removed the misleading 2D display from the 3D option.
- v1.2.7: Preserve original LRC lines with smaller Latin readings in the app and mod. The Bridge uses Kuromoji for Japanese kanji, adds vetted LRCLIB/NetEase/manual video captions, and fetches lyrics on a separate mod worker so controls stay responsive. The unsupported 3D signature shown on user builds is still unresolved; renderer logcat reports how far each profile matches.
- v1.2.8: Adds a Build ID checked native 3D profile for Minecraft 1.26.62.1 using the user's actual ARM64 game binary. Hook installation should now proceed on that exact binary; a device test is required to verify the panel appears and follows the camera.
- v1.2.9: On the exact audited game Build ID, recognize existing mod hook stubs if the following function body still matches; keep hook chaining. Anchor world lyrics on player position, reduce the glyph scale and outline weight, render mixed-case Latin glyphs, and correct Japanese small-tsu and contracted syllables. Coexistence with independently hooked mods still requires an on-device test.
- v1.2.10: Improve loanword romanization, including フェ as fe, ティ as ti, and ヴァ as va.
- v1.2.11: Keep the 3D lyric origin attached to the player while placing the panel along the player's vertical look direction. Reposition as the player turns, and shorten the forward distance so nearby walls are less likely to hide it. Fixes active renderer status while the lyric is outside the visible field.
- v1.2.12: Lower the world lyric panel for first person. Each line gets one fixed world position until the next lyric; the lateral scatter now spans 3.4 blocks. The Bridge supplies its token automatically to the Minecraft process through a caller-UID-checked Android provider; manual Pair remains available for launchers using another process identity.

