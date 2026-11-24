# LeviMusic - Aurelis Online

**A client-side music player with synchronized lyrics for Minecraft Bedrock on LeviLauncher Android (ARM64).**

[![Latest release](https://img.shields.io/github/v/release/TerserahGw/levimusic-android?label=Latest%20release)](https://github.com/TerserahGw/levimusic-android/releases/latest)
[![License: GPLv3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)

**Stable:** v1.6.0 (24 November 2026)  
**Author:** Liliyana Rea de Aurelis · **Platform:** Android arm64-v8a · **Loader:** LeviLauncher

LeviMusic combines an in-game music interface with a separate Android companion (LeviMusic Bridge). It includes music discovery, playlists, an editable Up Next queue, synchronized lyrics, and optional 3D floating lyrics rendered locally in your Minecraft world.

**[Download the latest release](https://github.com/TerserahGw/levimusic-android/releases/latest)** · [All releases](https://github.com/TerserahGw/levimusic-android/releases) · [Feature details](FEATURES.md)

## Features

- **Music player and discovery:** play music using the Android Bridge, browse/search tracks, see artwork, and control playback.
- **Search / Playlist / Lyrics tabs:** a touch-friendly in-game interface for landscape Android. Create playlists and add or remove their tracks directly in game.
- **Editable Up Next:** starting a song creates a temporary related-track radio with up to three future suggestions when available. Remove, play, or add songs to the queue. Saved playlists retain their own order.
- **Synchronized lyrics:** active-line highlights and word-by-word timing when a source provides authentic word timestamps. Smaller romanized readings appear beneath the original text.
- **Multiple lyric sources:** QQ Music/QRC, LRCLIB, NetEase, KuGou, lrc.red, BetterLyrics, LiriQo, local lyrics, and supported caption fallbacks. Results vary by song and provider availability.
- **3D floating lyrics:** client-only world-space lyric panels with Unicode font fallback, four font styles, four color palettes, segmented transitions, and configurable placement. No per-world addon is required; other players do not see your lyrics.
- **2D HUD lyrics:** synchronized words stay above the compact HUD while 3D lyrics are on; the separate legacy scatter remains optional.
- **Settings:** a four-page in-game panel for common controls, with Advanced Settings for the complete list. The default 3D layout uses the verified user preset.

This project is an unofficial community mod. It is not affiliated with Mojang, Microsoft, or the LeviLauncher maintainers.

## Compatibility

| Component | Requirements / limitations |
| --- | --- |
| Device | Android **ARM64 (arm64-v8a)** |
| Game / launcher | Minecraft Bedrock running through LeviLauncher Android with a compatible native mod loader |
| Core music features | Full mode uses Bridge; Standalone defaults to the hosted Aurelis Symphony API and needs internet access, without a user-run server |
| 3D world lyrics | Native profiles for Minecraft **1.26.52**, **1.26.60**, **1.26.62.1**, and **1.26.66.1** (exact ARM64 Build ID only), and only when the ARM64 binary's Build ID/signatures match |
| Minecraft 1.26.66.1 | Exact binary profile added in v1.5.1; the maintainer reported working 3D lyrics on this version. v1.6.0 still needs device testing |

**Important:** A successful CI build does not prove on-device compatibility. The renderer checks native game signatures and may refuse to activate on an unmatched game binary. Do not assume all Minecraft patch versions support 3D lyrics.

## Installation

Choose **one** LeviPack variant from the [v1.6.0 stable release](https://github.com/TerserahGw/levimusic-android/releases/tag/v1.6.0). Do not enable both variants at once.

### Full mode (with Android Bridge)

- [LeviMusic-1.6.0-arm64-v8a.levipack](https://github.com/TerserahGw/levimusic-android/releases/download/v1.6.0/LeviMusic-1.6.0-arm64-v8a.levipack)
- [LeviMusic-Bridge-1.6.0.apk](https://github.com/TerserahGw/levimusic-android/releases/download/v1.6.0/LeviMusic-Bridge-1.6.0.apk)

Install both files. The Bridge app handles playback, searches, and playlists. Existing playlists are migrated from Android preferences to an atomic file on the first edit; export and import remain available in the app.

### Standalone mode (without Bridge)

- [LeviMusic-Standalone-1.6.0-arm64-v8a.levipack](https://github.com/TerserahGw/levimusic-android/releases/download/v1.6.0/LeviMusic-Standalone-1.6.0-arm64-v8a.levipack)

Standalone uses the hosted Aurelis Symphony API. It needs internet access but no user-run server or Bridge APK. Playlists are stored in the mod's configuration folder with a backup. Upstream YouTube streams can still fail if the provider blocks access.

## New in v1.6.0

- Import a public YouTube or YouTube Music playlist URL from **Playlists** in the Bridge app, or paste it into the in-game playlist name field. Imported track lists are saved locally. The Bridge uses the provider's playlist title; Standalone names it by playlist ID. Private playlists are not supported, and Standalone's YouTube page parser may not retrieve every page.
- In Full mode, choose **Save to Offline** on a search result. Bridge saves the audio in private app storage, stores available synchronized LRC, and creates an **Offline** playlist. Downloaded audio plays without a network connection; adding new music still needs internet.
- In Standalone, place audio files and an optional matching `.lrc` file in the mod configuration directory's `music/` folder (beside `settings.conf`). Open **Playlists → Offline**, or search for `offline`. Supported extensions: MP3, M4A, AAC, OGG, Opus, FLAC, WAV, MP4, and WebM, subject to Android's codec support.
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
