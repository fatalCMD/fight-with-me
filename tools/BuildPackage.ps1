<#
.SYNOPSIS
	Builds the mod-manager package for Fight With Me.

.DESCRIPTION
	Builds two required, independently versioned downloads: the main plugin/UI/config
	and POTC Effects (meshes/textures). Neither includes debug symbols.

	CMake is taken from -CMake, then PATH, then the newest Visual Studio install.
	vcpkg comes from -VcpkgRoot or the VCPKG_ROOT environment variable.

.EXAMPLE
	.\tools\BuildPackage.ps1 -VcpkgRoot C:\vcpkg
	.\tools\BuildPackage.ps1 -SkipBuild
#>
[CmdletBinding()]
param(
	[string]$Version = '0.5.0',
    [string]$EffectsVersion = '1.0.21',
	[string]$VcpkgRoot = $env:VCPKG_ROOT,
	[string]$CMake = '',
	[switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
foreach ($label in @($Version, $EffectsVersion)) {
    if ($label -notmatch '^[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?$') { throw "Invalid package version: $label" }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$packageRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'package'))
$releaseName = "FightWithMe-$Version"
$releaseRoot = Join-Path $packageRoot $releaseName
$archivePath = Join-Path $packageRoot "$releaseName.zip"
$effectsName = "POTC Effects-$EffectsVersion"
$effectsRoot = Join-Path $packageRoot $effectsName
$effectsArchive = Join-Path $packageRoot "$effectsName.zip"
$builtDll = Join-Path $repoRoot 'build\skyrim\Release\FightWithMe.dll'
$tests = Join-Path $repoRoot 'build\skyrim\Release\FightWithMeTests.exe'

function Remove-PackageTarget([string]$Path) {
	$full = [IO.Path]::GetFullPath($Path)
	$prefix = $packageRoot.TrimEnd('\') + '\'
	if (-not $full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
		throw "Refusing to remove a path outside the package folder: $full"
	}
	if (Test-Path -LiteralPath $full) {
		Remove-Item -LiteralPath $full -Recurse -Force
	}
}

function Find-CMake {
	if ($CMake) { return $CMake }
	$onPath = Get-Command cmake -ErrorAction SilentlyContinue
	if ($onPath) { return $onPath.Source }
	$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
	if (Test-Path -LiteralPath $vswhere) {
		$found = & $vswhere -latest -products * -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' | Select-Object -First 1
		if ($found) { return $found }
	}
	return $null
}

if (-not $SkipBuild) {
	$CMake = Find-CMake
	if (-not $CMake -or -not (Test-Path -LiteralPath $CMake -PathType Leaf)) {
		throw 'CMake was not found. Pass -CMake, put cmake on PATH, or install Visual Studio with its CMake tools.'
	}
	if (-not $VcpkgRoot) {
		throw 'vcpkg was not found. Pass -VcpkgRoot or set VCPKG_ROOT.'
	}
	$env:VCPKG_ROOT = $VcpkgRoot
	Push-Location $repoRoot
	# CMake and vcpkg write progress to stderr. Under Stop, Windows PowerShell turns that
	# into a failure when the caller redirects it; the exit codes decide instead.
	$strict = $ErrorActionPreference
	$ErrorActionPreference = 'Continue'
	try {
		& $CMake --preset skyrim | Out-Null
		if ($LASTEXITCODE -ne 0) { throw 'Configuration failed.' }
		& $CMake --build 'build/skyrim' --config Release --parallel 8 | Out-Null
		if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
	}
	finally {
		$ErrorActionPreference = $strict
		Pop-Location
	}

}

if (-not (Test-Path -LiteralPath $builtDll -PathType Leaf)) {
	throw "The built plugin is missing: $builtDll"
}
$CMake = Find-CMake
$ctest = Join-Path (Split-Path -Parent $CMake) 'ctest.exe'
& $ctest --test-dir (Join-Path $repoRoot 'build/skyrim') -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed; not packaging.' }
$actualVersion = (Get-Item -LiteralPath $builtDll).VersionInfo.FileVersion
if ($actualVersion -ne "$Version.0") { throw "Built DLL version $actualVersion does not match package $Version. Rebuild first." }
# The translation template must say what the source says.
& (Join-Path $PSScriptRoot 'Strings.ps1') -Check

New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null
Remove-PackageTarget $releaseRoot
Remove-PackageTarget $archivePath

$pluginDir = Join-Path $releaseRoot 'SKSE\Plugins'
New-Item -ItemType Directory -Force -Path $pluginDir | Out-Null
Copy-Item -LiteralPath $builtDll -Destination $pluginDir -Force
Copy-Item -LiteralPath (Join-Path $repoRoot 'config\FightWithMe.ini') -Destination $pluginDir -Force
# The English template a translation starts from, in the folder translations are read from.
$translationDir = Join-Path $pluginDir 'FightWithMe\Translations'
New-Item -ItemType Directory -Force -Path $translationDir | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot 'config\Translations\FightWithMe_ENGLISH.txt') -Destination $translationDir -Force

# The main download owns the UI, fonts and settings. Effects have their own required archive.
New-Item -ItemType Directory -Force -Path (Join-Path $releaseRoot 'docs') | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot 'docs\RELEASE-NOTES.md') -Destination (Join-Path $releaseRoot 'docs') -Force
$iconDir = Join-Path $releaseRoot 'interface\FightWithMe'
New-Item -ItemType Directory -Force -Path $iconDir | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot 'assets\interface\FightWithMe\glyphs') -Destination $iconDir -Recurse -Force
foreach ($icon in 'wolf-head.svg', 'barbed-sun.svg', 'weak-will.svg', 'last-bastion.svg') {
    Copy-Item -LiteralPath (Join-Path $repoRoot "assets\interface\FightWithMe\$icon") -Destination $iconDir -Force
}
$fontDir = Join-Path $pluginDir 'Fonts'
New-Item -ItemType Directory -Force -Path $fontDir | Out-Null
Get-ChildItem -LiteralPath (Join-Path $repoRoot 'assets\fonts') -Recurse -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $fontDir -Force
}
foreach ($document in 'README.md', 'THIRD-PARTY-NOTICES.md') {
    Copy-Item -LiteralPath (Join-Path $repoRoot $document) -Destination $releaseRoot -Force
}
$licenseDir = Join-Path $releaseRoot 'licenses'
New-Item -ItemType Directory -Force -Path $licenseDir | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot 'extern\CommonLibSSE-NG\COPYING') -Destination (Join-Path $licenseDir 'CommonLibSSE-NG-GPL-3.0.txt') -Force
Copy-Item -LiteralPath (Join-Path $repoRoot 'extern\CommonLibSSE-NG\EXCEPTIONS.md') -Destination (Join-Path $licenseDir 'CommonLibSSE-NG-EXCEPTIONS.md') -Force
Copy-Item -LiteralPath (Join-Path $repoRoot 'extern\CommonLibSSE-NG\licenses\LICENSE-MIT') -Destination (Join-Path $licenseDir 'CommonLibSSE-NG-legacy-MIT.txt') -Force
Copy-Item -LiteralPath (Join-Path $repoRoot 'extern\CommonLibSSE-NG\extern\openvr\LICENSE') -Destination (Join-Path $licenseDir 'OpenVR-LICENSE.txt') -Force
Copy-Item -LiteralPath (Join-Path $repoRoot 'build\skyrim\_deps\hde64-src\LICENSE.txt') -Destination (Join-Path $licenseDir 'MinHook-hde64-LICENSE.txt') -Force
$dependencyNotices = Join-Path $repoRoot 'build\skyrim\vcpkg_installed\x64-windows-static-md\share'
foreach ($dependency in 'directxmath', 'directxtk', 'fmt', 'rapidcsv', 'spdlog') {
	Copy-Item -LiteralPath (Join-Path $dependencyNotices "$dependency\copyright") -Destination (Join-Path $licenseDir "$dependency-copyright.txt") -Force
}

"Matching plugin source: FightWithMe-0.5.0-Paths-1.0.0-Source.zip (separate developer download; not required to play). See BUILD-SOURCE.md inside that archive." |
    Set-Content -LiteralPath (Join-Path $releaseRoot 'SOURCE-INFORMATION.txt') -Encoding UTF8
Remove-PackageTarget $effectsRoot
Remove-PackageTarget $effectsArchive
New-Item -ItemType Directory -Force -Path $effectsRoot | Out-Null
foreach ($folder in 'meshes', 'textures') {
    Copy-Item -LiteralPath (Join-Path $repoRoot "assets\$folder") -Destination $effectsRoot -Recurse -Force
}
# Large generated UI artwork belongs to the separate required art download.
$backdropSource = Join-Path $repoRoot 'assets\interface\FightWithMe\backdrops'
$backdropTarget = Join-Path $effectsRoot 'interface\FightWithMe\backdrops'
New-Item -ItemType Directory -Force -Path $backdropTarget | Out-Null
foreach ($theme in 'vanilla','oxblood','nordic') {
    foreach ($role in 'fighter','tank','healer','bruiser','archer','mage') {
        $art = Join-Path $backdropSource "$theme-$role.png"
        if (-not (Test-Path -LiteralPath $art -PathType Leaf)) { throw "Missing class backdrop: $art" }
        Copy-Item -LiteralPath $art -Destination $backdropTarget -Force
    }
}
Copy-Item -LiteralPath (Join-Path $repoRoot 'THIRD-PARTY-NOTICES.md') -Destination (Join-Path $effectsRoot 'Effects-Credits.md')
@"
POTC Effects $EffectsVersion
Required by Path of the Companions (FightWithMePaths.dll). Battle-orders-only Fight With Me does not require this pack.
Includes the 18 class skill-tree backdrops under interface/FightWithMe/backdrops.
The Effects pack contains only Fight With Me artwork and Skyrim-derived assets for use with Skyrim.
No Triumvirate, Elemental Destruction Magic Redux, Vibrant Weapons or Fluffworks installation is needed.
When updating from an older combined FWM installation, replace the old main package instead of merging
it, or remove its old meshes/FightWithMe and textures/FightWithMe folders after backing them up.
Keep your *_user.ini files and saves. Update shipped INI defaults with their matching plugin packages. This pack adds no gameplay plugin.
"@ | Set-Content -LiteralPath (Join-Path $effectsRoot 'Effects-Readme.txt') -Encoding UTF8
$manifest = @{
    version = $EffectsVersion
    contract = 1
    files = @(Get-ChildItem -LiteralPath $effectsRoot -Recurse -File | ForEach-Object {
        @{ path = $_.FullName.Substring($effectsRoot.Length + 1).Replace('\','/'); bytes = $_.Length;
           sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $effectsRoot 'Effects-Manifest.json') -Encoding UTF8
"Optional: Path of the Companions adds abilities, traits and progression. It requires POTC Effects $EffectsVersion (asset contract 1)." |
    Set-Content -LiteralPath (Join-Path $releaseRoot 'Optional-Paths.txt') -Encoding UTF8

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
function Write-Package([string]$Folder, [string]$ArchivePath) {
    $archive = [IO.Compression.ZipFile]::Open($ArchivePath, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in Get-ChildItem -LiteralPath $Folder -Recurse -File) {
            $relative = $file.FullName.Substring($Folder.Length + 1).Replace('\', '/')
            [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $relative, [IO.Compression.CompressionLevel]::Optimal)
        }
    } finally { $archive.Dispose() }
    $verify = [IO.Compression.ZipFile]::OpenRead($ArchivePath)
    try {
        foreach ($entry in $verify.Entries) {
            if ($entry.FullName.Contains('\') -or $entry.FullName.EndsWith('.pdb')) { throw "Invalid distribution entry: $($entry.FullName)" }
            $original = Join-Path $Folder $entry.FullName
            if ($entry.Length -ne (Get-Item -LiteralPath $original).Length) { throw "Archive size mismatch: $($entry.FullName)" }
        }
    } finally { $verify.Dispose() }
    $bytes = (Get-ChildItem -LiteralPath $Folder -Recurse -File | Measure-Object Length -Sum).Sum
    Write-Output ("{0}: ZIP {1:N0} bytes; unpacked {2:N0} bytes" -f $ArchivePath, (Get-Item -LiteralPath $ArchivePath).Length, $bytes)
}
if (Get-ChildItem -LiteralPath $releaseRoot -Recurse -File | Where-Object { $_.Extension -in '.nif','.dds' }) { throw 'Effect content leaked into the main package.' }
if (Get-ChildItem -LiteralPath $effectsRoot -Recurse -File | Where-Object { $_.Extension -in '.dll','.ini','.pdb','.esp','.esl' }) { throw 'Non-art content leaked into the Effects pack.' }
if (-not (Test-Path -LiteralPath (Join-Path $effectsRoot 'textures\FightWithMe\EffectsPack-1.txt'))) { throw 'Effects pack contract marker is missing.' }
Write-Package $releaseRoot $archivePath
Write-Package $effectsRoot $effectsArchive
