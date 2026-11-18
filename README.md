# LeviMusic Online v0.1.2 — LeviLauncher Android

LeviMusic is a **development beta** native C++ music mod for **real Minecraft Bedrock Android**, running through **LeviLauncher**. It supports **Audius** as its online music source and optional synced lyrics from **LRCLIB**.

**Compiled does not mean confirmed compatible.** We have not verified startup stability, touch/input mapping, audio playback or performance on a physical Android device after this update. If the game stalls, disable LeviMusic and collect the launch logs.

## v0.1.2 changes

1. Removed all old **LeviMusic on-screen external control buttons**.
2. Moved the compact **music HUD to the lower-left corner**.
3. Reworked the HUD with a dark rounded card, disc icon, mint accents, title, artist and progress.
4. Tap the mini-player to open the **full music interface**: left = player, playback, duration and seek; right = search and scrollable pages of results.
5. Search field uses the Android soft keyboard. Tap the field to clear the old query, type a new song, then tap **SEARCH** or press Enter. Tap a result to play it.
6. Added Audius stream redirect/HTTP-response probing, fallback host and optional API key. Audio playback is still **not guaranteed** until runtime testing on device.

### Why not Kuro as the engine?

[Kuro](https://github.com/ayashiiiyo/kuro) uses **NewPipeExtractor + AndroidX Media3/ExoPlayer**, tied to Kotlin/Java Android dependencies and a full Android app's configuration. A \`.levipack\` consists of a native \`.so\` and metadata; it cannot just import Kuro's application libraries as-is. To reuse a Kuro-like engine, a **separate Android companion / Java bridge** would be needed, including dependency and licensing review. v0.1.2 does **not** include or claim to use Kuro, YouTube Music, or NewPipeExtractor.

## Install

1. Download \`LeviMusic-0.1.2-arm64-v8a.levipack\` from the repository's **Releases** after the GitHub Actions workflow completes successfully.
2. Open **LeviLauncher Android → Mods / Package Management** and install the downloaded \`.levipack\`.
3. Use LeviLauncher **v1.7.1** for the best SDK ABI match. Earlier versions may not be compatible.
4. Start Minecraft with **only LeviMusic** enabled initially.
5. In game, tap the **LeviMusic HUD in the lower-left corner**, tap the search field, type, and search.
6. If both public Audius endpoints fail to stream, try a different Audius track and configure your own Audius API key under **Mod Menu → LeviMusic Online → Audius API Key**. Never share the key or commit it to GitHub.

## Features

See [FEATURES.md](FEATURES.md).

## Limits & known issues

- The catalogue is restricted to **public Audius tracks**, not every commercial song.
- A registered Audius API key may be needed depending on provider requirements. The free API key can be obtained from Audius developer plans.
- The app does not download/import individual MP3s, but uses internet connectivity for streaming and search.
- About 20 results per search; six results appear per page.
- LRC lyrics only when the lyric service returns a suitable timestamped match.
- The in-game HUD is rendered with Preloader primitives; **no true blur**, advanced native text widget, or album cover yet.
- Music controls depend on the Android media stack. Some codecs, network conditions and redirects may still fail.
- Touch interaction is not yet validated at every display resolution or Minecraft GUI scale.
- Disabling native mods while loading or on an unstable build may be unsafe; stop the game before replacing old versions.
- Full end-to-end mobile tests are still necessary. GitHub Actions only verifies compile/package.

## Development / Building

Android NDK r28.2, CMake, Ninja, Git needed. GitHub Actions builds ARM64 and publishes each successful push to main as a versioned pre-release, with features from \`FEATURES.md\` and changelog from Git history.

In Termux, synchronize your previously created working directory:

\`\`\`sh
cd ~/LeviMusic-Android-Source
git pull --ff-only origin main
\`\`\`

Build source and package details: [CMakeLists.txt](CMakeLists.txt). Latest artifacts: [Releases](https://github.com/TerserahGw/levimusic-android/releases).

### API references
- [LeviLauncher Android](https://github.com/LiteLDev/LeviLauncher-Android)
- [Preloader Android Input API](https://github.com/LiteLDev/preloader-android)
- [Audius API Docs](https://docs.audius.co/api/)
- [LRCLIB](https://lrclib.net/)

Copyright notice: Kuro and NewPipeExtractor are **not included** in this codebase.