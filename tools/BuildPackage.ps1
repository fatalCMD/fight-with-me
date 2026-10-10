<#
.SYNOPSIS
	Builds the mod-manager package for Fight With Me.

.DESCRIPTION
	One SKSE plugin and its INI: no ESP, no scripts, no meshes or textures. The
	archive holds SKSE/Plugins/FightWithMe.dll and FightWithMe.ini, with the README
	and third-party notices beside them, and lands in package/.

	CMake is taken from -CMake, then PATH, then the newest Visual Studio install.
	vcpkg comes from -VcpkgRoot or the VCPKG_ROOT environment variable.

.EXAMPLE
	.\tools\BuildPackage.ps1 -VcpkgRoot C:\vcpkg
	.\tools\BuildPackage.ps1 -SkipBuild
#>
[CmdletBinding()]
param(
	[string]$Version = '0.1.0',
	[string]$VcpkgRoot = $env:VCPKG_ROOT,
	[string]$CMake = '',
	[switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$packageRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'package'))
$releaseName = "FightWithMe-$Version"
$releaseRoot = Join-Path $packageRoot $releaseName
$archivePath = Join-Path $packageRoot "$releaseName.zip"
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
	# A package is only made from a build whose rules still pass.
	& $tests | Out-Null
	if ($LASTEXITCODE -ne 0) { throw 'Tests failed; not packaging.' }
}

if (-not (Test-Path -LiteralPath $builtDll -PathType Leaf)) {
	throw "The built plugin is missing: $builtDll"
}

New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null
Remove-PackageTarget $releaseRoot
Remove-PackageTarget $archivePath

$pluginDir = Join-Path $releaseRoot 'SKSE\Plugins'
New-Item -ItemType Directory -Force -Path $pluginDir | Out-Null
Copy-Item -LiteralPath $builtDll -Destination $pluginDir -Force
Copy-Item -LiteralPath (Join-Path $repoRoot 'config\FightWithMe.ini') -Destination $pluginDir -Force
Copy-Item -LiteralPath (Join-Path $repoRoot 'README.md') -Destination $releaseRoot -Force
Copy-Item -LiteralPath (Join-Path $repoRoot 'THIRD-PARTY-NOTICES.md') -Destination $releaseRoot -Force

# Entries are written one by one with forward slashes. Compress-Archive and
# ZipFile::CreateFromDirectory on Windows PowerShell 5.1 write backslashes, which
# some mod managers read as one flat file name instead of a folder tree.
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$archive = [IO.Compression.ZipFile]::Open($archivePath, [IO.Compression.ZipArchiveMode]::Create)
try {
	$prefixLength = $releaseRoot.TrimEnd('\').Length + 1
	foreach ($file in Get-ChildItem -LiteralPath $releaseRoot -Recurse -File) {
		$relative = $file.FullName.Substring($prefixLength).Replace('\', '/')
		[void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
			$archive, $file.FullName, $relative, [IO.Compression.CompressionLevel]::Optimal)
	}
}
finally { $archive.Dispose() }

# Fail loudly rather than ship an archive that installs wrong.
$verify = [IO.Compression.ZipFile]::OpenRead($archivePath)
try {
	$names = @($verify.Entries | ForEach-Object { $_.FullName })
	if (@($names | Where-Object { $_ -like '*\*' }).Count -gt 0) { throw 'Archive has entries with backslash separators.' }
	foreach ($required in 'SKSE/Plugins/FightWithMe.dll', 'SKSE/Plugins/FightWithMe.ini') {
		if ($names -notcontains $required) { throw "Archive is missing $required." }
	}
}
finally { $verify.Dispose() }

Write-Output "Package : $releaseRoot"
Write-Output "Archive : $archivePath"
Write-Output ("Plugin  : {0:N0} bytes" -f (Get-Item $builtDll).Length)
Write-Output ("Archive : {0:N0} bytes" -f (Get-Item $archivePath).Length)
