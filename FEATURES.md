## LeviMusic Online v0.1.2 — Beta

- **Native Minecraft Bedrock ARM64 client mod** for LeviLauncher Android v1.7.1 (preloader commit cc08b40a3).
- **No external LeviMusic buttons.** A compact music HUD in the **lower-left corner** opens the interface when tapped.
- **New two-column music interface** drawn via Preloader HUD: player/controls on the left, online search/results on the right.
- **Touch controls:** play/pause, stop, next, previous, select any search result, page through up to 20 results.
- **Progress/seek bar** with elapsed time, duration and song/artist labels (duration depends on the media provider).
- **On-screen keyboard search** using the official native input callbacks; tap the search field to replace the current search query.
- **Audius streaming improvements:** check endpoint redirects and HTTP responses, retry the Audius discovery host, optional Audius API key in Mod Menu.
- **Optional synced lyrics** from LRCLIB when available.
- **Simple dark glass-inspired palette** and mint accent; the overlay does not implement actual backdrop blur.
- **Automatic GitHub Release:** versioned \`.levipack\` plus changelog after successful builds.

> **Runtime status:** development beta. UI/touch/audio have not been validated on a real Minecraft Bedrock Android device. The media catalog is **Audius**, not Spotify, YouTube Music or Kuro/NewPipeExtractor. The Kuro project's Java/Kotlin + AndroidX Media3 engine cannot be embedded directly into a native-only \`.levipack\` without an Android companion component. No Kuro code or NewPipeExtractor code was copied. Audius may require a free API key or impose service restrictions.