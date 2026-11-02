# Exercise the native Steam Audio renderer against owned game data.
# Uses a private game directory and SDL's disk audio device for silent capture.
param(
    [string]$GameDirectory = 'C:/Games/Steam/steamapps/common/Quake',
    [string]$Executable,
    [string]$Artifacts,
    [ValidateSet('id1', 'hipnotic', 'rogue')][string]$Mod = 'id1',
    [ValidateRange(1, 5)][int]$Renderer = 1,
    [switch]$LFEOnly,
    [switch]$CycleModes,
    [switch]$WithoutSDK,
    [switch]$CorruptCache,
    [switch]$RealAudio
)
$ErrorActionPreference = 'Stop'
if ($LFEOnly -and ($Renderer -lt 4 -or $WithoutSDK -or $CycleModes)) { throw 'LFEOnly requires Renderer 4 or 5 with the SDK and without CycleModes' }
$repo = Split-Path -Parent $PSScriptRoot
if (!$Executable) { $Executable = Join-Path $repo 'Windows/VisualStudio/Build-vkQuake/x64/Debug/vkQuake.exe' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if (!$Artifacts) { $Artifacts = Join-Path $repo ('build/steam-audio-smoke-' + [DateTime]::Now.ToString('yyyyMMdd-HHmmss')) }
$data = Join-Path $Artifacts $Mod
foreach ($directory in @('id1', $Mod) | Select-Object -Unique) {
    $destination = Join-Path $Artifacts $directory
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    foreach ($pak in @('pak0.pak', 'pak1.pak')) {
        $source = Join-Path $GameDirectory ($directory + '/' + $pak)
        $target = Join-Path $destination $pak
        if ((Test-Path -LiteralPath $source) -and !(Test-Path -LiteralPath $target)) { Copy-Item -LiteralPath $source -Destination $target }
    }
}
if ($WithoutSDK) {
    $binaryDirectory = Join-Path $Artifacts 'bin-without-sdk'
    New-Item -ItemType Directory -Path $binaryDirectory -Force | Out-Null
    Copy-Item -LiteralPath $Executable -Destination $binaryDirectory -Force
    Get-ChildItem -LiteralPath (Split-Path -Parent $Executable) -Filter '*.dll' |
        Where-Object Name -NE 'phonon.dll' | Copy-Item -Destination $binaryDirectory -Force
    $Executable = Join-Path $binaryDirectory (Split-Path -Leaf $Executable)
}
if ($CorruptCache) {
    $cacheFiles = @(Get-ChildItem -LiteralPath (Join-Path $data 'steamaudio') -File |
        Where-Object { $_.Extension -in @('.scene', '.probes') })
    if (!$cacheFiles.Count) { throw 'CorruptCache requires an existing smoke cache in Artifacts' }
    foreach ($file in $cacheFiles) {
        $bytes = [IO.File]::ReadAllBytes($file.FullName)
        $bytes[$bytes.Length-1] = $bytes[$bytes.Length-1] -bxor 1
        [IO.File]::WriteAllBytes($file.FullName, $bytes)
    }
}
@('vid_fsaamode 0', 'vid_fsaa 0', 'r_ssao 0', 'r_oit 0', 'vid_fullscreen 0', "snd_renderer $Renderer", 'cl_confirmquit 0', 'cl_startdemos 0') |
    Set-Content -LiteralPath (Join-Path $Artifacts 'vkQuake.cfg') -Encoding ASCII
$commands = [System.Collections.Generic.List[string]]::new()
$demo1 = if ($Mod -eq 'hipnotic') { 'hipdemo1' } else { 'demo1' }
$demo2 = if ($Mod -eq 'hipnotic') { 'hipdemo2' } else { 'demo2' }
$commands.AddRange([string[]]@('vid_unlock', 'vid_vsync 0', 'host_maxfps 60', 'bgmvolume 0', 'cl_startdemos 0', "snd_renderer $Renderer", "playdemo $demo1"))
1..60 | ForEach-Object { $commands.Add('wait') }
$commands.AddRange([string[]]@('menu_sound', 'wait', 'screenshot png', 'wait', 'wait', 'snd_renderer 0'))
1..10 | ForEach-Object { $commands.Add('wait') }
$commands.AddRange([string[]]@('screenshot png', 'wait', 'wait', 'snd_steam_info', "snd_renderer $Renderer", 'wait', 'togglemenu', "playdemo $demo1"))
if ($CycleModes) {
    foreach ($mode in @(1, 2, 3, 4, 5, 4, 3, 2, 1, 0, $Renderer)) {
        $commands.Add("snd_renderer $mode")
        1..20 | ForEach-Object { $commands.Add('wait') }
        $commands.Add('snd_steam_info')
    }
}
foreach ($i in 1..6) {
    1..60 | ForEach-Object { $commands.Add('wait') }
    $commands.Add('snd_steam_info')
}
$commands.Add("playdemo $demo2")
1..180 | ForEach-Object { $commands.Add('wait') }
$commands.AddRange([string[]]@('snd_steam_info', "playdemo $demo1"))
1..180 | ForEach-Object { $commands.Add('wait') }
$commands.AddRange([string[]]@('snd_steam_info', 'disconnect', 'echo STEAM_AUDIO_SMOKE_DONE', 'toggleconsole', 'quit'))
if ($LFEOnly) {
    $commands.Clear()
    $commands.AddRange([string[]]@('vid_unlock', 'vid_vsync 0', 'host_maxfps 60', 'bgmvolume 0', 'cl_startdemos 0', "snd_renderer $Renderer", "playdemo $demo1"))
    1..60 | ForEach-Object { $commands.Add('wait') }
    $commands.AddRange([string[]]@('snd_steam_info', 'menu_sound', 'wait', 'screenshot png', 'wait', 'togglemenu'))
    foreach ($frequency in @(60, 70, 80, 90, 100, 110, 120)) {
        $commands.Add("snd_steam_crossover $frequency")
        1..20 | ForEach-Object { $commands.Add('wait') }
        $commands.Add('snd_steam_info')
    }
    $commands.Add('snd_steam_crossover 80')
    1..160 | ForEach-Object { $commands.Add('wait') }
    $commands.AddRange([string[]]@('snd_steam_info', 'disconnect', 'echo STEAM_AUDIO_SMOKE_DONE', 'quit'))
}
$commands | Set-Content -LiteralPath (Join-Path $data 'steam-audio-smoke.cfg') -Encoding ASCII
$stdout = Join-Path $Artifacts 'stdout.log'
$stderr = Join-Path $Artifacts 'stderr.log'
$previousDriver = $env:SDL_AUDIO_DRIVER
$previousOutput = $env:SDL_AUDIO_DISK_OUTPUT_FILE
$rawAudio = Join-Path $Artifacts 'audio.raw'
try {
    if (!$RealAudio) {
        $env:SDL_AUDIO_DRIVER = 'disk'
        $env:SDL_AUDIO_DISK_OUTPUT_FILE = $rawAudio
    }
    $process = Start-Process -FilePath $Executable -WorkingDirectory $Artifacts -WindowStyle Hidden -PassThru -ArgumentList @(
        '-basedir', '.', '-game', $Mod, '-window', '-width', '800', '-height', '600', '-condebug', '+exec', 'steam-audio-smoke.cfg'
    ) -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    [void]$process.Handle
    Write-Output "Steam Audio smoke PID $($process.Id); artifacts: $Artifacts"
    $deadline = [DateTime]::UtcNow.AddMinutes(3)
    while (!$process.WaitForExit(1000)) {
        if ([DateTime]::UtcNow -gt $deadline) { $process.Kill(); throw "Smoke timed out; see $Artifacts" }
    }
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw "Game exited $($process.ExitCode); see $Artifacts" }
} finally {
    $env:SDL_AUDIO_DRIVER = $previousDriver
    $env:SDL_AUDIO_DISK_OUTPUT_FILE = $previousOutput
}
$log = (Get-Content -LiteralPath $stdout -Raw) + (Get-Content -LiteralPath $stderr -Raw)
if ($log -notmatch 'STEAM_AUDIO_SMOKE_DONE') { throw 'Smoke did not finish' }
if ($WithoutSDK) {
    if ($log -notmatch 'runtime unavailable' -or $log -notmatch 'Audio renderer: Standard stereo speakers' -or $log -match 'built-in HRTF') {
        throw 'Missing SDK did not fall back to Standard stereo speakers'
    }
} else {
    $modeNames = @('Standard stereo speakers', 'Steam Audio HRTF Headphones', 'Steam Audio Stereo Speakers', 'Steam Audio 4.0 Speakers', 'Steam Audio 5.1 Speakers', 'Steam Audio 7.1 Speakers')
    foreach ($mode in $(if ($CycleModes) { 0..5 } else { @($Renderer) })) {
        if ($log -notmatch [regex]::Escape('Audio renderer: ' + $modeNames[$mode])) { throw "Renderer $mode did not initialize" }
    }
    if (($Renderer -eq 1 -or $CycleModes) -and $log -notmatch 'built-in HRTF') { throw 'Native HRTF was not initialized' }
    if (!$LFEOnly) {
        if ($log -notmatch 'Simulation updates: direct [1-9]\d*, reflections [1-9]\d*, pathing [1-9]\d*') { throw 'Acoustic simulation did not complete' }
        if ($log -notmatch 'cache loaded') { throw 'Cache was not reused' }
        if ($CorruptCache -and $log -notmatch 'generating pathing cache') { throw 'Damaged cache was not regenerated' }
    } else {
        foreach ($frequency in @(60, 70, 80, 90, 100, 110, 120)) {
            if ($log -notmatch "Bass crossover: $frequency Hz, 24 dB/octave Linkwitz-Riley") { throw "Crossover $frequency Hz was not applied" }
        }
    }
    if ($log -match 'initialization failed|simulation; returning|runtime unavailable') { throw 'Native renderer reported an error' }
}
if ($log -match 'Assertion|Sys_Error|Unknown command') { throw 'Game reported an error' }
if (!$RealAudio) {
    $channelCount = if ($WithoutSDK) { 2 } else { @(2, 2, 2, 4, 6, 8)[$Renderer] }
    $formats = [regex]::Matches($log, 'format=(F32LE|S16LE) channels=(\d+) freq=(\d+)')
    if (!$formats.Count) { throw 'Disk audio format was not reported as PCM' }
    $format = $formats[$formats.Count - 1]
    if ([int]$format.Groups[2].Value -ne $channelCount) { throw "Disk device did not open $channelCount channels" }
    Add-Type -TypeDefinition @'
using System;
using System.IO;
public static class SteamAudioCapture {
    public static string Check(string raw, string wav, bool floating, int rate, int channels) {
        byte[] data = File.ReadAllBytes(raw);
        int width = floating ? 4 : 2;
        double energy = 0, difference = 0, peak = 0;
        double[] channelEnergy = new double[channels];
        for (int i = 0; i + channels*width <= data.Length; i += channels*width) {
            double left = 0;
            for (int channel = 0; channel < channels; channel++) {
                int offset = i + channel*width;
                double value = floating ? BitConverter.ToSingle(data,offset) : BitConverter.ToInt16(data,offset)/32768.0;
                if (Double.IsNaN(value) || Double.IsInfinity(value)) throw new Exception("Nonfinite audio output");
                energy += value*value;
                channelEnergy[channel] += value*value;
                peak = Math.Max(peak,Math.Abs(value));
                if (channel == 0) left = value;
                if (channel == 1) difference += (left-value)*(left-value);
            }
        }
        if (data.Length < rate*channels*width || data.Length % (channels*width) != 0 || energy < 1 || difference < 0.01 || peak > 1)
            throw new Exception("Captured audio is empty, silent, mono or outside PCM range");
        for (int channel = 0; channel < channels; channel++)
            if (channelEnergy[channel] < 0.001)
                throw new Exception("Silent speaker channel " + channel);
        using (BinaryWriter output = new BinaryWriter(File.Create(wav))) {
            output.Write(System.Text.Encoding.ASCII.GetBytes("RIFF")); output.Write(data.Length+60);
            output.Write(System.Text.Encoding.ASCII.GetBytes("WAVEfmt ")); output.Write(40);
            output.Write((short)-2); output.Write((short)channels); output.Write(rate);
            output.Write(rate*channels*width); output.Write((short)(channels*width)); output.Write((short)(width*8));
            output.Write((short)22); output.Write((short)(width*8));
            output.Write(channels == 8 ? 0x63f : channels == 6 ? 0x3f : channels == 4 ? 0x33 : 3);
            output.Write(new Guid(floating ? "00000003-0000-0010-8000-00aa00389b71" : "00000001-0000-0010-8000-00aa00389b71").ToByteArray());
            output.Write(System.Text.Encoding.ASCII.GetBytes("data")); output.Write(data.Length); output.Write(data);
        }
        return String.Format("PCM passed: {0} channels, {1:F1}s, peak {2:F3}, channel energies [{3}]",
            channels,(double)data.Length/(rate*channels*width),peak,String.Join(", ",channelEnergy));
    }
}
'@
    [SteamAudioCapture]::Check($rawAudio, (Join-Path $Artifacts 'audio.wav'), $format.Groups[1].Value -eq 'F32LE', [int]$format.Groups[3].Value, $channelCount)
}
Write-Output "Steam Audio smoke passed: $Artifacts"
