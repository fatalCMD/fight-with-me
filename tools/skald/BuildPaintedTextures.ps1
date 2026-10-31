# Convert the unmodified generated artwork into Skyrim-ready, mipmapped BC3 textures.
$ErrorActionPreference = 'Stop'
$skaldRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$skaldTextures = Join-Path $skaldRoot 'assets\textures\FightWithMe\Skald'
foreach ($skaldName in @('Fervor','Echoes','Hollow','Glory')) {
    $skaldSource = Join-Path $skaldTextures ('Skald' + $skaldName + 'Atlas.png')
    & texconv -nologo -f BC3_UNORM -w 1024 -h 1024 -m 0 -y -o $skaldTextures $skaldSource
    if ($LASTEXITCODE -ne 0) { throw "Texture conversion failed: $skaldSource" }
}
