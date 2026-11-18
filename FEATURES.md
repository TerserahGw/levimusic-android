# LeviMusic Online v0.3.0 Beta

**Author:** ReaAurelis

## Official app bridge (new)
- Controls **Spotify** and **YouTube Music** official Android playback via active `MediaSession`, requiring the separately installed LeviMusic Bridge APK and granted Notification Access.
- Real title, artist, playback position/duration, Play/Pause, Next/Previous and seek where supported by the provider.
- No Audius catalogue required in the default Bridge mode; no scraping/download of copyrighted audio.
- No added hard-coded plays-per-day limit. The original music provider's account and playback restrictions still apply.
- Pairing key generated on device; bridge listens only on `127.0.0.1`.
- Bridge does not provide music search in the game; choose songs within the official app.

## Lyrics & UI
- Tries to match LRCLIB timestamped lyric entries using title, artist and the actual track duration.
- Briefly appearing/disappearing timed lyric bubble above or below the compact HUD.
- Full player shows neighboring timed lyric lines and highlights the active line.
- Restrained black / charcoal interface inspired by the supplied Apple UI Designer guidance. Soft corners, clear hierarchy and fewer accent colors; no proprietary fonts or images.
- Existing icon assets from Lucide, mini-player scaling/positioning, seek progress and touch controls remain.

## Fallback mode
- Legacy public Audius search/streaming remains available **only if Bridge mode is disabled** in Mod Menu. It is not a replacement for Spotify or YouTube Music.

## Honest limitations
- Cannot unlock Premium playback, erase advertisements, force background playback or bypass rights/region restrictions.
- No legal/official API for unlimited DRM-free, audio-only access to the complete Spotify or YouTube Music catalog.
- Android companion requires Notification Access; no direct in-game catalog search through MediaSession.
- Production stability, metadata quality, lyrics accuracy and overlay stacking still need testing on the user's actual device.
