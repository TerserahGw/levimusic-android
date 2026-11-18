# LeviMusic Standalone — native embedded browser experiment, 1.2.0

Based on the user-supplied WebScreen package's observable architecture, the experimental Standalone variant uses Android System WebView inside Minecraft's Activity. Unlike WebScreen, this prototype opens a visible browser panel in the game window, not a 3D world texture.

## Implementation
- The Standalone levipack includes browser.dex compiled from original Java code in standalone_browser/src/com/reaaurelis/levimusic/standalone/NativeBrowser.java.
- The native mod loads this bundled DEX with Android DexClassLoader and invokes its open function when WEB is tapped. A Mod Menu toggle named Experimental embedded WebView defaults OFF.
- Java finds the current Android Activity through ActivityThread reflection, attaches a visible WebView panel and starts https://m.youtube.com.
- Users directly navigate, select video and press play. Closing the panel destroys the WebView and stops its playback.
- No companion APK and no copied WebScreen binaries, code, or resources.

## Limitations
- Requires Android System WebView and compatible hidden ActivityThread reflection. Android 16 and launcher variations may block this; testing is required.
- It is a VISIBLE browser player. YouTube's rules prohibit hiding the video to extract audio or circumvent background playback restrictions. DRM, site login and media playback may fail.
- Browser tracks are not automatically registered with the native LeviMusic seek/search/lyrics queue. The normal native Standalone music catalog remains Audius until a compatible, allowed browser media bridge is developed.
- The browser UI panel is NOT projected into the Minecraft 3D world, and no world renderer from WebScreen or LHolo is included.
- Never enable both LeviMusic standalone and Bridge native modules together.
