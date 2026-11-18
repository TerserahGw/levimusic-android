# LeviMusic v1.2.0 RC

**Author:** ReaAurelis  
**Platform:** Minecraft Bedrock via LeviLauncher Android (ARM64).

v1.2.0 contains a new optional embedded Standalone WebView browser and a permanent APK release-signing process. Actual Android 16 runtime compatibility remains unverified.

## IMPORTANT: one-time signing setup in Termux

Previous builds were APKs signed with GitHub Actions debug certificates, which can change between runs. Android requires the same signing certificate for in-place APK upgrades. This cannot be fixed retroactively without the earlier private signing key. See Android documentation.

1. **Before uninstalling an old app**, if you use playlists, keep the old Bridge running. In Termux run: bash scripts/backup_playlists_old_termux.sh. Paste the Bridge Key from the installed app. The script writes LeviMusic-playlists-backup.json to Downloads if available.
2. Sync latest project source: git pull --ff-only origin main (inside your LeviMusic repository folder).
3. Termux dependencies: pkg install gh openjdk-17 openssl.
4. Run: bash scripts/setup_apk_signing_termux.sh.
5. This command creates/reuses a PKCS12 signing keystore in Termux's **private home directory**, sets two protected GitHub Actions Secrets, then triggers a GitHub Actions build.
6. KEEP A PRIVATE BACKUP of the keystore and password. Never commit either file or send them in chat.
7. Once a signed GitHub Release appears, if Android rejects the update due to the previous debug certificate, this specific transition requires a ONE-TIME removal of the old APK. Make sure playlist backup exists first.
8. Install the new signed Bridge APK, open **My Playlists -> Import backup**, choose the JSON file. Future updates should install on top of this new build if the signing key remains unchanged and versionCode increases.

Without the GitHub signing secrets, Actions deliberately **does not publish a new release**. This prevents distributing another APK that needs uninstalling on the next version. Debug artifacts may still be generated only for compiler testing.

## Full mod / Standalone

Full setup: LeviMusic Bridge APK and LeviMusic ARM64 .levipack. This has searchable catalog (via unofficial NewPipeExtractor YouTube results), playlist queue, player controls, artwork and lyrics through the authenticated local bridge. It still depends on unofficial provider access; Spotify Premium/YouTube Music Premium is not unlocked.

Standalone: LeviMusic-Standalone arm64 .levipack. The existing built-in music HUD still uses Audius, with an optional **WEB** control which embeds Android System WebView directly in the Minecraft Activity. Enable **Experimental embedded WebView (visible video)** in Mod Menu, then tap WEB. This mode needs no separate APK, but WebView may fail if Android 16 blocks the ActivityThread reflection used to find Minecraft Activity.

WebView is not an audio-extraction API and does not replace the native player's metadata/search/playlist backend. It opens a visible interactive browser UI, defaulting to https://m.youtube.com. User actions, website permissions, DRM, ads, and playback policies apply. Closing the browser destroys WebView. Browser rendering into the 3D world is not included. See docs/STANDALONE_BROWSER.md.

## Floating Lyrics in true 3D

The two uploaded reference .levipack mods were inspected without executing them: WebScreen uses native Android WebView/GL texture concepts; LHolo has native world projection/render hooks. That architecture can inform a version-matched, locally rendered 3D lyric billboard.

**NOT INCLUDED IN v1.2.0:** client-only world-space 3D lyric quads with new random world positions each line. These require safe camera and world-renderer hooks for the exact game version; using binary offsets from other mods without testing could cause black screens/crashes. There is a documented development plan in docs/WORLD_LYRICS_3D.md. The existing 2D lyric scatter remains separate, optional and off by default.

## Previous v1.1.2 features preserved

Three-column UI (player / synced lyrics / search and playlists), autoplay next and best-effort related-track recommendations, HUD config and cover art, lyric match by timestamps, Bridge pairing, playlist editing and Lucide icons.

## Troubleshooting

- APK update rejected: check package name and certificate; a one-time migration away from the old debug cert may be necessary.
- New signed release missing: run the signing setup script, then inspect GitHub Actions for failed checks.
- WEB fails to open: ensure Standalone mod, experiment ON, WebView installed; inspect Android logcat for LeviMusicWebView and NativeBrowser.open.
- 3D lyric absent: that experimental renderer has not been implemented. Do not confuse it with 2D scatter.
- Back up playlists before app uninstall; uninstall may erase private app data.

Resources: docs/STANDALONE_BROWSER.md, docs/WORLD_LYRICS_3D.md, FEATURES.md, THIRD_PARTY_LICENSES.md.