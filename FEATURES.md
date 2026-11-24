# LeviMusic - Aurelis Online: Features

LeviMusic is a client-side music mod for Minecraft Bedrock on LeviLauncher Android ARM64. Choose the Full variant with the Android Bridge APK or the Standalone variant with hosted Aurelis Symphony. Both include an in-game player, search, playlists, Up Next, and synchronized lyrics. The Standalone variant requires internet access but no user-run server.

## Player and playlists

- Search for music, manage playback, and use the Search / Playlist / Lyrics tabs in the in-game interface.
- Create playlists, add search results, open saved playlists, and remove tracks from a playlist in both variants.
- Edit Up Next or use related-track suggestions. Availability depends on the source; a saved playlist keeps its own order.
- Full mode uses the Bridge APK for audio playback and companion controls. Standalone streams through Aurelis Symphony.

## Lyrics

- Synchronized 2D lyrics remain above the compact HUD, including when 3D lyrics are enabled.
- Word-by-word timing is used when a provider supplies word timestamps. Otherwise, lyrics follow line timestamps.
- Optional romanization shows a smaller Latin reading below the original script.
- Client-only 3D world lyrics support Unicode font fallback, font and color choices, configurable placement, and segmented transitions. Other players cannot see them; no per-world addon is needed.
- Native 3D rendering is limited to matching ARM64 builds of Minecraft 1.26.52, 1.26.60, and 1.26.62.1. 3D support for 1.26.66.1 is unverified.

## Settings and storage

- A four-page in-game settings menu exposes common controls; Advanced Settings includes the full list.
- Mod settings and Standalone playlists use temporary writes and backups. Bridge playlists are migrated from Android preferences on first edit, retaining the previous copy.

See the [README](README.md) for installation and troubleshooting. Online media and lyric providers may be unavailable for individual tracks.
