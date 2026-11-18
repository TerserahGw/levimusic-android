# LeviMusic Online — LeviLauncher Android (Beta source 0.1.0)

**Status: source project, syntax checked on host with interface stubs; NOT YET built as Android ARM64 `.so`, NOT yet tested inside Minecraft.**
This source ZIP is **NOT** an installable `.levipack`. GitHub Actions builds an actual `.levipack` using Android NDK.

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
5. In your repository at github.com, open **Actions -> Build LeviMusic Android ARM64**. The push to main triggers a build automatically. On success: choose the build run -> **Artifacts -> LeviMusic-Android-levipack** and unzip the downloaded GitHub artifact ZIP. Inside it is `LeviMusic-0.1.0-arm64-v8a.levipack`.
6. Open LeviLauncher Android -> Mods -> Add Mod and select the `.levipack`. Start Minecraft. In-game open LeviLauncher Mod Menu -> LeviMusic Online -> edit `Search Query`, tap the `Cari` button. Find results automatically play their **first** match. Use `>>` and `<<` buttons to change among the 20 search results.

IMPORTANT: GitHub Actions compilation or runtime load may still fail; this is a new untested mod. If it fails, the GitHub Actions log will show why. Do not import the source ZIP directly.

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
