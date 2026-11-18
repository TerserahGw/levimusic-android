# LeviMusic Online v0.2.0 Beta

**Author: ReaAurelis**

## Interface / UI
- Smaller mini player HUD at the lower left (default size 84%), with dark rounded surfaces, audio visualization, and a thin progress line.
- Editable HUD size (55-125%) and horizontal / vertical position (0-100%) through the LeviLauncher Mod Menu.
- Animated equalizer / artwork indicator while audio plays, with an option to disable subtle animations.
- New two-column music panel with Now Playing, title, artist, lyrics, playback controls, seek bar, search, and six tracks per page.
- Thin 2.7-point duration bar and smoother progress updates (about every 110 ms when animations are enabled).
- Real SVG icons from Lucide (ISC license), rasterized to RGBA via lunaSVG, not text approximations.
- Default full panel starts lower on the screen (26% from the top) to reduce clashes with other launcher/mod overlay buttons. Panel Top is configurable from 15-35%.
- Settings icon opens LeviLauncher's Mod Menu. No extra LeviMusic floating buttons.

## Audio
- Audius online search (up to 20 results) and native Android MediaPlayer streaming.
- HTTP endpoint validation, redirect handling, Audius discovery host fallback, optional Audius API key.
- Play, pause, stop, previous, next, seek, volume and progress/duration labels.
- LRCLIB timed lyrics when a match exists.

## Limits and compatibility
- Kuro (https://github.com/ayashiiiyo/kuro) uses NewPipeExtractor and AndroidX Media3/ExoPlayer. That app's engine is **not embedded** in this native C++ .levipack. A companion Android service with licensed playback and authenticated IPC would be needed for that integration.
- The catalog remains **Audius**, not YouTube Music or Spotify. No access to their full copyrighted catalogs is promised.
- The overlay cannot force itself to appear above other mods' separately layered Android views. Use Panel Top and other overlays' placement settings to reduce conflicts.
- Animation is a lightweight data-driven visual; no true blur or alpha tween framework.
- Intended for LeviLauncher Android v1.7.1 (Preloader commit cc08b40a); still needs end-to-end testing on a real device.
- v0.1.2 was shown playing a song successfully by the user. v0.2.0 has not yet been tested on hardware.
