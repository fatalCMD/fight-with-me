"""Package a matched Skald update without replacing released archives or settings."""
from pathlib import Path
import argparse
import hashlib
import json
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    files = {}

    def add(source, target):
        assert target not in files and '..' not in Path(target).parts
        files[target] = (ROOT / source).read_bytes()

    dlls = ('FightWithMe.dll', 'FightWithMePaths.dll')
    with zipfile.ZipFile(args.source) as source_zip:
        manifest = json.loads(source_zip.read('SOURCE-MANIFEST.json'))
        assert manifest.get('build', '').startswith('Skald development preview')
        for name in dlls:
            content = (ROOT / 'build/skyrim/Release' / name).read_bytes()
            assert digest(content) == manifest['dlls'][name], 'Source archive does not match ' + name
            files['SKSE/Plugins/' + name] = content

    for folder, pattern in (('meshes/FightWithMe', 'Skald*.nif'),
                             ('interface/FightWithMe/backdrops', '*-skald.png')):
        assets = sorted((ROOT / 'assets' / folder).glob(pattern))
        assert len(assets) == (12 if pattern.endswith('.nif') else 3)
        for asset in assets:
            add(asset, folder + '/' + asset.name)
    atlases=sorted((ROOT/'assets/textures/FightWithMe/Skald').glob('Skald*Atlas.dds'))
    assert len(atlases)==4
    for atlas in atlases:add(atlas,'textures/FightWithMe/Skald/'+atlas.name)
    add('tools/skald/generated-fx-prompts.json','docs/skald-generated-fx-prompts.json')
    for code in range(0xef10,0xef1f):
        target=f'interface/FightWithMe/glyphs/{code:04x}.svg'
        add('assets/'+target,target)
    add('tools/skald/icon-selection.json','docs/skald-icon-selection.json')
    tracks=json.loads((ROOT/'tools/skald/beat-map.json').read_text())
    lute_audio=set()
    for track in tracks:
        target='sound/fx/FightWithMe/Skald/'+track['file']
        add('assets/'+target,target)
        assert digest(files[target])==track['sha256']
        lute_audio.add(target)
    add('tools/skald/arrangements.json','docs/skald-arrangements.json')
    for prefix, name in (('config', 'FightWithMe'), ('paths/config', 'FightWithMePaths')):
        add(prefix + '/Translations/' + name + '_ENGLISH.txt',
            'SKSE/Plugins/FightWithMe/Translations/' + name + '_ENGLISH.txt')
    add('docs/SKALD.md', 'docs/SKALD.md')
    add('docs/PARTY-BALANCE.md', 'docs/PARTY-BALANCE.md')
    add('THIRD-PARTY-NOTICES.md', 'THIRD-PARTY-NOTICES.md')
    add('extern/CommonLibSSE-NG/COPYING', 'COPYING')
    add('extern/CommonLibSSE-NG/EXCEPTIONS.md', 'licenses/CommonLibSSE-NG-EXCEPTIONS.md')
    for folder in ('extern/CommonLibSSE-NG/licenses', 'build/skyrim/vcpkg_installed/x64-windows-static-md/share'):
        for file in sorted((ROOT / folder).rglob('*')):
            if file.is_file() and (file.name == 'copyright' or folder.endswith('licenses')):
                add(file, 'licenses/' + file.relative_to(ROOT / folder).as_posix())
    add('extern/CommonLibSSE-NG/extern/openvr/LICENSE', 'licenses/OpenVR-LICENSE.txt')
    add('build/skyrim/_deps/hde64-src/LICENSE.txt', 'licenses/MinHook-hde64-LICENSE.txt')
    files['README-SKALD.txt'] = f'''Path of the Companions — Skald development preview

Update for an existing POTC installation with POTC Effects 1.0.21.
Install after the existing mods in your mod manager; let this archive win conflicts.
With Fight With Me: use BOTH included DLLs (interface version 6).
Standalone POTC: hide SKSE/Plugins/FightWithMe.dll and keep FightWithMePaths.dll.
Existing INI files and personal settings are not included or replaced.

Give the companion a lute, select Skald and spend 10 points to unlock the tree.
Ground waves grant stacks at 3 / 5 / 10 total hits: 1x / 1.5x / 2x strength.
Up-tempo acoustic lute music drives one wave every two beats; a bright sigil confirms each hit.
The bard's layered rune seal and spectral strings gather and release with those beats.
Four generated, hand-painted texture atlases drive the animated seals, wisps and waves.
All 15 Skald nodes use distinct Game-icons.net artwork by Lorc and Delapouite (CC BY 3.0).
Floating motes surround the Skald; wave hits send glowing sparks up the recipient's body.
Learned rank III adds a wider, denser aura to the Skald and recipients from the first pulse.
Floating sparks travel with each expanding ring without adding extra gameplay hits.
Skald uses Skyrim's rooted lute idle and is interrupted by hits. Outside performances,
the bard holds back and uses only weak fists (1% damage, capped at 1 health per hit).
The existing soundtrack fades to 28% during performance and returns afterward.
Old Tongue rolls independently for each newly earned stack and recipient.
Follower damage is one slider in POTC Settings > Party balance (0–100%).

Optional tavern activity: install Walk-With-Me-Skald-Taverns-v1.zip over Walk With Me.
With Area activities enabled, an unlocked Skald carrying a lute has a 5% chance
at each eligible new Relax activity to play a short tune inside an inn/tavern.
The party must be peaceful and in the same inn; homes and other locations do not qualify.
One performer at a time, with a five-minute shared cooldown. This uses Skyrim's
ordinary lute idle and gives no combat buffs. See docs/SKALD.md for testing notes.

See docs/SKALD.md for all songs, branches, traits, mechanics and known limitations.
This build compiles and passes automated checks. Lute animation, audio and combat
resumption still need verification in Skyrim on the target Nolvus profile.
Original release archives and installed game files have not been replaced.

Matching source download: {args.source.name}
The DLL version resources retain 0.5.0 / 1.0.0; this archive is a separate preview.
Four pitch-preserved, faster Skyrim lute excerpts are included for use with Skyrim.
'''.encode('utf-8')
    assert not any(name.lower().endswith(('.ini', '.mp3', '.xwm', '.pdb')) for name in files)
    assert {name for name in files if name.lower().endswith('.wav')}==lute_audio
    provenance = {'build': 'Skald development preview', 'interface': 6, 'save_format': 5,
                  'source_archive': args.source.name,
                  'source_sha256': digest(args.source.read_bytes()),
                  'files': {name: digest(data) for name, data in sorted(files.items())}}
    files['SKALD-MANIFEST.json'] = json.dumps(provenance, indent=2).encode('utf-8')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, 'x', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(files.items()):
            archive.writestr(name, data)
    with zipfile.ZipFile(args.output) as archive:
        assert archive.testzip() is None
        for name, data in files.items():
            assert archive.read(name) == data, name
    print(f'{args.output}: {len(files)} files, {args.output.stat().st_size:,} bytes; hashes verified.')


if __name__ == '__main__':
    main()
