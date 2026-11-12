<!--
SPDX-FileCopyrightText: 2025 shadPS4 Emulator Project
SPDX-License-Identifier: GPL-2.0-or-later
-->

<h1 align="center">
  <br>
  <img src="https://github.com/shadps4-emu/shadPS4-qtlauncher/blob/main/.github/shadps4.png" width="220">
  <br>
  <b>QtLauncher — shadPS4 Anti-Crash</b>
  <br>
</h1>

<p align="center">
The launcher of the <a href="https://github.com/vsajjadx/shadPS4"><b>shadPS4 Anti-Crash</b></a> fork:
the official shadPS4 QtLauncher with additions for this fork.
</p>

# Download

You don't need to build anything. The launcher comes **together with the emulator** in one zip:
download the latest one from the emulator's [**Releases**](https://github.com/vsajjadx/shadPS4/releases/latest),
unzip it and run `shadPS4QtLauncher.exe`.

# What's different from the official launcher

- **Game list:** each game shows its name, and under it the serial, region flag and game version.
- **About → Recent:** always describes the latest build, with this fork's changes and fixes.
- **Automatic updates** from this fork's releases. Your saves and settings are never touched.
- **Direct PKG execution:** point it at a folder with the base game, update and DLC PKGs and launch.
- **Frame generation switch** in **Settings → Graphics**.
- **Easy window size:** pick 720p, 1080p, 1440p, 4K and more from a list.
- **Compatibility list:** the official list plus the games this build runs that the official one doesn't.
- **Clean title:** just the launcher name and version.

The branch with these changes is [`fork`](https://github.com/vsajjadx/shadps4-qtlauncher/tree/fork).

# Author

- GitHub: [vsajjadx](https://github.com/vsajjadx)
- Reddit: [u/Unhappy_Advantage_10](https://www.reddit.com/user/Unhappy_Advantage_10/)

Made with the help of **Claude**, the AI assistant by Anthropic.

Based on the official [shadPS4 QtLauncher](https://github.com/shadps4-emu/shadPS4-qtlauncher).

# Building

> [!IMPORTANT]
> If you want to use this launcher, you don't have to follow the build instructions, you can simply download the launcher from the [**release tab**](https://github.com/shadps4-emu/shadPS4-qtlauncher/releases).

## Windows

Check the build instructions for [**Windows**](https://github.com/shadps4-emu/shadPS4-qtlauncher/blob/main/documents/building-windows.md).

## Linux

Check the build instructions for [**Linux**](https://github.com/shadps4-emu/shadPS4-qtlauncher/blob/main/documents/building-linux.md).

## macOS

Check the build instructions for [**macOS**](https://github.com/shadps4-emu/shadPS4-qtlauncher/blob/main/documents/building-macos.md).

# Keyboard and Mouse Mappings

> [!NOTE]
> Some keyboards may also require you to hold the Fn key to use the F\* keys. Mac users should use the Command key instead of Control, and need to use Command+F11 for full screen to avoid conflicting with system key bindings.

| Button | Function |
|-------------|-------------|
F10 | FPS Counter
Ctrl+F10 | Video Debug Info
F11 | Fullscreen
F12 | Trigger RenderDoc Capture

> [!NOTE]
> Xbox and DualShock controllers work out of the box.

| Controller button | Keyboard equivalent |
|-------------|-------------|
LEFT AXIS UP | W |
LEFT AXIS DOWN | S |
LEFT AXIS LEFT | A |
LEFT AXIS RIGHT | D |
RIGHT AXIS UP | I |
RIGHT AXIS DOWN | K |
RIGHT AXIS LEFT | J |
RIGHT AXIS RIGHT | L |
TRIANGLE | Numpad 8 or C |
CIRCLE | Numpad 6 or B |
CROSS | Numpad 2 or N |
SQUARE | Numpad 4 or V |
PAD UP | UP |
PAD DOWN | DOWN |
PAD LEFT | LEFT |
PAD RIGHT | RIGHT |
OPTIONS | RETURN |
BACK BUTTON / TOUCH PAD | SPACE |
L1 | Q |
R1 | U |
L2 | E |
R2 | O |
L3 | X |
R3 | M |

Keyboard and mouse inputs can be customized in the settings menu by clicking the Controller button, and further details and help on controls are  also found there. Custom bindings are saved per-game. Inputs support up to three keys per binding, mouse buttons, mouse movement mapped to joystick input, and more.

Logo is done by [**Xphalnos**](https://github.com/Xphalnos)

# Contributing

If you want to contribute, please read the [**CONTRIBUTING.md**](https://github.com/shadps4-emu/shadPS4-qtlauncher/blob/main/CONTRIBUTING.md) file.\
Open a PR and we'll check it :)

# Translations

If you want to translate shadPS4 to your language we use [**Crowdin**](https://crowdin.com/project/shadps4-emulator).
# Contributors

<a href="https://github.com/shadps4-emu/shadPS4-qtlauncher/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=shadps4-emu/shadPS4-qtlauncher&max=24">
</a>

# License

- [**GPL-2.0 license**](https://github.com/shadps4-emu/shadPS4-qtlauncher/blob/main/LICENSE)
