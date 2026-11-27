# LeviMusic - Aurelis Online

**A client-side music player with synchronized lyrics for Minecraft Bedrock on LeviLauncher Android (ARM64).**

[![Latest release](https://img.shields.io/github/v/release/TerserahGw/levimusic-android?label=Latest%20release)](https://github.com/TerserahGw/levimusic-android/releases/latest)
[![License: GPLv3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)

**Stable:** v1.6.9 (27 November 2026)
**Author:** Liliyana Rea de Aurelis · **Platform:** Android arm64-v8a · **Loader:** LeviLauncher

LeviMusic combines an in-game music interface with a separate Android companion (LeviMusic Bridge). It includes music discovery, playlists, an editable Up Next queue, synchronized lyrics, and optional 3D floating lyrics rendered locally in your Minecraft world.

**[Download the latest release](https://github.com/TerserahGw/levimusic-android/releases/latest)** · [All releases](https://github.com/TerserahGw/levimusic-android/releases) · [Feature details](FEATURES.md)

## Features

- **Music player and discovery:** play music using the Android Bridge, browse/search tracks, see artwork and artist details, and control Repeat or playlist Shuffle.
- **Search / Playlist / Lyrics tabs:** a touch-friendly in-game interface for landscape Android. Create playlists and add or remove their tracks directly in game.
- **Editable Up Next:** starting a song creates a temporary related-track radio with up to three future suggestions when available. Remove, play, or add songs to the queue. Saved playlists retain their own order.
- **Synchronized lyrics:** active-line highlights and word-by-word timing when a source provides authentic word timestamps. Smaller romanized readings appear beneath the original text.
- **Multiple lyric sources:** QQ Music/QRC, LRCLIB, NetEase, KuGou, lrc.red, BetterLyrics, LiriQo, local lyrics, and supported caption fallbacks. Results vary by song and provider availability.
- **3D floating lyrics:** client-only world-space lyric panels with Unicode font fallback, four font styles, four color palettes, segmented transitions, and configurable placement. No per-world addon is required; other players do not see your lyrics.
- **2D HUD lyrics:** synchronized words can be placed above or below the compact HUD and aligned left or right while 3D lyrics remain active.
- **Settings:** a six-tab in-game panel for common controls, including dedicated Experimental and Keybinds tabs, with Advanced Settings for the complete list. The default 3D layout uses the verified user preset.

This project is an unofficial community mod. It is not affiliated with Mojang, Microsoft, or the LeviLauncher maintainers.

## Compatibility

| Component | Requirements / limitations |
| --- | --- |
| Device | Android **ARM64 (arm64-v8a)** |
| Game / launcher | Minecraft Bedrock running through LeviLauncher Android with a compatible native mod loader |
| Core music features | Full mode uses Bridge; Standalone defaults to the hosted Aurelis Symphony API and needs internet access, without a user-run server |
| 3D world lyrics | Native profiles for Minecraft **1.26.52**, **1.26.60**, **1.26.62.1**, and **1.26.66.1** (exact ARM64 Build ID only), and only when the ARM64 binary's Build ID/signatures match |
| Minecraft 1.26.66.1 | Exact binary profile added in v1.5.1; v1.6.5 adds corrected and signature-locked environment queries plus native matrix validation |

**Important:** A successful CI build does not prove on-device compatibility. The renderer checks native game signatures and may refuse to activate on an unmatched game binary. Do not assume all Minecraft patch versions support 3D lyrics.

## Installation

Choose **one** LeviPack variant from the [v1.6.9 stable release](https://github.com/TerserahGw/levimusic-android/releases/tag/v1.6.9). Do not enable both variants at once.

### Full mode (with Android Bridge)

- [LeviMusic-1.6.9-arm64-v8a.levipack](https://github.com/TerserahGw/levimusic-android/releases/download/v1.6.9/LeviMusic-1.6.9-arm64-v8a.levipack)
- [LeviMusic-Bridge-1.6.9.apk](https://github.com/TerserahGw/levimusic-android/releases/download/v1.6.9/LeviMusic-Bridge-1.6.9.apk)

Install both files. The Bridge app handles playback, searches, and playlists. Existing playlists are migrated from Android preferences to an atomic file on the first edit; export and import remain available in the app.

### Standalone mode (without Bridge)

- [LeviMusic-Standalone-1.6.9-arm64-v8a.levipack](https://github.com/TerserahGw/levimusic-android/releases/download/v1.6.9/LeviMusic-Standalone-1.6.9-arm64-v8a.levipack)

Standalone uses the hosted Aurelis Symphony API. It needs internet access but no user-run server or Bridge APK. Playlists are stored in the mod's configuration folder with a backup. Upstream YouTube streams can still fail if the provider blocks access.

## Fixed and improved in v1.6.9

- Standard now obtains the active `BlockSource` from Minecraft's own verified block-query calls instead of relying on guessed object offsets.
- Room and cave probes now use the live world receiver, so enclosed spaces can switch Bridge playback to the cave environment reliably.
- Immersive Sound and 3D Lyrics Beats remain Standard-only.

## Fixed and improved in v1.6.8

- Immersive Sound and 3D Lyrics Beats are now Standard-only; Standalone no longer exposes or runs either experiment.
- Standard preserves environment updates sent before playback begins and periodically resynchronizes the active environment with Bridge.
- Room/cave detection covers higher ceilings and irregular walls, while a stronger PCM reflection chain makes indoor echo easier to hear.

## Fixed and improved in v1.6.7

- Standard immersive sound now runs through PCM processing so cave reflections and underwater filtering are retained by internal screen recording.
- Standalone fixes an Android 16 JNI constructor crash and adds a recordable early cave reflection.
- Cave/interior detection is stable across short block-query gaps, with stronger reflections and a longer decay.
- Playback pauses on world exit, resumes on re-entry only when auto-paused, and Bridge playback stops when Minecraft exits.

## Improved in v1.6.6

- Standard and Standalone now use a tunable environmental reverb path instead of relying on equalizer changes alone.
- Cave and interior detection scans a wider volume, including larger rooms and broad caves.
- Underwater audio keeps more bass presence while applying a much stronger high-frequency roll-off.
- Environment transitions refresh both the equalizer and reverb for the current playback session.

## Fixed in v1.6.5

- Corrected an invalid native matrix-stack receiver and added fail-closed matrix validation for Minecraft 1.26.66.1.
- Replaced the non-working Block-name layout guess with exact-build water and enclosure queries.
- Standard Bridge now refreshes its audio effects when the detected environment changes in the same playback session.
- Cave/interior reverb is stronger and underwater playback uses a clearly audible low-pass profile.

## New and fixed in v1.6.4

- Added Repeat and playlist-only Shuffle controls to Now Playing in both variants.
- Song and public YouTube playlist URLs can be pasted directly into the mod. Tracks imported from a public playlist can be added to saved playlists.
- Search, Up Next, and playlist rows now show cover art and artist names.
- Disabling LeviMusic through LeviLauncher Mod Menu now immediately hides the HUD and removes its touch targets.
- Saved playlists reliably loop from the last track back to the first.
- Added configurable keyboard bindings for opening the player, Previous, Next, and Play/Pause.

## Fixed in v1.6.3

- Standard/Bridge mode now always exposes the automatic **Offline** playlist in Minecraft.
- Before returning playlist data to the mod, Bridge reconciles the Offline playlist with downloaded and imported audio files stored by the app.
- Duplicate Offline entries are suppressed. This hotfix does not change Standalone playback.

## Fixed and improved in v1.6.2

- Offline non-Latin lyrics no longer display a **Font unavailable** placeholder in the 3D world. A line that cannot be rasterized and has no usable romanization is omitted from 3D while its HUD lyric remains available.
- Playlist playback now follows the saved order in Full and Standalone, keeps Up Next in the same sequence, and loops to the first track after the last.
- Standard settings include a dedicated **Experimental** tab for 3D Lyrics Beats; this experiment is not included in Standalone.
- HUD lyrics can be switched independently and positioned at Up Left, Up Right, Down Left, or Down Right around the compact player.
- Opening the music menu while moving no longer leaves the Minecraft player walking after the movement touch is released.

- The in-game **DL** action accepts Aurelis track IDs of supported lengths and recognizes actual MP3, M4A, WebM, Ogg, AAC, FLAC, and WAV data even when an audio gateway sends a generic content type. Standalone downloads do not rely on the separate playback probe.
- Bridge retries its offline download through Aurelis if the primary YouTube audio URL fails. An unavailable audio service can still prevent a download.
- Latin words separated by enhanced LRC timestamps, and readable CamelCase phrases in local LRC, retain their spaces. CJK lyrics are preserved.

## New in v1.6.0

- Import a public YouTube or YouTube Music playlist URL from **Playlists** in the Bridge app, or paste it into the in-game playlist name field. Bridge uses NewPipeExtractor first and Aurelis as a fallback; Standalone uses Aurelis first and a public page fallback. The playlist title and track list are stored locally. Private playlists and unavailable entries cannot be imported.
- In Full mode, use **Save to Offline** in the Bridge app or tap **DL** on an in-game search result. Bridge saves audio and any available synchronized LRC into an automatically created **Offline** playlist. You can also import local audio files and matching `.lrc` files together through the Bridge app's Android file picker, or add a matching LRC later.
- In Standalone, tap **DL** on an in-game online search result to download its audio and available synchronized lyrics. You can also place audio files and same-name `.lrc` files manually in the mod configuration directory's `music/` folder (beside `settings.conf`). Open **Playlists → Offline**, or search for `offline`. Supported extensions: MP3, M4A, AAC, OGG, Opus, FLAC, WAV, MP4, and WebM, subject to Android's codec support.
- Rename or delete playlists from the in-game list using **R** and **X**; tap **X** twice to confirm deletion. Bridge playlists can also be renamed or deleted in the app. Removing a playlist does not delete its downloaded audio.

## Changes in v1.5.1 stable

- Added an exact ARM64 3D renderer profile for Minecraft 1.26.66.1 based on its Build ID and seven verified instruction signatures. The maintainer reported 3D lyrics working on 1.5.1; changes in 1.6.0 still need device testing.

## Previous v1.5.0 changes

- Adopted the provided 3D lyric layout as the default for new installs, while retaining existing user settings. An empty `chruServer` uses the hosted Aurelis endpoint.
- Renamed the mod to **LeviMusic - Aurelis Online** and added a four-page settings panel inside the game.
- Create playlists, add search results using **P+**, open playlists, and remove songs with **X** in both modes.
- Improved settings and playlist persistence with temporary files and backups; Bridge playlists migrate without deleting the previous copy.
- Previous 3D lyric transitions, Unicode rendering, font choices, and color palettes are included.

## Troubleshooting

- **Music controls do not respond:** confirm the Bridge APK and full LeviPack use the same release version and that pairing is complete.
- **No 3D lyrics:** check the exact Minecraft ARM64 build; turn on the 3D feature only on compatible builds. Try Legacy 2D lyrics separately. See [3D technical notes](docs/WORLD_LYRICS_3D.md).
- **No lyric lines or recommendations:** third-party sources may be unavailable, rate-limited, or missing timestamp information for that recording.
- **Standalone cannot play:** check access to the hosted Aurelis Symphony API. An individual YouTube stream can be blocked despite successful search; test an original demo track to separate server connectivity from upstream audio restrictions.

Please report reproducible issues through [GitHub Issues](https://github.com/TerserahGw/levimusic-android/issues), including Minecraft version, device architecture, mod version, and relevant logs without private tokens.

## Distribution and updates

- GitHub Releases: [download and changelog](https://github.com/TerserahGw/levimusic-android/releases).
- LeviLauncher **External Mods / LeviModHub**: catalog metadata lives in [levimod.json](levimod.json), once the catalog entry is accepted by its maintainers.
- For a future release, keep the **version** and **minecraft_versions** in levimod.json aligned with its Git tag and verified support. Catalog entries must not claim unsupported renderer versions.

## License and acknowledgments

Licensed under **[GNU GPL v3](LICENSE)**. See [third-party licenses and acknowledgments](THIRD_PARTY_LICENSES.md) for external components and credited references. Respect applicable platform terms for any online media sources.
