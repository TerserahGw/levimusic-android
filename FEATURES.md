# LeviMusic 1.1.0 Release Candidate

**Author: ReaAurelis** · **Minecraft Bedrock / LeviLauncher Android ARM64**

## Music player & UI
- Remove the redundant Stop button at the lower-left corner of the player; the **center button alone toggles Play/Pause**.
- Animate panel opening/closing with a restrained 240ms ease and subtle vertical movement when UI animations are enabled.
- Animate long track names with a UTF-8-aware marquee on the mini HUD and Now Playing panel.
- Maintain soft dark surfaces, Lucide SVG icons, 2-column layout, cover art, and configurable HUD scale, position and animations.
- The native mod remains usable without adding on-screen controls outside the LeviMusic HUD.

## Playlist system
- Playlists persist inside the Android companion's private preferences (up to 40 playlists, 300 tracks each).
- Companion UI can **create, browse, add songs, remove songs, play playlists, and delete playlists**.
- Native in-game UI has **PLAYLISTS / SEARCH** navigation, playlist filtering, creation, browsing, track selection and playback, plus an Add button for adding the currently selected search song to the last opened playlist.
- New local authenticated endpoints: `/v1/playlists`, `/v1/playlist/create`, `/v1/playlist/tracks`, `/v1/playlist/add`, `/v1/playlist/remove`, `/v1/playlist/delete`, `/v1/playlist/play`.
- Playback queue for playlists is separate from the last search results, so browsing songs does not discard the active playlist's next/previous sequence.

## Better local pairing
- In the companion APK's settings, tap **Pair Minecraft (copy)** to copy a private key.
- In the Minecraft mod, tap **PAIR**. The mod reads the clipboard only on that user action, verifies the bridge locally, and stores the key in its private config folder for reconnection after restarting the game.
- The bridge remains protected by a per-installation secret, listening only at `127.0.0.1`. No anonymous control API or publicly accessible music server is introduced.
- Manual pairing remains possible where clipboard access is restricted.

## Lyrics
- Prefer a user's imported local synchronized LRC file (select a song in the companion first, then tap **LRC**).
- Add LRCLIB exact lookup by track signature, followed by structured and broad searches. Rank candidates by title, artist and duration; reject substantially mismatched recordings.
- Synchronized white lyric text with black outline and no background, lasting until the next timestamp.
- **Experimental floating lyric overlay (default OFF)** gently drifts in free screen space. It is a **2D HUD effect**, not a 3D label attached to a world position, block or avatar.

## Two installable variants
- **LeviMusic 1.1.0** (.levipack + APK companion) supports music discovery, playlists, controls and artwork from the Android music app via its authenticated local Bridge. The app's embedded engine uses Media3 and unofficial NewPipeExtractor-based YouTube stream discovery.
- **LeviMusic Standalone 1.1.0** (.levipack only) defaults to the existing native Audius public catalog; it does not embed an actual web browser, and cannot claim YouTube Music/Spotify full-catalog streaming without an APK. Install only one LeviMusic variant.

## Limits
- This is an **RC until actual tests on the user's Android 16/LeviLauncher/Minecraft environment pass**; successful CI compilation is insufficient to call it Stable.
- NewPipeExtractor access is unofficial, can break, and must not be presented as free unlimited streaming of Spotify Premium or YouTube Music Premium. Restrictions and rights remain provider-controlled.
- No world-space lyric anchoring or safe camera projection API was established in the pinned Preloader HUD interface.
- Playlists and imported LRC files are stored locally in the companion app; uninstalling it may delete them.
- Interactions with other mods' overlay z-order remain constrained by LeviLauncher's layer model.
