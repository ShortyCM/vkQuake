# vkQuake-OpenXR

Play Quake in a PC VR headset using the usual **mouse and keyboard**, with stereoscopic rendering and head tracking. Mouse movement still aims and steers the game; turning or leaning your head changes your view without changing where the weapon fires. The HUD and crosshair stay together on mouse aim. VR controllers are not used.

This is an experimental **Windows x64, OpenXR-only** build. It requires a connected headset and has no desktop-only renderer. Use regular vkQuake when playing on a monitor. Tested with an HP Reverb G2, Oasis, and SteamVR; other compatible PC OpenXR headsets have not been verified. Smooth locomotion and mouse turning can still be uncomfortable in VR; stop if you feel unwell.

## What you need

- A Windows x64 PC with a Vulkan-capable GPU and its current graphics driver.
- A PC VR headset with a working Windows OpenXR runtime that supports Vulkan. Start the headset's PC connection software and select that runtime as the active OpenXR runtime. A standalone headset must be connected to the PC; this is not an Android/Quest application.
- Quake game data: a base folder containing `id1\pak0.pak`, plus `id1\pak1.pak` for the full game. Game data is not included.
- The vkQuake-OpenXR Windows ZIP, extracted to a writable folder with **all supplied DLLs beside `vkQuake-OpenXR.exe`**. End users do not need the Vulkan or OpenXR development SDKs.
- The Microsoft Visual C++ x64 runtime if Windows reports a missing `VCRUNTIME140` or `MSVCP140` DLL. Use Microsoft's [supported Visual C++ Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist).

For a Reverb G2 with Oasis, start SteamVR and select SteamVR as the OpenXR runtime. Optionally enable **SteamVR Settings > Windows Mixed Reality > Use Proximity Sensor (Presence)**, then restart SteamVR. This enables detection when you put the headset on; it is **not required** for the startup recenter prompt.

## Install and start

1. Extract the Windows ZIP into your Quake folder, beside the `id1` directory. Keep the executable and all supplied DLLs together.
2. Connect the headset and start its OpenXR runtime (SteamVR for the tested G2/Oasis setup).
3. Double-click **vkQuake-OpenXR.exe**. No command-line options are required when you launch from the Quake folder.
4. Keep the desktop game window focused for mouse and keyboard input, then complete the recenter routine below.

SteamVR's startup dashboard is dismissed automatically. Other runtimes may require you to dismiss their system UI yourself.

### Optional: run from a separate folder

If you prefer to keep the engine outside the Quake installation, use `-basedir` to point it at the folder containing `id1`:

```bat
vkQuake-OpenXR.exe -basedir "C:\Games\Steam\steamapps\common\Quake"
```

Other optional arguments are `-multiuser` to store configuration and saves in your user preferences folder, and `-window -width 1280 -height 720` to select a windowed desktop mirror. These are preferences, not requirements for OpenXR rendering.

## Recenter before playing

The prompt appears at every startup, even when the headset has no presence detection:

1. Put the headset on, sit in your actual playing position, and look straight ahead.
2. At **Press Mouse1 to recenter**, click the **left mouse button**. The game captures your current position and heading.
3. Check the result. At **Mouse1: confirm  Mouse2: retry**, click **left** to accept, or **right** to try again.
4. Retry returns to the first prompt. Adjust your position, click left to recenter again, then confirm when satisfied.

The recentering clicks do not fire weapons. Normal mouse controls resume after confirmation.

To recenter later, open the Quake console and run `xr_recenter`. For a convenient keyboard shortcut, enter:

```text
bind F11 xr_recenter
```

F11 then recenters and opens the same confirm/retry prompt. If the runtime supports headset-presence detection, putting the headset on again also starts the routine.

## Controls and display

Use the usual Quake mouse and keyboard controls. Mouse movement aims the weapon; head movement changes your view independently. The HUD and crosshair stay together on mouse aim. Menus and console are flat, and the desktop shows a letterboxed right-eye mirror.

If the SteamVR dashboard is opened again, `xr_hide_dashboard` in the Quake console requests dismissal. `xr_auto_hide_dashboard 0` disables the automatic startup request. Dashboard dismissal uses SteamVR's installed `vrcmd` utility and is unavailable on other runtimes.

| Setting | Default | Effect |
| --- | --- | --- |
| `xr_units_per_meter` | `30.621172` | World scale: 56 Quake units equal 6 feet. Larger values make the world appear smaller relative to eye spacing. |
| `xr_hud_distance` | `2` | Reference distance in meters for HUD angular size. |
| `xr_hud_width` | `1.6` | HUD width at the reference distance; height follows the eye image aspect ratio. |
| `xr_auto_hide_dashboard` | `1` | Dismiss SteamVR's startup dashboard once. |

These settings are saved. `r_width` / `r_height` can reduce scene resolution while retaining native-resolution UI. SteamVR's per-application resolution setting can change the eye resolution at the next launch. Normal zoom commands and underwater effects remain available.

## Compatibility and troubleshooting

The renderer uses runtime-reported eye geometry, resolution, field of view, and tracking; it contains no G2-specific optical constants. It requires `XR_KHR_vulkan_enable2`, two-eye `PRIMARY_STEREO` rendering with opaque composition, and an RGBA8/BGRA8 UNORM or sRGB swapchain format. Headset-worn detection additionally requires optional `XR_EXT_user_presence` support and a working sensor signal. The startup prompt and manual recentering work without it.

- **OpenXR initialization fails:** connect the headset, start its PC runtime, and check which runtime is selected as active. SteamVR is the tested runtime, not a requirement for all headsets.
- **View or weapon height feels wrong:** sit in position and run the manual recenter routine again.
- **Mouse does nothing:** focus the desktop game window. During recentering, finish the confirm/retry prompt before expecting normal button bindings.
- **Quake data cannot be found:** check `-basedir` and `id1\pak0.pak`.
- **Putting the headset on is not detected:** use the startup prompt or `xr_recenter`; for Oasis, enable the optional proximity setting and restart SteamVR.

Other platform build systems still build upstream vkQuake and have not been converted to OpenXR. There is no standalone headset build in this fork.

## Build from source (Windows x64)

Install Visual Studio 2026 with the v145 C++ build tools, a Windows SDK, CMake, the Vulkan SDK, and the [Khronos OpenXR SDK](https://github.com/KhronosGroup/OpenXR-SDK). The tested OpenXR SDK is release 1.1.63. The OpenXR loader is linked statically; no separate loader DLL is distributed.

Place `OpenXR-SDK` beside this repository, then build its static loader from a developer command prompt:

```bat
cmake -S ..\OpenXR-SDK -B ..\OpenXR-SDK\build -G "Visual Studio 18 2026" -A x64 -DDYNAMIC_LOADER=OFF
cmake --build ..\OpenXR-SDK\build --config Release --target openxr_loader
build.cmd
```

The game and companion DLLs are written to `Windows\VisualStudio\Build-vkQuake-OpenXR\x64\Release`. `build.cmd` defaults to Release/x64 and locates MSBuild through Visual Studio's installer.

Defaults are Vulkan SDK `C:\VulkanSDK\1.4.363.0` and the sibling `..\OpenXR-SDK`. Override `VULKAN_SDK` in the environment or pass `/p:VULKAN_SDK=...`, `/p:OpenXRSDK=...`, or `/p:OpenXRLoaderLibrary=...` to MSBuild through `build.cmd`. Debug builds require the matching Debug loader (`build\src\loader\Debug\openxr_loaderd.lib`); build it with `--config Debug` first.

## Developer checks

Build `tests/openxr_display_test.vcxproj` as Release/x64 with MSBuild and run `build\tests\openxr_display_test.exe`. It tests the production backend with a deterministic OpenXR frame source: swapchain formats, asymmetric projection, scale, live eye separation, head/mouse composition, recenter/confirm/retry with and without presence support, captured clicks and releases, invalid tracking, menus without a level, desktop-resize isolation, HUD projection, dashboard dismissal, and balanced frame/image/session handling. It does not test GPU rendering or replace headset checks.

The G2/Oasis/SteamVR setup was tested on an RTX 3080 Ti at 2216 x 2168 pixels per eye using sRGB eye images. Image placement, tracked viewing, and manual recenter/confirm/retry were checked in the headset.

Custom CSQC HUD compatibility depends on the mod.

The upstream vkQuake documentation follows. Other platform build systems have not been converted to OpenXR.

---

vkQuake is a port of id Software's [Quake](https://en.wikipedia.org/wiki/Quake_(video_game)) using Vulkan instead of OpenGL for rendering. It is based on the popular [QuakeSpasm](http://quakespasm.sourceforge.net/) and [QuakeSpasm-Spiked](https://triptohell.info/moodles/qss/) ports and runs all mods compatible with QuakeSpasm like [Arcane Dimensions](http://www.moddb.com/mods/arcane-dimensions).

Improvements over QuakeSpasm include:
* Much better performance with multithreaded rendering and loading
* The game can run at higher frame rates than 72Hz without breaking physics
* A software Quake like underwater effect
* Support for Classic (original Quake) and Enhanced models (either MD5 format like the 2021 re-Release, or MD3 like Quake3)
* When both Classic and Enhanced versions of models exist, display one or other kind using the Models menu option.
* True color skins support (`.png`, `.tga`, `.jpg`) for MD3/MD5 models, including fullbrights (from QSS)
* Dynamic shadows (requires a GPU with ray tracing support)
* Dynamic lights, using 2021 rerelease `dynamiclight` entities (requires a GPU with ray tracing support and "Dynamic Shadows" set to medium or higher)
* Better color precision reducing banding in dark areas
* Native support for anti aliasing and anisotropic filtering
* 8-bit color emulation
* Scaling for pixelated look
* Mods menu for easy mod loading
* More modern protocol to avoid certain movement issues (from QSS)
* Support for custom mod HUDs (from QSS)
* Support for scriptable particles (from QSS)
* True color support for level textures and static models (`.png`, `.tga`, `.jpg`)
* External WAD support ([more info](https://github.com/Novum/vkQuake/pull/753))
* WAD3 format support, allowing per-texture palettes for more colorful levels ([more info](https://github.com/Novum/vkQuake/pull/753))

## Installation

Windows and Linux binaries can be found in [Releases](https://github.com/Novum/vkQuake/releases).
MacOS (both Apple Silicon and 64-bit Intel) binaries are at [Mac Source Ports](https://www.macsourceports.com/game/quake).

### Windows
It is recommended to use the installer on Windows. This sets up start menu entries for the classic and remastered Quake versions. The engine finds Steam, GOG and Epic Games Store installs automatically; otherwise it asks for the game folder once and remembers it. Save data and config files will be written to the user folder (`%APPDATA%\vkQuake`) instead of the Quake data folder.

Otherwise copy all files inside the `vkquake-<version>_windows_x64.zip` (Intel) or `vkquake-<version>_windows_arm64.zip` (Arm64) folder in the zip to the Quake base directory. Overwrite any existing files. Afterward to run the game just execute `vkQuake.exe`.

### Linux
Download the Linux AppImage from [Releases](https://github.com/Novum/vkQuake/releases). Make it executable and run it:

```sh
chmod +x vkQuake-*.AppImage
./vkQuake-*.AppImage
```

> **Note**\
> Make sure all data files are lowercase, e.g. "id1", not "ID1" and "pak0.pak", not "PAK0.PAK". Some distributions of the game have upper case file names, e.g. from GOG.com.

### OpenBSD

[OpenBSD](https://openbsd.org) includes vkQuake in the standard package repositories since version [6.6](https://www.openbsd.org/66.html).

If you're running `OpenBSD 6.6` or greater, you can install the package with:

```console
$ pkg_add vkquake
```

### FreeBSD

[FreeBSD](https://freebsd.org) includes vkQuake in the standard port/package repoistories since version [11.3](https://www.freebsd.org/releases/11.3R/announce).

If you're running `FreeBSD 11.3` or greater, you can install the package with:

```console
# pkg install vkquake
```

Alternatvely, you can build vkQuake with FreeBSD's port collection:
```console
$ cd /usr/ports/games/vkquake
# make install
```

### Quake '2021 re-release'

vkQuake has support for playing the 2021 re-release content. Follow installation instructions as above but copy the files into the rerelease folder.

## Vulkan
vkQuake shows basic usage of the API. For example it demonstrates render passes & sub passes, pipeline barriers & synchronization, compute shaders, push & specialization constants, CPU/GPU parallelism and memory pooling.

## Endianness
vkQuake only supports **little-endian** systems. 
The reason is, all known existing Vulkan-capable systems in the wild are little-endian. Consequently, all big-endian support from QuakeSpasm has been effectively removed. 
vkQuake wouldn't even start on an big-endian system, outputing a fatal error.

## Building
> **Note**\
> For Windows, you will need at least Vulkan SDK version 1.4.321.1 or newer.
> 
> For Linux, you will need at least Vulkan SDK version 1.2.162 or newer. When building for Linux this is not always the case for the SDK provided by the distribution. Install the latest LunarG SDK if necessary.

### Windows

Clone the vkQuake repo from `https://github.com/Novum/vkQuake.git`

Prerequisites:

* [Git for Windows](https://github.com/git-for-windows/git/releases)
* A [Vulkan-capable GPU](https://en.wikipedia.org/wiki/Vulkan_(API)#Compatibility) with the appropriate drivers installed
* Install the latest [Vulkan SDK](https://vulkan.lunarg.com/sdk/home). Log out and back in after installation to make sure environment variables are set.

#### Visual Studio

* Install [Visual Studio Community](https://www.visualstudio.com/products/free-developer-offers-vs) with Visual C++ component.

Open the Visual Studio solution, `Windows\VisualStudio\vkquake.sln`, select the desired configuration and platform, then
build the solution.

#### MinGW

Setup your [MinGW-w64](https://sourceforge.net/projects/mingw-w64/) environment, e.g. using [w64devkit](https://github.com/skeeto/w64devkit) or [MSYS2](https://www.msys2.org/).


Build 64 bit Intel vkQuake:

~~~
cd vkQuake/Quake
make -f Makefile.w64
~~~

Build 64 bit Arm vkQuake:

~~~
cd vkQuake/Quake
make -f Makefile.w64a
~~~

If you are on Linux and want to cross-compile for Windows, see the `build_cross_win??.sh` scripts.

#### Meson

With [Meson](https://mesonbuild.com/), [Ninja](https://ninja-build.org/) and a compiler installed (LLVM/Clang on PATH, or run `meson setup --vsenv build` for MSVC):

~~~
cd vkQuake
meson setup build && ninja -C build
~~~

### Linux

Make sure that both your GPU and your GPU driver support [Vulkan](https://en.wikipedia.org/wiki/Vulkan#Support_across_vendors).

To compile vkQuake, first install the build dependencies:

Ubuntu:
~~~
apt-get install git meson gcc glslang-tools spirv-tools libsdl3-dev libvulkan-dev libvorbis-dev libmpg123-dev libx11-xcb-dev
~~~

Arch Linux:
~~~
pacman -S git meson flac glibc libgl mpg123 libvorbis libx11 sdl3 vulkan-headers glslang spirv-tools
~~~

Fedora:
~~~
dnf install git meson gcc glslang spirv-tools vulkan-loader-devel SDL3-devel mpg123-devel libvorbis-devel flac-devel opusfile-devel
~~~

On distributions that do not ship SDL3 yet, install the SDL2 development package instead (e.g. `libsdl2-dev`); the build falls back to SDL2 automatically.

Then clone the vkQuake repo:

~~~
git clone https://github.com/Novum/vkQuake.git
~~~

Now go to the Quake directory and compile the executable:

~~~
cd vkQuake
meson build -Ddebug=true -Dstrip=false && ninja -C build
~~~

Meson prefers SDL3 and falls back to SDL2 if it is not installed; add `-Duse_sdl3=disabled` to force SDL2 (or `enabled` to require SDL3).

On macOS the build links MoltenVK directly. Add `-Duse_vulkan_loader=enabled` to link the Vulkan loader instead, so the driver can be chosen with `VK_DRIVER_FILES`.

> **Note**\
> The Meson version needs to be 1.3.0 or newer. For older distributions you can use make:
> ~~~
> cd vkQuake/Quake
> make -j
> ~~~
> Meson is the preferred way to build vkQuake because it automatically checks for out of date file depenencies, is faster and has better error reporting for missing dependencies.

> **Note**\
> vkQuake requires **SDL3** or, as a fallback for older distributions, at least **SDL2 2.0.6 with enabled Vulkan support**.

### MacOS

To compile vkQuake, first install the build dependencies with Homebrew:

~~~
brew install molten-vk vulkan-headers glslang spirv-tools sdl3 libvorbis flac opus opusfile flac mpg123 meson pkgconfig
~~~

Then clone the vkQuake repo:

~~~
git clone https://github.com/Novum/vkQuake.git
~~~

Now go to the Quake directory and compile the executable:

~~~
cd vkQuake
meson build -Ddebug=true -Dstrip=false && ninja -C build
~~~

Meson prefers SDL3 and falls back to SDL2 if it is not installed; add `-Duse_sdl3=disabled` to force SDL2 (or `enabled` to require SDL3).

> **Note**\
> The Meson version needs to be 1.3.0 or newer.

## Error reporting

`vkQuake` is not garanteed to be free of runtime errors. In those rare cases, the game is either quit brutally with a `Quake Error` dialog, 
or by an `Host_Error` console message that only terminates the currently played level. 

In both cases some error context is provided that can be useful for developers. See [Error reporting](error_reporting_howto.md) for details.      

## Optional - Music / Soundtrack

> **Note**\
> This section only applies to older releases. For the 2021 re-release music will work out of the box.

The original Quake had a great soundtrack by Nine Inch Nails. Unfortunately, the Steam version does not come with the soundtrack files. The GOG-provided files need to be converted before they are ready for use. In general, you'll just need to move a "music" folder to the correct location within your vkQuake installation (.e.g `/usr/share/quake/id1/music`). Most Quake engines play nicest with soundtracks placed in the `id1/music` subfolder vs. `sound\cdtracks`

QuakeSpasm, the engine vkQuake is derived from, supports OGG, MP3, FLAC, and WAV audio formats. The Linux version of QuakeSpasm/VkQuake requires external libraries: libogg or libvorbis for OGG support, libmad or libmpg123 for MP3, and libflac for FLAC. If you already have a setup that works for the engine you're currently using, then you don't necessarily have to change it. 

Generally, the below setup works for multiple engines, including Quakespasm/vkQuake:

* The music files are loose files, NOT inside a pak or pk3 archive.
* The files are placed inside a "music" subfolder of the "id1" folder. For missionpack or mod soundtracks, the files are placed in a "music" subfolder of the appropriate game folder. So the original Quake soundtrack files go inside "id1\music", Mission Pack 1 soundtrack files go inside "hipnotic\music", and Mission Pack 2 soundtrack files go inside "rogue\music".
* The files are named in the pattern "tracknn", where "nn" is the CD track number that the file was ripped from. Since the soundtrack starts at the second CD track, MP3 soundtrack files are named "track02.mp3", "track03.mp3", etc. OGG soundtrack files are named "track02.ogg", "track03.ogg", etc. FLAC soundtrack files are named "track02.flac", "track03.flac", etc. WAV soundtrack files are named "track02.wav", "track03.wav", etc.

**See more:** [Quake Soundtrack Solutions (Steam Community)](http://steamcommunity.com/sharedfiles/filedetails/?id=119489135)
