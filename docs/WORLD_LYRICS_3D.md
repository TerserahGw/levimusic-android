# 3D Floating Lyrics: Android world renderer

LeviMusic draws local lyric panels in Minecraft's 3D world through the LeviLauncher Android native mod. They are visible only to the local player: no world addon, server entity, or network packet is required. The implementation follows the native rendering path described in [World Analysis](https://github.com/callmehuda/World-Analysis); see [third-party licenses](../THIRD_PARTY_LICENSES.md).

## Compatibility

The renderer checks the exact ARM64 game Build ID and required signatures before hooking. Verified profiles cover Minecraft 1.26.52, 1.26.60, and 1.26.62.1. Minecraft 1.26.66.1 3D rendering remains unverified. A successful build does not establish compatibility with an unmatched Minecraft binary. When bindings are unavailable, 3D remains inactive and the player displays a status message. Playback runs separately from renderer preparation.

## Behavior and controls

- Word-timed lyrics reveal words at their timestamps when the provider supplies them. Line-timed lyrics use the line cue instead.
- Long lines split into short parts with ordered appearance, overlap, and gradual fading. Original script and optional smaller romanization are retained.
- Unicode font fallback, selectable font styles and palettes, spawn effects, adjustable distance, height, field of view, scattering, and follow/stay placement are available.
- The compact 2D HUD lyrics remain visible while 3D lyrics are on. Legacy 2D scatter is a separate option.
- The settings panel persists choices. If 3D does not appear, confirm the exact Minecraft version and ARM64 build identity and include the renderer status and logs in an issue.

The native hook remains inert after turning 3D off while a frame is executing; unloading the mod removes it. Independent mods that hook the same game function can still conflict on some builds.
