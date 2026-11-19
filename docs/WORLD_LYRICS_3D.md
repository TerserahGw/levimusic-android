# 3D Floating Lyrics — experimental Android renderer (v1.2.8)

Turn on the option in LeviMusic's mod menu. With a timed lyric track and a supported Minecraft build, each lyric is drawn as a camera-facing world-space billboard near the player. The panel remains fixed as the player looks around until the next lyric line changes it. It is local-only: no addon per world, spawned server entity, network packet, or server permission is needed; other players cannot see it.

The native path follows the Android render and material handling in [World Analysis](https://github.com/callmehuda/World-Analysis) by Ekowat, derived from BedrockTools by RadiantByte. See `THIRD_PARTY_LICENSES.md`. LHolo's installed Android package was examined for comparison but no binary or unpublished source was copied.

## Compatibility and controls

- The 3D option is OFF by default. Audited ARM64 Minecraft 1.26.52 and 1.26.60 layouts supply seven function signatures and offsets. A later hotfix can use the same layout only if all seven function signatures match; there is no mixed profile or arbitrary offset guess.
- Preparation runs on a separate thread and does not block the music player worker.
- On a missing or altered binding, 3D stays inactive and the player card shows the reason. The 3D option does not show a misleading 2D panel. Enable the distinct Legacy 2D lyric scatter if wanted.
- A hook stays installed but inert after turning the option OFF, avoiding live hook churn while a frame is executing. Module unload removes it.
- LRC lines are rendered with a bounded two-row, 5×7 Latin glyph mesh and 180 ms fade. The app and mod romanization option helps convert non-Latin scripts; an unsupported character becomes `?` in the current native glyph font.

The source compiles and the local render-detour smoke test covers geometry submission, Minecraft's original render callback and color restoration. It has **not** been visually tested on a physical Android device. Verify the panel and Play/Pause/Next on your exact game build; send the build identity and logs if status reports unsupported or binding conflict.

The user's Minecraft 1.26.62.1 ARM64 binary has Build ID `c60fa32908019eecae39ebfe7bac5755dc35cde2`. v1.2.8 adds its seven verified function entry points, including the changed material-group address calculation. This enables an exact build binding; world-space drawing and camera layout still require an on-device test. The working LHolo package uses a different render entry point, so it cannot establish LeviMusic's object layouts by itself.
