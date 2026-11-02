#!/usr/bin/env python3
"""Build Valve's native SDK from an existing source checkout (Windows x64).

Uses the SDK's pinned dependency manifest and dependency builder. All generated
files, including the SDK's plugin-copy post-build steps, stay under build/.
No SDK source or prebuilt SDK is downloaded. Only missing third-party
dependencies are fetched by Valve's build script.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


def run(*args, **kwargs):
    subprocess.run([str(x) for x in args], check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="Existing ValveSoftware/steam-audio checkout")
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--generator", default="Visual Studio 18 2026")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parent.parent
    source = args.source.resolve()
    core = repo / "build/steam-audio/source/core"
    out = repo / "ThirdParty/SteamAudio"
    if not (source / "core/src/core/phonon.h").is_file():
        parser.error("source must contain core/src/core/phonon.h")
    # The SDK has post-build steps that copy into its sibling plugin folders.
    # Stage the user's source locally so these cannot modify their checkout.
    for directory in ("src", "build", "data", "git"):
        shutil.copytree(source / "core" / directory, core / directory,
                        dirs_exist_ok=True, ignore=shutil.ignore_patterns("__pycache__"))
    for filename in ("CMakeLists.txt", "README.md", "THIRDPARTY.md"):
        shutil.copy2(source / "core" / filename, core / filename)
    # 4.8.1's non-IPP convolution has an aligned load in its unaligned SIMD
    # branch. A 512-sample FFT has 513 complex bins, so odd Ambisonic channels
    # reliably trigger an access violation. Keep the one-line fix reviewable.
    git = Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "Git/cmd/git.exe"
    if not git.is_file():
        git = shutil.which("git") or "git"
    run(git, "apply", "--unsafe-paths", "--directory=" + str(core), repo / "Misc/steam-audio-pffft-alignment.patch", cwd=repo)
    # Speaker gains must interpolate over samples, not output channels.
    run(git, "apply", "--unsafe-paths", "--directory=" + str(core), repo / "Misc/steam-audio-panning.patch", cwd=repo)
    manifest_path = core / "build/dependencies.json"
    manifest = json.loads(manifest_path.read_text())
    manifest = {k: manifest[k] for k in ("flatbuffers", "pffft", "zlib", "mysofa")}
    for dep in manifest.values():
        dep.setdefault("configure", {}).setdefault("cmake", []).append({
            "flags": ["-DCMAKE_POLICY_VERSION_MINIMUM=3.15"]})
    manifest_path.write_text(json.dumps(manifest, indent=2))
    builder = core / "build/get_dependencies.py"
    text = builder.read_text()
    text = text.replace("return 'Visual Studio 17 2022'", "return " + repr(args.generator))
    builder.write_text(text)
    env = os.environ.copy()
    env["PATH"] = str(Path(args.cmake).resolve().parent) + os.pathsep + env["PATH"]
    git_dir = Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "Git/cmd"
    if git_dir.is_dir():
        env["PATH"] = str(git_dir) + os.pathsep + env["PATH"]
    import sys
    run(sys.executable, builder, "--platform", "windows", "--architecture", "x64",
        "--toolchain", "vs2022", cwd=core / "build", env=env)
    native = repo / "build/steam-audio/native"
    options = ["-D" + name + "=OFF" for name in (
        "STEAMAUDIO_BUILD_TESTS", "STEAMAUDIO_BUILD_ITESTS", "STEAMAUDIO_BUILD_BENCHMARKS",
        "STEAMAUDIO_BUILD_SAMPLES", "STEAMAUDIO_BUILD_DOCS", "STEAMAUDIO_ENABLE_IPP",
        "STEAMAUDIO_ENABLE_EMBREE", "STEAMAUDIO_ENABLE_RADEONRAYS", "STEAMAUDIO_ENABLE_TRUEAUDIONEXT")]
    run(args.cmake, "-S", core, "-B", native, "-G", args.generator, "-A", "x64", *options, env=env)
    run(args.cmake, "--build", native, "--config", "Release", "--target", "phonon", "--parallel", env=env)
    (out / "lib/windows-x64").mkdir(parents=True, exist_ok=True)
    (out / "include").mkdir(parents=True, exist_ok=True)
    for filename in ("phonon.dll", "phonon.lib"):
        shutil.copy2(native / "src/core/Release" / filename, out / "lib/windows-x64" / filename)
    shutil.copy2(core / "src/core/phonon.h", out / "include/phonon.h")
    shutil.copy2(native / "src/core/phonon_version.h", out / "include/phonon_version.h")
    shutil.copy2(source / "LICENSE.md", out / "LICENSE.md")
    shutil.copy2(source / "core/THIRDPARTY.md", out / "THIRDPARTY.md")
    revision = subprocess.run([str(git), "-c", "safe.directory=" + str(source), "-C", str(source), "rev-parse", "HEAD"],
                              capture_output=True, text=True)
    digest = hashlib.sha256((out / "lib/windows-x64/phonon.dll").read_bytes()).hexdigest()
    (out / "BUILDINFO.txt").write_text(
        "Steam Audio native SDK, Windows x64 Release\n"
        "Source: https://github.com/ValveSoftware/steam-audio\n"
        "Revision: " + (revision.stdout.strip() if revision.returncode == 0 else "local source (git revision unavailable)") + "\n"
        "Build: Misc/build-steam-audio.py\n"
        "Local fix: Misc/steam-audio-pffft-alignment.patch\n"
        "Local fix: Misc/steam-audio-panning.patch\n"
        "Features: built-in HRTF, default CPU ray tracer, PFFFT convolution, libmysofa\n"
        "Disabled: IPP, Embree, Radeon Rays, TrueAudio Next, samples, tests, documentation\n"
        "phonon.dll SHA256: " + digest + "\n", encoding="utf-8")
    print("Steam Audio runtime built from", source, "and installed to", out)


if __name__ == "__main__":
    main()
