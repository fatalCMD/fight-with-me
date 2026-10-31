<#
.SYNOPSIS
	Builds the source download for a Fight With Me / Path of the Companions release.

.DESCRIPTION
	The working source of both DLLs, including untracked files, with the CommonLibSSE-NG,
	OpenVR, MinHook and exact vcpkg library sources they were built from, Skald's runtime
	assets and documents, BUILD-SOURCE.md and SOURCE-MANIFEST.json (every file's hash and
	the built DLLs'). A developer download, never part of a game package.

.EXAMPLE
	.\tools\BuildReleaseSource.ps1 -Output package\FightWithMe-0.6.0-Paths-1.2.0-Source.zip
#>
[CmdletBinding()]
param(
	[Parameter(Mandatory)][string]$Output,
	[string]$Version = '0.6.0',
	[string]$PathsVersion = '1.2.0',
	[string]$EffectsVersion = '1.0.21',
	[string]$VcpkgBuildtrees = 'X:\vcpkg\buildtrees'
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$excluded = '.git', '__pycache__', 'node_modules', 'build', 'package', 'out', '.vs', '.github', '.claude', '.factory'
$text = '.c', '.cpp', '.h', '.hpp', '.in', '.cmake', '.json', '.txt', '.ini', '.ps1', '.py', '.js', '.cjs', '.mjs',
	'.html', '.css', '.md', '.svg', '.natvis', '.inc', '.rc'
$files = @{}  # entry name -> file path or bytes

function Add-File([string]$Path, [string]$Name) {
	if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing source file: $Path" }
	if ($Name.Split('/') -contains '..' -or $Name.StartsWith('/')) { throw "Bad entry name: $Name" }
	if ($files.ContainsKey($Name)) { throw "Duplicate entry: $Name" }
	$files[$Name] = (Resolve-Path -LiteralPath $Path).Path
}
function Add-Tree([string]$Path, [string]$Prefix, [scriptblock]$Keep = { $true }) {
	if (-not (Test-Path -LiteralPath $Path -PathType Container)) { throw "Missing source folder: $Path" }
	$base = (Resolve-Path -LiteralPath $Path).Path.TrimEnd('\')
	foreach ($file in Get-ChildItem -LiteralPath $base -Recurse -File -Force) {
		$relative = $file.FullName.Substring($base.Length + 1)
		if (@($relative.Split('\') | Where-Object { $excluded -contains $_ }).Count -gt 0) { continue }
		if (& $Keep $file) { Add-File $file.FullName ($Prefix + $relative.Replace('\', '/')) }
	}
}
function Get-Hash([byte[]]$Bytes) {
	if ($null -eq $Bytes) { $Bytes = [byte[]]::new(0) }  # an empty file
	$sha = [Security.Cryptography.SHA256]::Create()
	try { return ([BitConverter]::ToString($sha.ComputeHash([byte[]]$Bytes)) -replace '-', '').ToLowerInvariant() }
	finally { $sha.Dispose() }
}
# The comma keeps PowerShell from unrolling the bytes into the pipeline.
function Get-Bytes([string]$Name) {
	$value = $files[$Name]
	if ($value -is [byte[]]) { return , $value }
	return , [IO.File]::ReadAllBytes($value)
}

$sourceText = { param($f) $text -contains $f.Extension.ToLowerInvariant() -and -not $f.Name.EndsWith('_user.ini') }
foreach ($folder in 'src', 'include', 'cmake', 'tests', 'config', 'tools',
	'paths/src', 'paths/include', 'paths/cmake', 'paths/config', 'paths/tools') {
	$path = Join-Path $root $folder
	if (Test-Path -LiteralPath $path -PathType Container) { Add-Tree $path ($folder + '/') $sourceText }
}
foreach ($name in 'CMakeLists.txt', 'CMakePresets.json', 'vcpkg.json', 'vcpkg-configuration.json', 'README.md',
	'THIRD-PARTY-NOTICES.md', 'paths/README.md', 'docs/RELEASE-NOTES.md', 'docs/SKALD.md', 'docs/PARTY-BALANCE.md') {
	Add-File (Join-Path $root $name) $name
}
Add-Tree (Join-Path $root 'assets/fonts') 'assets/fonts/'
Add-Tree (Join-Path $root 'assets/interface/FightWithMe') 'assets/interface/FightWithMe/' { param($f) $f.Extension -in '.svg', '.txt' }
# Skald's runtime assets ship in the Paths download, so they are in its source too.
foreach ($group in @(@('assets/meshes/FightWithMe', 'Skald*.nif'), @('assets/interface/FightWithMe/backdrops', '*-skald.png'),
		@('assets/sound/fx/FightWithMe/Skald', 'Skald*.wav'))) {
	foreach ($asset in Get-ChildItem -LiteralPath (Join-Path $root $group[0]) -Filter $group[1] -File) {
		Add-File $asset.FullName ($group[0] + '/' + $asset.Name)
	}
}
Add-Tree (Join-Path $root 'assets/textures/FightWithMe/Skald') 'assets/textures/FightWithMe/Skald/'

$common = Join-Path $root 'extern/CommonLibSSE-NG'
$commonBase = (Resolve-Path -LiteralPath $common).Path.TrimEnd('\')
Add-Tree $common 'extern/CommonLibSSE-NG/' {
	param($f)
	$parts = $f.FullName.Substring($commonBase.Length + 1).Split('\')
	($parts -notcontains 'extern') -and (($text -contains $f.Extension.ToLowerInvariant()) -or ($parts -contains 'licenses') -or
		($f.Name -in 'COPYING', 'LICENSE', '.clang-format'))
}
$openvr = Join-Path $common 'extern/openvr'
foreach ($folder in 'headers', 'src') { Add-Tree (Join-Path $openvr $folder) "extern/CommonLibSSE-NG/extern/openvr/$folder/" }
foreach ($name in 'CMakeLists.txt', 'LICENSE', 'README.md', 'Toolchain-clang.cmake', 'lib/win64/openvr_api.lib') {
	Add-File (Join-Path $openvr $name) "extern/CommonLibSSE-NG/extern/openvr/$name"
}
Add-Tree (Join-Path $root 'build/skyrim/_deps/hde64-src') 'third-party-sources/minhook/' {
	param($f) ($text -contains $f.Extension.ToLowerInvariant()) -or $f.Name -eq 'LICENSE'
}
$dependencies = [ordered]@{
	'directxmath' = 'jun2026-d9b23f80e1.clean'
	'directxtk'   = 'may2026-a68e548ea6.clean'
	'fmt'         = '12.2.0-d352ede310.clean'
	'rapidcsv'    = 'v8.99-fcbbd6a5ea.clean'
	'spdlog'      = 'v1.17.0-8a1a388e84.clean'
}
foreach ($name in $dependencies.Keys) {
	Add-Tree (Join-Path $VcpkgBuildtrees "$name\src\$($dependencies[$name])") "third-party-sources/$name/" {
		param($f) $f.Extension.ToLowerInvariant() -notin '.exe', '.dll', '.pdb', '.obj', '.lib'
	}
	$share = Join-Path $root "build/skyrim/vcpkg_installed/x64-windows-static-md/share/$name"
	foreach ($file in 'copyright', 'vcpkg.spdx.json') { Add-File (Join-Path $share $file) "third-party-sources/provenance/$name-$file" }
}
Add-File (Join-Path $root 'build/skyrim/vcpkg_installed/vcpkg/status') 'third-party-sources/provenance/vcpkg-status.txt'
Add-File (Join-Path $common 'COPYING') 'COPYING'

$buildSource = @"
# Fight With Me $Version / Path of the Companions $PathsVersion source

This is the current working source, including new untracked files, for both DLLs.
It is a developer download, not a game mod. Large effect models/textures and class
backdrops are supplied separately in POTC Effects-$EffectsVersion.zip. To rebuild
the runtime archives, copy that download's meshes, textures and interface folders
into assets/. Theme fonts, small UI icons and the Skald's meshes, textures, lute
recordings and portraits are already included here.

Use Visual Studio 2022 C++ Build Tools (v143, Windows SDK), CMake, Git and vcpkg.
Set VCPKG_ROOT to your vcpkg checkout. From this extracted directory run:

    cmake --preset skyrim -DFETCHCONTENT_SOURCE_DIR_HDE64=<absolute-path-to>/third-party-sources/minhook
    cmake --build build/skyrim --config Release
    ctest --test-dir build/skyrim -C Release --output-on-failure

Native DLLs are in build/skyrim/Release. Packaging scripts are tools/BuildPackage.ps1
and paths/tools/BuildPathsPackage.ps1. Keep their version defaults together with
CMakeLists.txt; the scripts reject DLLs whose version does not match the archive.
This archive is made by tools/BuildReleaseSource.ps1.

CommonLibSSE-NG 7.2.0, its OpenVR dependency, MinHook and the exact patched vcpkg
library sources are included. vcpkg manifests and provenance preserve versions.
vcpkg still needs its own tool/registry caches or network for normal configuration;
this is not an entirely offline compiler/toolchain distribution.

The plugin code declares GPL-3.0-or-later. See COPYING and the dependency licenses,
CommonLib's exceptions, font licenses and THIRD-PARTY-NOTICES.md. Skyrim-derived
effect assets have separate terms and are intended for use with Skyrim only. The
Skald's lute WAVs are tempo-adjusted Skyrim excerpts for use with Skyrim. To
regenerate the Skald's meshes and timing maps, run tools/skald/research.cjs, then
tools/skald/ArrangeLute.py and tools/skald/BuildSkald.cjs, with the installed stock
game beside this checkout; tools/skald/NativeLjodhs.cjs builds the Ljodh clouds.

SOURCE-MANIFEST.json records every included file and the matching DLL hashes.
Build paths/timestamps/compiler versions can change the resulting binary hashes.
"@ -replace "`r?`n", "`n"
$files['BUILD-SOURCE.md'] = [Text.UTF8Encoding]::new($false).GetBytes($buildSource)

$names = [string[]]@($files.Keys)
[Array]::Sort($names, [StringComparer]::Ordinal)
$hashes = [ordered]@{}
foreach ($name in $names) { $hashes[$name] = Get-Hash (Get-Bytes $name) }
$dlls = [ordered]@{}
foreach ($dll in 'FightWithMe.dll', 'FightWithMePaths.dll') {
	$dlls[$dll] = Get-Hash ([IO.File]::ReadAllBytes((Join-Path $root "build/skyrim/Release/$dll")))
}
$manifest = [ordered]@{
	versions              = [ordered]@{ FightWithMe = $Version; FightWithMePaths = $PathsVersion }
	working_tree_included = $true
	repository_revision   = (& git -C $root rev-parse HEAD).Trim()
	dlls                  = $dlls
	files                 = $hashes
}
$files['SOURCE-MANIFEST.json'] = [Text.UTF8Encoding]::new($false).GetBytes(($manifest | ConvertTo-Json -Depth 5))
$names = [string[]]@($files.Keys)
[Array]::Sort($names, [StringComparer]::Ordinal)

$outputPath = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($Output)) { $Output } else { Join-Path (Get-Location) $Output }))
if (Test-Path -LiteralPath $outputPath) { throw "Refusing to replace an existing archive: $outputPath" }
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outputPath) | Out-Null
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::Open($outputPath, [IO.Compression.ZipArchiveMode]::Create)
try {
	foreach ($name in $names) {
		$bytes = Get-Bytes $name
		$stream = $archive.CreateEntry($name, [IO.Compression.CompressionLevel]::Optimal).Open()
		try { $stream.Write($bytes, 0, $bytes.Length) } finally { $stream.Dispose() }
	}
}
finally { $archive.Dispose() }

# Read back: every entry present, byte for byte.
$verify = [IO.Compression.ZipFile]::OpenRead($outputPath)
try {
	if ($verify.Entries.Count -ne $names.Count) { throw "Archive holds $($verify.Entries.Count) entries, expected $($names.Count)." }
	foreach ($entry in $verify.Entries) {
		$memory = [IO.MemoryStream]::new()
		$stream = $entry.Open()
		try { $stream.CopyTo($memory) } finally { $stream.Dispose() }
		if ((Get-Hash $memory.ToArray()) -ne $(if ($entry.FullName -eq 'SOURCE-MANIFEST.json') { Get-Hash $files['SOURCE-MANIFEST.json'] } else { $hashes[$entry.FullName] })) {
			throw "Archive entry differs from its source: $($entry.FullName)"
		}
	}
}
finally { $verify.Dispose() }
Write-Output ("{0}: {1} files, {2:N0} bytes; verified." -f [IO.Path]::GetFileName($outputPath), $names.Count, (Get-Item -LiteralPath $outputPath).Length)
