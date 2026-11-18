# LeviMusic v1.1.2 Release Candidate

**Author:** ReaAurelis | Minecraft Bedrock LeviLauncher Android ARM64

## Three-area player UI
- **Music Player (left):** cover art, title, artist, status, slim seekbar, center Play/Pause, Previous and Next.
- **Synced Lyrics (center):** a separate permanent panel, showing highlighted active lyric and the surrounding LRC lines, or a clear unavailable state.
- **Search / Playlist / Songs (right):** discover songs, create/find/select playlists and tracks, Add to playlist; buttons and page rows have separate non-overlapping areas.
- Responsive landscape column proportions, improved spacing and dynamic marquee length.
- Existing dark theme, rounded controls, Lucide icons and open/close animation are preserved.

## Automatic next track
- Media3 handles playback completion by advancing automatically.
- **Playlist mode:** next saved track, looping to first when the playlist ends.
- **Search mode:** next track in the search queue; on exhaustion, discover a related song from YouTube via NewPipeExtractor and append it to the queue.
- Recommendation fallback searches the current artist and title, excludes recently played IDs and filters very short, very long or live streams.
- Source is unofficial; YouTube extraction or related-item discovery may fail.
- No hard-coded daily playback quota; access and restrictions are upstream-controlled.

## Experimental floating lyrics
- User requested **real 3D world-space, client-only lyric text** appearing at a different visible world position for each new LRC line. **Not implemented**: the pinned Preloader SDK supplies HUD overlay primitives but no stable 3D camera/world text render API.
- Legacy optional feature now clearly states **2D lyric scatter**, defaults OFF, and changes screen position per timestamped line.
- A correct 3D implementation requires a game-version-specific camera/render hook and real-device verification. No world entities are spawned, and no fake 3D claim is made.

## Other
- Companion app + native Bridge .levipack and optional standalone Audius-only .levipack.
- Timestamped lyrics from LRCLIB or user-supplied LRC; no invented lyrics.
- RC status, not confirmed stable until Android and Minecraft tests pass.
