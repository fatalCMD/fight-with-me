<# Stages Skald's runtime assets into the normal Paths package. No separate addon is needed. #>
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Destination)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$groups = @(
    @{ Folder = 'meshes\FightWithMe'; Pattern = 'Skald*.nif'; Count = 48 },  # lutes, pulses, hits and the 36 native Ljodh clouds
    @{ Folder = 'textures\FightWithMe\Skald'; Pattern = 'Skald*Atlas.dds'; Count = 4 },
    @{ Folder = 'sound\fx\FightWithMe\Skald'; Pattern = 'Skald*.wav'; Count = 5 },
    @{ Folder = 'interface\FightWithMe\backdrops'; Pattern = '*-skald.png'; Count = 3 }
)
# Validate the complete source before writing the destination.
$files = @()
foreach ($group in $groups) {
    $found = @(Get-ChildItem -LiteralPath (Join-Path $repo "assets\$($group.Folder)") -Filter $group.Pattern -File)
    if ($found.Count -ne $group.Count) { throw "Incomplete Skald assets: $($group.Folder)" }
    foreach ($file in $found) { $files += @{ Source = $file.FullName; Target = Join-Path $group.Folder $file.Name } }
}
foreach ($document in 'SKALD.md', 'PARTY-BALANCE.md') {
    $source = Join-Path $repo "docs\$document"
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing Skald document: $source" }
    $files += @{ Source = $source; Target = "docs\$document" }
}
foreach ($file in $files) {
    $target = Join-Path $Destination $file.Target
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    Copy-Item -LiteralPath $file.Source -Destination $target -Force
}
Write-Output "Staged $($files.Count) Skald runtime files and documents into Paths."
