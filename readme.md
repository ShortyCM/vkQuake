# 🌋 vkQuake
[![Windows CI](https://github.com/Novum/vkQuake/actions/workflows/build-windows.yml/badge.svg)](https://github.com/Novum/vkQuake/actions/workflows/build-windows.yml) [![Windows CI](https://github.com/Novum/vkQuake/actions/workflows/build-mingw.yml/badge.svg)](https://github.com/Novum/vkQuake/actions/workflows/build-mingw.yml) [![Windows CI](https://github.com/Novum/vkQuake/actions/workflows/build-msys2-clangarm64.yml/badge.svg)](https://github.com/Novum/vkQuake/actions/workflows/build-msys2-clangarm64.yml) [![Linux CI](https://github.com/Novum/vkQuake/actions/workflows/build-linux.yml/badge.svg)](https://github.com/Novum/vkQuake/actions/workflows/build-linux.yml) [![macOS CI](https://github.com/Novum/vkQuake/actions/workflows/build-mac.yml/badge.svg)](https://github.com/Novum/vkQuake/actions/workflows/build-mac.yml) [![Formatting](https://github.com/Novum/vkQuake/actions/workflows/clang-format-check.yml/badge.svg)](https://github.com/Novum/vkQuake/actions/workflows/clang-format-check.yml)

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

## 3dfx RGSS antialiasing

In **Options > Graphics > AA mode**, select **3dfx 2x RGSS**, **3dfx 4x RGSS**, or **3dfx 8x RGSS**. The existing Multisample and Supersample modes remain available. The Antialiasing row cycles through 2x, 4x, and 8x when a 3dfx mode is selected.

These modes render the complete frame two, four, or eight times into single-sample buffers, including the weapon, particles, transparency, HUD, console, and menus. Each render uses a different subpixel offset from the VSA-100's [2x/4x sample patterns](https://sources.debian.org/src/glide/2002.04.10ds1-11/glide3x/h5/incsrc/h3defs.h) or Glide's [four-chip 8x sample table](https://sources.debian.org/src/glide/2002.04.10ds1-16/glide3x/h5/glide3/src/gpci.c), expressed relative to the pixel center. The resulting pixels receive equal weights, with display gamma and contrast applied after averaging. The emulation covers the spatial sampling method; the original hardware's parallel execution, color precision, and dithering are outside its scope.

Simulation and animation advance once per displayed frame. Hardware MSAA and sample shading are disabled in these modes. GPU rendering work increases because each sample rerenders the frame. At reduced render resolutions, jitter remains relative to output pixels and the HUD stays at output resolution; use matching render and output resolutions for native-resolution RGSS.

Console equivalents are `vid_fsaamode 2` (2x), `vid_fsaamode 3` (4x), and `vid_fsaamode 4` (8x). Modes `0` and `1` restore Multisample and Supersample using the saved `vid_fsaa` setting. Changes apply immediately. RGSS requires a Vulkan device supporting at least six bound descriptor sets.

For Windows runtime validation, build Debug and run `powershell -ExecutionPolicy Bypass -File tests/rgss-smoke.ps1 -GameDirectory "C:\path\to\Quake"` with `VULKAN_SDK` set. The test copies game data into an isolated directory under `build`, checks twenty rendering configurations with Vulkan synchronization validation, and saves screenshots for visual review.

## Steam Audio

In **Options > Sound**, the bottom **Renderer** row selects **Stereo** or **Steam Audio HRTF**. Stereo remains the default and uses the original mixer. Changes apply immediately. Steam Audio produces binaural stereo for headphones using the native SDK's built-in HRTF; no SOFA file, Steam client, or map authoring tool is required. If the native library cannot be initialized, the engine reports the reason and returns to Stereo.

World sounds follow their emitting entities and the listener's position and orientation. The renderer applies HRTF directionality, volumetric occlusion, frequency-dependent transmission and air absorption, convolution reflections, listener reverberation, and baked pathing with runtime visibility validation and alternate paths. Brush geometry follows moving doors, lifts, rotating brushes, and entity scale, including the last known transforms of brushes outside the visible set. Local player/menu sounds and BSP ambient loops remain centered; music remains stereo. The existing underwater effect and volume controls still apply.

Existing BSP maps and mods need no changes. Acoustic triangles come from their loaded world and brush models. Texture names supply approximate masonry, metal, wood, glass, earth, and fence materials; legacy maps contain no measured acoustic materials. Sky and liquid surfaces are excluded from the acoustic mesh. Probe placement samples open BSP leaves, preserves separated rooms, and increases spacing on large maps to fit a 2,048-probe budget. Extremely fragmented maps use a deterministic sample across the entire map. Pathing is an approximation based on these probes, with a 100-metre maximum baked path length. Dynamic models made of alias, MD3, or MD5 triangles are sound emitters but do not obstruct sound.

The first visit builds the scene and bakes pathing in the background. Direct sound and real-time reflections work while pathing prepares. The engine caches the scene and baked probes under the active game's writable `steamaudio` directory, such as `id1/steamaudio` or `hipnotic/steamaudio`. Content hashes, SDK/schema versions, and checksums invalidate changed or damaged data automatically. Mod data stays in its own write directory. A read-only cache directory still permits acoustic simulation for the current session. Map changes and renderer switches cancel outstanding work and release its resources.

These archived console settings control quality and cost:

| Setting | Default | Meaning |
| --- | --- | --- |
| `snd_renderer` | `0` | `0` = Stereo; `1` = Steam Audio HRTF |
| `snd_steam_occlusion` | `1` | Geometry occlusion |
| `snd_steam_transmission` | `1` | Transmission through occluders; requires occlusion |
| `snd_steam_reflections` | `1` | Individual source reflections |
| `snd_steam_reverb` | `1` | Listener reverberation for local sounds and remaining voices |
| `snd_steam_pathing` | `1` | Baked pathing with dynamic validation |
| `snd_steam_reflection_gain` | `0.7` | Individual reflected sound gain, 0–2 |
| `snd_steam_reverb_gain` | `0.25` | Listener reverb gain, 0–2 |
| `snd_steam_path_gain` | `0.7` | Pathing gain, 0–2 |
| `snd_steam_sources` | `16` | Loudest voices with individual reflections/pathing, 1–32; changing this restarts the renderer |
| `snd_steam_rays` | `2048` | Reflection rays per update, 256–8192 |
| `snd_steam_bounces` | `16` | Reflection bounces, 1–64 |
| `snd_steam_update_hz` | `20` | Target simulation update rate, 5–60; CPU cost can reduce the achieved rate |
| `snd_steam_probe_spacing` | `128` | Starting probe spacing in Quake units, 64–512; applies on the next map load/rebuild |
| `snd_steam_cache` | `1` | Read/write acoustic caches; applies on the next map load/rebuild |

All audible world voices receive HRTF and direct simulation. A bounded pool handles individual indirect sound; other voices feed listener reverb. DSP uses 512-sample blocks and first-order Ambisonics with two-second convolution responses. Quake's direct distance attenuation is preserved, while indirect sound uses the SDK's inverse-distance model. Lower `snd_steam_sources`, `snd_steam_rays`, or `snd_steam_bounces` to reduce CPU use. `snd_steam_info` reports the renderer, geometry, probe/cache status, and simulation counters. `snd_steam_rebuild` regenerates the current map's acoustic data.

### Native SDK and builds

`ThirdParty/SteamAudio` contains the official 4.8.1 API headers, licenses, Windows x64 runtime/import library, and build provenance. The runtime was compiled from the existing Valve source checkout with its built-in HRTF, CPU ray tracer, PFFFT convolution, and libmysofa. IPP, Embree, Radeon Rays, and TrueAudio Next are disabled. Visual Studio, MinGW x64, and Windows x64 Meson builds copy `phonon.dll` and its license notices alongside the executable; Windows installers and archives include them.

Rebuild the runtime from an existing SDK source checkout with Python 3, Git, CMake, and Visual Studio installed:

```powershell
python Misc/build-steam-audio.py C:/users/Clay/source/repos/steam-audio --cmake "C:/path/to/cmake.exe"
```

The default generator is Visual Studio 2026; pass `--generator "Visual Studio 17 2022"` when building with VS 2022. The script stages source under `build/steam-audio`, uses Valve's pinned dependency manifest to fetch missing build dependencies, and leaves the original checkout untouched. It applies `Misc/steam-audio-pffft-alignment.patch`, a one-line fix for an aligned SIMD load in Steam Audio 4.8.1's unaligned convolution branch. Without that fix, this non-IPP build can crash on odd Ambisonic channels. The patch is needed for non-IPP builds on other platforms as well.

Linux/macOS builds use the same renderer source and dynamically load `libphonon.so` / `libphonon.dylib` from beside the executable or the system library path. Supply a matching native SDK build using `meson setup build -Dsteam_audio_library=/path/to/libphonon.so` or `make STEAMAUDIO_LIBRARY=/path/to/libphonon.so` (substitute `.dylib` on macOS). Windows ARM64 Make builds accept `STEAMAUDIO_LIBRARY=/path/to/arm64/phonon.dll`; a Windows ARM64 SDK runtime is not bundled. Stereo remains available on platforms without a matching SDK library.

Implementation references: Valve's [native API guide](https://valvesoftware.github.io/steam-audio/doc/capi/guide.html), [simulation API](https://valvesoftware.github.io/steam-audio/doc/capi/simulation.html), and [baking API](https://valvesoftware.github.io/steam-audio/doc/capi/baking.html).

### Audio validation

Build and run `tests/steam_audio_native.vcxproj` in Release/x64 to exercise the actual native SDK at 44.1 and 48 kHz with validation enabled: horizontal/front/back/elevation HRTF responses, occlusion/transmission, a moving wall, convolution tails, listener reverb, serialized pathing, path DSP, and dynamic path validation.

After building vkQuake, run `powershell -ExecutionPolicy Bypass -File tests/steam-audio-smoke.ps1 -GameDirectory "C:/path/to/Quake"`. The test copies owned game data into a private directory under `build`, plays demo maps, changes renderer and map, verifies simulation and cache reuse, and saves menu screenshots plus `audio.wav`. The silent SDL disk capture is checked for finite, nonzero stereo output. Use `-Executable` to select another build, `-Mod hipnotic` or `-Mod rogue` for installed mission packs, or `-RealAudio` for playback through the configured device. `-WithoutSDK` checks fallback using an isolated executable directory. Reuse a previous test's `-Artifacts` directory with `-CorruptCache` to damage its test cache deliberately and verify regeneration. Windows CI also runs the native SDK regression tests without game assets.

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

[FreeBSD](https://freebsd.org) includes vkQuake in the standard port/package repositories since version [11.3](https://www.freebsd.org/releases/11.3R/announce).

If you're running `FreeBSD 11.3` or greater, you can install the package with:

```console
# pkg install vkquake
```

Alternatively, you can build vkQuake with FreeBSD's port collection:
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
vkQuake wouldn't even start on an big-endian system, outputting a fatal error.

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
> Meson is the preferred way to build vkQuake because it automatically checks for out of date file dependencies, is faster and has better error reporting for missing dependencies.

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

`vkQuake` is not guaranteed to be free of runtime errors. In those rare cases, the game is either quit brutally with a `Quake Error` dialog, 
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
