## Latest build

**shadPS4 COD** (emulator `bbeb74bc`) and **shadPS4QtLauncher** — fork by vsajjadx

### Emulator
- **Endless hang in time conversion fixed** — They Always Run (#13), Desperados III (#14) and Rustler (#19) froze because local time and UTC conversion never agreed on daylight saving time.
- **DLSS5 removed.** It cut the frame rate to a third (NHL 24: 149 fps off, 50 on) and caused
  blur, ghosting and flicker, so it is gone, including its key `2` and its settings.
- **Giana Sisters: Twisted Dreams** no longer crashes loading the first level (geometry shader divide by zero, issue #12).
- **Synced with the official shadPS4** (upstream main 4788f0b1, 56 commits) while keeping every fix of this fork.
- **NHL 24 — flickering lights, lines on the fog and GPU hang fixed again.** Dynamic resolution keeps one image per surface, also when the mip count differs, and a resized surface now starts cleared instead of from stale memory (that memory held huge HDR values that bloom turned into lights and lines).
- **Shader compile stutter reduced.** A draw whose pipeline is still compiling is skipped for a frame instead of stopping the game; cached shaders are compiled at boot.
- **EA SPORTS FC 27 — freeze fixed.** A workgroup barrier at the head of a loop that threads leave at different times deadlocked the GPU on NVIDIA (shader 0x5e4f7d97).
- **Call of Duty: Black Ops Cold War** — its own section; the video decoder fix applies only to that game.
- **Hotkey 8** (dump the render stages of the frame on screen) no longer hangs, and also saves the textures each pass samples.
- **Window title:** game – PKG / Extracted – build name – version.
- Better GPU hang reports (the hung command is logged).

### Launcher
- Game list: name on the first line, then the serial, region flag and game version in one cell.
- Title shows only the launcher name and version.
- About: links to the author's GitHub and Reddit, and this Recent section.

### Hotkeys
- **0** slow motion · **9** freeze frame · **8** dump render stages to `user/screenshots/stages_<time>`
