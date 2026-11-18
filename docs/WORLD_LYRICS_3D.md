# Client-only world-space lyric renderer — technical specification

## Request
Experimental OFF by default. Each synced lyric appears as a billboard in actual 3D Minecraft world space, visible only to the local player. On each new lyric timestamp, the old billboard disappears and another position is chosen around the player's field of view. Never create server entities, blocks, chat messages or network packets.

## Evidence from user-supplied mods
- WebScreen 1.0.23 includes native webscreen.so and references Android WebView, SurfaceTexture and OpenGL texture/framebuffer functions. This proves a browser-to-texture rendering route is technically plausible.
- LHolo 26.60.1 includes liblholo.so and references LevelRendererPlayer::renderBlockEntities, BaseActorRenderContext, ScreenContext, Tessellator, player/camera transforms and materials. This suggests client-side native world rendering is possible.
- The supplied packages have native binary files but no source code or documented public interoperability APIs. Their code has NOT been copied, bundled, or used as a linkable dependency.

## Intended implementation (NOT YET SHIPPED)
1. Resolve render hooks only for exact Minecraft build IDs. Fail closed on unknown versions.
2. Hook a compatible world-render stage on the render thread. Cache validated camera/view/projection matrices and texture/material context.
3. For each LRC line change, sample candidate world positions about 3 to 9 blocks ahead, limited by frustum, a clear line of sight, distance, nearby geometry and screen-space exclusion zones.
4. Render anti-aliased outlined white text as a client-only camera-facing textured quad. Respect depth, perspective and max projected dimensions.
5. Fade between old and new line for approximately 200 ms, but preserve readability until the line's end timestamp.
6. Dispose state on world exit, dimension changes, app pause and mod disable; test interactions with other render mods.
7. No world edits and no multiplayer-visible entities.

## Current status
True 3D lyric rendering is NOT implemented in LeviMusic 1.2.0. The pinned documented Preloader DrawCommands API renders HUD 2D, not world-space quads. Porting LHolo's binary engine offsets without tested symbol metadata risks native crashes and black screens. The existing optional 2D lyric scatter remains a distinct feature and defaults OFF.
