<# Installs a matched local build from the three package folders, with backups and hash checks.
   Existing *_user.ini files are never replaced. Does not enable or change MO2 profiles. #>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ModsDirectory,
    [string]$CoreVersion='0.5.0',
    [string]$PathsVersion='1.0.0',
    [string]$EffectsVersion='1.0.20'
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$mods=[IO.Path]::GetFullPath($ModsDirectory).TrimEnd('\')
if(-not (Test-Path -LiteralPath $mods -PathType Container)){throw 'Mods directory does not exist'}
foreach($v in @($CoreVersion,$PathsVersion,$EffectsVersion)){if($v -notmatch '^\d+\.\d+\.\d+$'){throw 'Invalid version'}}
if(Get-Process -Name SkyrimSE,SkyrimVR -ErrorAction SilentlyContinue){throw 'Close Skyrim before installing its plugins.'}
$sources=@(
    @("FightWithMe-$CoreVersion",'Fight With Me'),
    @("Path of the Companions-$PathsVersion",'Fight With Me - Paths'),
    @("POTC Effects-$EffectsVersion",'Fight With Me')
)
$items=@()
foreach($pair in $sources){
    $root=Join-Path $repo "package\$($pair[0])"
    if(-not (Test-Path -LiteralPath $root -PathType Container)){throw "Missing package $root"}
    foreach($file in Get-ChildItem -LiteralPath $root -Recurse -File){
        if($file.Name -like '*_user.ini'){throw 'A package contains personal settings'}
        $relative=$file.FullName.Substring($root.Length+1)
        $destination=[IO.Path]::GetFullPath((Join-Path $mods "$($pair[1])\$relative"))
        $allowed=Join-Path $mods $pair[1]
        if(-not $destination.StartsWith($allowed+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unexpected destination'}
        $hash=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        if((Test-Path -LiteralPath $destination) -and (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -eq $hash){continue}
        $items += [pscustomobject]@{source=$file.FullName;destination=$destination;sha256=$hash}
    }
}
# Replacing only DLLs was the original regression: configurations and trait data are part of this transaction.
$required=@('Fight With Me\SKSE\Plugins\FightWithMe.dll','Fight With Me\SKSE\Plugins\FightWithMe.ini',
    'Fight With Me - Paths\SKSE\Plugins\FightWithMePaths.dll','Fight With Me - Paths\SKSE\Plugins\FightWithMePaths.ini',
    'Fight With Me - Paths\SKSE\Plugins\FightWithMe\Traits\Followers.ini')
$backup=Join-Path $repo ('build\deployment-backups\split-audit-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
foreach($item in $items){
    if(Test-Path -LiteralPath $item.destination){
        $target=Join-Path $backup $item.destination.Substring($mods.Length+1)
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath $item.destination -Destination $target
    }
}
$manifest=[ordered]@{status='staged';createdAt=Get-Date -Format o;backup=$backup;files=$items}
$manifestPath=Join-Path $repo 'build\split-audit-deployment.json'
$manifest|ConvertTo-Json -Depth 6|Set-Content -LiteralPath $manifestPath -Encoding UTF8
if(Get-Process -Name SkyrimSE,SkyrimVR -ErrorAction SilentlyContinue){throw 'Skyrim started; packages remain staged.'}
foreach($item in $items){
    if((Get-FileHash -LiteralPath $item.source -Algorithm SHA256).Hash -ne $item.sha256){throw 'Package changed during installation'}
    New-Item -ItemType Directory -Path (Split-Path -Parent $item.destination) -Force | Out-Null
    Copy-Item -LiteralPath $item.source -Destination $item.destination -Force
    if((Get-FileHash -LiteralPath $item.destination -Algorithm SHA256).Hash -ne $item.sha256){throw 'Installed hash mismatch'}
}
foreach($relative in $required){if(-not (Test-Path -LiteralPath (Join-Path $mods $relative))){throw "Missing installed file $relative"}}
$manifest.status='installed_verified';$manifest.installedAt=Get-Date -Format o
$manifest|ConvertTo-Json -Depth 6|Set-Content -LiteralPath $manifestPath -Encoding UTF8
Write-Output "Installed and verified $($items.Count) changed files. Backup: $backup"
