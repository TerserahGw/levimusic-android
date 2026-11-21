# LeviMusic Online

**A client-side music player with synchronized lyrics for Minecraft Bedrock on LeviLauncher Android (ARM64).**

[![Latest release](https://img.shields.io/github/v/release/TerserahGw/levimusic-android?label=Latest%20release)](https://github.com/TerserahGw/levimusic-android/releases/latest)
[![License: GPLv3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)

**Latest beta:** v1.4.1 beta (21 November 2026) · **Stable:** v1.3.6  
**Author:** ReaAurelis · **Platform:** Android arm64-v8a · **Loader:** LeviLauncher

LeviMusic combines an in-game music interface with a separate Android companion (LeviMusic Bridge). It includes music discovery, playlists, an editable Up Next queue, synchronized lyrics, and optional experimental 3D floating lyrics rendered locally in your Minecraft world.

**[Download the latest release](https://github.com/TerserahGw/levimusic-android/releases/latest)** · [All releases](https://github.com/TerserahGw/levimusic-android/releases) · [Feature details](FEATURES.md)

## Features

- **Music player and discovery:** play music using the Android Bridge, browse/search tracks, see artwork, and control playback.
- **Search / Playlist / Lyrics tabs:** a touch-friendly in-game interface for landscape Android.
- **Editable Up Next:** starting a song creates a temporary related-track radio with up to three future suggestions when available. Remove, play, or add songs to the queue. Saved playlists retain their own order.
- **Synchronized lyrics:** active-line highlights and word-by-word timing when a source provides authentic word timestamps. Smaller romanized readings appear beneath the original text.
- **Multiple lyric sources:** QQ Music/QRC, LRCLIB, NetEase, KuGou, lrc.red, BetterLyrics, LiriQo, local lyrics, and supported caption fallbacks. Results vary by song and provider availability.
- **Experimental 3D floating lyrics:** client-only world-space lyric panels with segmented transitions, multiple layouts, and configurable camera-related positioning. No per-world addon is required; other multiplayer users do not see your lyrics.
- **Legacy 2D lyrics:** available separately when 3D rendering is unsupported or disabled.

This project is an unofficial community mod. It is not affiliated with Mojang, Microsoft, or the LeviLauncher maintainers.

## Compatibility

| Component | Requirements / limitations |
| --- | --- |
| Device | Android **ARM64 (arm64-v8a)** |
| Game / launcher | Minecraft Bedrock running through LeviLauncher Android with a compatible native mod loader |
| Core music features | Full mode uses Bridge; Standalone defaults to the hosted Aurelis Symphony API and needs internet access, without a user-run server |
| 3D world lyrics | Native profiles for Minecraft **1.26.52**, **1.26.60**, and **1.26.62.1**, and only when the ARM64 binary's Build ID/signatures match |
| Minecraft 1.26.66.1 | Core music may work if the loader accepts the mod, but **3D world-lyrics compatibility has not been verified** |

**Important:** A successful CI build does not prove on-device compatibility. The renderer checks native game signatures and may refuse to activate on an unmatched game binary. Do not assume all Minecraft patch versions support 3D lyrics.

## Installation

Choose **one** of the following modes. **Do not install or enable both LeviPack variants at the same time.**

### Option A — Full mode (recommended)

Download both files from the **same release**:

1. **LeviMusic-1.3.6-arm64-v8a.levipack** — import/install this in LeviLauncher.
2. **LeviMusic-Bridge-1.3.6.apk** — install the Android companion app.

Open the Bridge, complete the available pairing/connection steps, then launch Minecraft through LeviLauncher. Full mode provides the Android music player, playlists, editable queue, and synchronized lyric integration.

Current v1.3.6 downloads:
- [Full LeviPack](https://github.com/TerserahGw/levimusic-android/releases/download/v1.3.6/LeviMusic-1.3.6-arm64-v8a.levipack)
- [Bridge APK](https://github.com/TerserahGw/levimusic-android/releases/download/v1.3.6/LeviMusic-Bridge-1.3.6.apk)

**Note for External Mods catalog users:** the catalog can distribute the LeviPack, but the **Bridge APK must also be installed separately** from GitHub Releases for full mode.

### Option B — Standalone mode (v1.4.1 beta)

Download [LeviMusic-Standalone-1.4.1-arm64-v8a.levipack](https://github.com/TerserahGw/levimusic-android/releases/download/v1.4.1-beta/LeviMusic-Standalone-1.4.1-arm64-v8a.levipack) instead of the full LeviPack.

This beta uses the **hosted Aurelis Symphony API** by default. No Bridge APK, local backend, account, or user-run server is required. It offers search, direct audio resolution, synchronized lyrics, and an editable radio queue. Internet access and the hosted service are required. YouTube audio can return HTTP 424 or stop if the upstream blocks streaming; the bundled original demo tracks are more reliable. See [Standalone browser notes](docs/STANDALONE_BROWSER.md).

For newer releases, use the matching files from the [latest release page](https://github.com/TerserahGw/levimusic-android/releases/latest) rather than mixing versions.

## What's new in v1.4.1 beta

- Standalone now reads the original lyric lines from Aurelis so word timestamps no longer remove spaces.
- Japanese, Korean, and Mandarin lyrics use Aurelis romanization. The original remains visible with a Latin reading beneath it; 3D lyrics use the Latin reading.
- In the mod settings, choose `romanizeLanguage` (`auto`, `ja`, `ko`, or `zh`) if automatic language detection gets a Kanji-only Japanese lyric wrong.
- Lyric fetching and romanization run separately from playback controls. If a reading is unavailable, the 3D renderer shows a clear message instead of question marks.

## Stable v1.3.6

- More fluid 3D lyric-segment entrances, with overlapping, ordered fades for old and new words.
- Added QQ Music QRC word-timing support alongside existing lyric-source fallbacks.
- Redesigned Bridge UI with larger artwork, an expanded player, touch-friendly queue, and timestamp-following lyrics with word highlights when timing data is available.
- **Minecraft 1.26.66.1 3D world lyrics remain unverified.**

Read the [v1.3.6 release notes](https://github.com/TerserahGw/levimusic-android/releases/tag/v1.3.6) and [detailed feature history](FEATURES.md).

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
