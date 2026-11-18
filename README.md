# LeviMusic Online — LeviLauncher Android (Beta source 0.1.0)

**Status: Android ARM64 GitHub Actions build has succeeded; runtime inside Minecraft/LeviLauncher Android has NOT yet been tested.**
This repository contains the source code. Download the installable `.levipack` from GitHub Releases after the release workflow succeeds.

## What it tries to do

- **100% Bedrock** through LeviLauncher Android native Preloader SDK (not Minecraft Java).
- Search songs by title/artist **in the game's Mod Menu**: enter *Search Query*, tap the *Cari* HUD button.
- Stream **original song recordings** from the **Audius public catalog**, no local MP3 import.
- Play/Pause, Next, Previous, Stop, Volume.
- Optional lyrics synced from LRCLIB if provided for the chosen song; one current line on HUD.
- Built for **arm64-v8a Android**, including Android 16.

**Limitations**: Not Spotify/YouTube Music; their catalogs are not provided by Audius. Many mainstream copyrighted songs will not be available, and tracks, endpoints, lyrics, or playback may fail. The Android MediaPlayer is controlled from a worker thread. There has been no runtime validation on LeviLauncher, so this is a development prototype, not a guaranteed working release. Online streams require Minecraft/LeviLauncher to have internet permissions, and must comply with the provider's terms.

## Android-only workflow: Build through GitHub Actions from Termux (NO PC required)

1. Install Termux, `pkg update && pkg install git gh unzip -y`.
2. Extract the **source ZIP** under Termux home (not a path with executable restrictions):
   ```sh
   cd ~
   unzip /sdcard/Download/LeviMusic-Android-Source.zip
   cd LeviMusic-Android-Source
   ```
3. Login to GitHub via `gh auth login` (browser flow).
4. Publish a **private** repository with:
   ```sh
   bash publish_termux.sh
   ```
5. Open **GitHub -> repository -> Releases** (or Actions -> Build & Release LeviMusic Android ARM64). Each successful push to `main` creates a new versioned **pre-release** and attaches `LeviMusic-<version>-arm64-v8a.levipack` directly. You can also get the build artifact from Actions.
6. Open LeviLauncher Android -> Mods -> Add Mod and select the `.levipack`. Start Minecraft. In-game open LeviLauncher Mod Menu -> LeviMusic Online -> edit `Search Query`, tap the `Cari` button. Find results automatically play their **first** match. Use `>>` and `<<` buttons to change among the 20 search results.

IMPORTANT: GitHub Actions has compiled successfully, but actual runtime loading and music playback in LeviLauncher have not been validated. Later builds may still fail. Never import a source ZIP directly.

## Alternative: Build on Linux x86_64 with Android NDK

Requires Android NDK r28.2, CMake 3.22+, Ninja, Git, internet.

```sh
export ANDROID_NDK_HOME="$HOME/Android/Sdk/ndk/28.2.13676358"
./build.sh
```

Expected output: `build/LeviMusic-0.1.0-arm64-v8a.levipack`.

## Package format

After compiling, the levipack has files at the ZIP root, as in LiteLDev's official `examples/full-cpp-mod`:

```
manifest.json
liblevimusic.so
```

`type` = `preload-native`. It uses Preloader SDK tag `0.2.3`, which includes the Mod Menu Text configuration type.

## Known engineering TODO / security & reliability

- Verify **ABI** against the exact version of LeviLauncher Android on device. Preloader 0.2.3 uses a different API from 0.2.2; menu text input depends on matching launcher support.
- Thoroughly test Android MediaPlayer behavior for HTTP redirects, TLS, errors, network latency and `prepare()` stalls, including Android 16 and MC version differences.
- Add a real search results list rather than immediately playing the first result; current `>>`/`<<` navigates results.
- Robust retry/failover for Audius API hosts and filtering inaccessible/unsupported streams.
- Real UI panel and album art, keyboard focus / touch interactions, draggable HUD.
- Better lyrics synchronization with duration/artist matching, user settings persistence, and line wrapping.
- Load-time dependency behavior: verify bundled launcher `libpreloader.so` matches the one linked at build time. The mod contains only its library, not a second preloader.

## APIs and references

- LeviLauncher Android: https://github.com/LiteLDev/LeviLauncher-Android
- Official full C++ mod example: https://github.com/LiteLDev/LeviLauncher-Android/tree/main/examples/full-cpp-mod
- Preloader SDK: https://github.com/LiteLDev/preloader-android
- Audius API docs: https://docs.audius.co/api/
- LRCLIB: https://lrclib.net/

No token, password, copyrighted song, or DRM bypass tool is included.


## Automated versions and release notes

Each successful push to `main` runs the Android ARM64 build and creates a GitHub **pre-release** with the actual `.levipack` file (unless the same commit has already been released). The first version is `v0.1.0`; subsequent pushes increment the patch number (`v0.1.1`, `v0.1.2`, ...). Failed builds do not publish releases.

Release descriptions include the current feature list from [FEATURES.md](FEATURES.md) and a changelog automatically assembled from commit messages since the previous release tag. When adding or removing a feature, update `FEATURES.md` in the same commit and write a descriptive commit message. Mod `manifest.json` and the downloadable file name receive the generated version during the CI build.

GitHub Releases: https://github.com/TerserahGw/levimusic-android/releases

If release publishing fails with a permissions error, check **Repository Settings -> Actions -> General -> Workflow permissions** and allow read/write permissions for GitHub Actions.
