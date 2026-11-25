# LeviMusic - Aurelis Online: Features

LeviMusic is a client-side music mod for Minecraft Bedrock on LeviLauncher Android ARM64. Choose the Full variant with the Android Bridge APK or the Standalone variant with hosted Aurelis Symphony. Both include an in-game player, search, playlists, Up Next, and synchronized lyrics. The Standalone variant requires internet access but no user-run server.

## v1.6.3 Standard Offline playlist hotfix

Standard mode now always exposes the automatic Offline playlist in Minecraft. The Bridge reconciles playlist metadata with audio actually downloaded or imported through the app before serving the playlist and its tracks. Duplicate Offline entries are removed. Standalone behavior is unchanged.

## v1.6.2 playlist, HUD, and input fixes

Saved playlists now drive Up Next in their stored order and loop after the final track in both variants. HUD lyrics have an independent visibility switch and four corner positions around the compact player. 3D Lyrics Beats is grouped under a dedicated Experimental tab. Opening the menu while a movement touch is active no longer traps movement. If an offline non-Latin lyric cannot be rasterized and has no usable romanization, LeviMusic skips that 3D line instead of drawing a missing-font message; its 2D HUD lyric remains available.

## v1.6.1 offline hotfix

The in-game DL button accepts valid Aurelis track IDs and verifies downloaded audio by its file signature, including responses marked application/octet-stream. Bridge retries downloads through Aurelis when its primary source fails. Enhanced and local LRC now restore Latin word spaces without inserting spaces into CJK text.

## Offline music and playlist import

Full mode saves a searched song and available synchronized LRC from the Bridge app or the in-game **DL** button. The Bridge app also imports local audio files and matching `.lrc` files via Android's document picker; a later LRC-only import can match a song already in Offline. Standalone downloads a searched online track and available lyrics with the in-game **DL** button, or scans manually placed audio and same-stem `.lrc` in the mod configuration directory's `music/` folder. Both variants show saved tracks in Offline. Android codec support determines playable formats.

Public YouTube and YouTube Music playlist URLs can be imported from the Bridge app or in-game playlist field. Bridge first uses its NewPipe extractor and falls back to the Aurelis playlist API. Standalone uses Aurelis first, then a public YouTube page parser. The track list is saved locally without downloading audio. Private or inaccessible playlists and unavailable entries cannot be imported.

## Player and playlists

- Search for music, manage playback, and use the Search / Playlist / Lyrics tabs in the in-game interface.
- Create playlists, add search results, open saved playlists, and remove tracks from a playlist in both variants.
- Edit Up Next or use related-track suggestions. Availability depends on the source; a saved playlist keeps its own order.
- Full mode uses the Bridge APK for audio playback and companion controls. Standalone streams through Aurelis Symphony.

## Lyrics

- Synchronized 2D lyrics can remain above or below the compact HUD, aligned left or right, including when 3D lyrics are enabled.
- Word-by-word timing is used when a provider supplies word timestamps. Otherwise, lyrics follow line timestamps.
- Optional romanization shows a smaller Latin reading below the original script.
- Client-only 3D world lyrics support Unicode font fallback, font and color choices, configurable placement, and segmented transitions. Other players cannot see them; no per-world addon is needed.
- Native 3D rendering is limited to matching ARM64 builds of Minecraft 1.26.52, 1.26.60, and 1.26.62.1. 3D support for 1.26.66.1 is unverified.

## Settings and storage

- A five-tab in-game settings menu exposes common controls and a dedicated Experimental tab; Advanced Settings includes the full list.
- Mod settings and Standalone playlists use temporary writes and backups. Bridge playlists are migrated from Android preferences on first edit, retaining the previous copy.

See the [README](README.md) for installation and troubleshooting. Online media and lyric providers may be unavailable for individual tracks.
