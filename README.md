# LeviMusic Online v0.2.0 Beta

**Author: ReaAurelis**

Native online music player for Minecraft Bedrock on LeviLauncher Android (ARM64).

## Install & run
1. Download `LeviMusic-0.2.0-arm64-v8a.levipack` from [Releases](https://github.com/TerserahGw/levimusic-android/releases) after the release workflow succeeds.
2. Disable/remove the old LeviMusic version, then import the new `.levipack` using LeviLauncher Android Mods.
3. Launch Minecraft and tap the compact music pill near the lower-left to open the full player.
4. Search for songs, use the transport controls, and tap **Settings** to open the LeviLauncher Mod Menu for size and placement controls.

## What's new
- Mini-player is smaller, with an animated equalizer and slim progress line.
- Full two-column panel redesign; lowered panel position avoids most top-of-screen mod buttons. You can adjust the top offset (15-35%) in settings.
- Playback progress bar is now thin and refreshed more often when animations are enabled.
- 14 real SVG Lucide icons rendered through lunaSVG as in-game image commands.
- HUD scale (55-125%), horizontal and vertical placement (0-100%), and animation toggle are configurable.
- Author set to ReaAurelis.

## Audio-engine research
[Kuro](https://github.com/ayashiiiyo/kuro) uses NewPipeExtractor for source discovery and AndroidX Media3/ExoPlayer for media playback. These are Java/Kotlin Android dependencies; they cannot simply be imported into a native `.levipack` as a substitute C++ engine. A future companion Android app could expose an authenticated service/IPC to LeviMusic. **v0.2.0 uses the existing Audius search + Android MediaPlayer engine** shown working in v0.1.2. It does not integrate Kuro, NewPipeExtractor, or YouTube Music.

## Known limitations
- Only public Audius tracks (up to 20 search matches per query). Not every popular song is present.
- Synced lyrics only if LRCLIB contains a matching entry.
- Separate Android overlays from other mods can still cover LeviMusic. The mod can shift its own panel, but cannot control other overlays' z-order without launcher changes.
- HUD is built from Preloader primitives; no native backdrop blur or full animation framework.
- An APK build success does not guarantee proper in-game functionality. Test the new version with only LeviMusic enabled first.

## Sources & attribution
- [LeviLauncher Android](https://github.com/LiteLDev/LeviLauncher-Android)
- [Kuro](https://github.com/ayashiiiyo/kuro)
- [Lucide SVG icons](https://github.com/lucide-icons/lucide) (ISC)
- [lunaSVG](https://github.com/sammycage/lunasvg) (MIT)
- [Audius API](https://docs.audius.co/api/)
- [LRCLIB](https://lrclib.net/)

See [FEATURES.md](FEATURES.md) and [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

## Termux
```bash
cd ~/LeviMusic-Android-Source
git pull --ff-only origin main
```
