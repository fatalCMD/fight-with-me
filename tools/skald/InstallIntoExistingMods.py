"""Integrate the verified Skald build into the three existing local MO2 mods.

Keeps mod entries, priority and INIs intact; backs up every replaced file first.
"""
from pathlib import Path
from datetime import datetime
import argparse
import hashlib
import json
import subprocess
import zipfile

REPO = Path(__file__).resolve().parents[2]
WORKSPACE = REPO.parent
MODS = WORKSPACE / 'MODS/mods'
PROFILE = WORKSPACE / 'MODS/profiles/Nolvus Awakening/modlist.txt'
CORE, PATHS, WALK = 'Fight With Me', 'Fight With Me - Paths', 'Walk With Me - Main'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def game_closed():
    tasks = subprocess.check_output(['tasklist', '/FO', 'CSV', '/NH'], text=True)
    assert not any(line.lower().startswith(('"skyrimse.exe"', '"skyrim.exe"'))
                   for line in tasks.splitlines()), 'Close Skyrim before updating its DLLs.'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--current-build',action='store_true',help='Use the current built DLLs and translation templates with the verified Skald assets')
    parser.add_argument('--check-only',action='store_true',help='Validate the prepared update and winning providers without writing or requiring Skyrim to close')
    args=parser.parse_args()
    if not args.check_only:
        game_closed()
    enabled = PROFILE.read_text().splitlines()
    for name in (CORE, PATHS, WALK):
        assert (MODS / name).is_dir() and '+' + name in enabled, f'Expected enabled main mod: {name}'
    protected = {PROFILE: PROFILE.read_bytes()}
    for folder in (MODS / CORE, MODS / PATHS, MODS / WALK, WORKSPACE / 'MODS/overwrite'):
        for path in folder.rglob('*.ini'):
            protected[path] = path.read_bytes()
    updates = {}

    def add(mod, name, data):
        path = (MODS / mod / name).resolve()
        assert path.is_relative_to((MODS / mod).resolve())
        assert path.suffix.lower() not in {'.ini', '.esp', '.pex'}, path
        updates[path] = data

    archives = [REPO / 'package/Path-of-the-Companions-Skald-Preview-v9.zip',
                WORKSPACE / 'Wayfarer/release/skald-taverns-v1/Walk-With-Me-Skald-Taverns-v1.zip']
    # A core/Paths rebuild must not roll back a separately updated Walk With Me.
    if args.current_build:
        archives=archives[:1]
    for archive, manifest_name in zip(archives, ('SKALD-MANIFEST.json', 'SKALD-TAVERNS-MANIFEST.json')):
        with zipfile.ZipFile(archive) as source:
            assert source.testzip() is None
            manifest = json.loads(source.read(manifest_name))
            for name, expected in manifest['files'].items():
                data = source.read(name)
                assert digest(data) == expected, name
                if name.startswith('README-SKALD'):
                    continue  # Retired overlay installation instructions.
                if archive == archives[0]:
                    mod = CORE if name == 'SKSE/Plugins/FightWithMe.dll' else PATHS
                    add(mod, name, data)
                    if name == 'SKSE/Plugins/FightWithMe/Translations/FightWithMe_ENGLISH.txt':
                        add(CORE, name, data)
                else:
                    add(WALK, name, data)
    if args.current_build:
        for folder,pattern in (('meshes/FightWithMe','Skald*.nif'),('textures/FightWithMe/Skald','*.dds')):
            for asset in (REPO/'assets'/folder).glob(pattern):
                add(PATHS,asset.relative_to(REPO/'assets').as_posix(),asset.read_bytes())
        add(PATHS,'sound/fx/FightWithMe/Skald/SkaldNote.wav',
            (REPO/'assets/sound/fx/FightWithMe/Skald/SkaldNote.wav').read_bytes())
        for mod,name in ((CORE,'FightWithMe.dll'),(PATHS,'FightWithMePaths.dll')):
            add(mod,'SKSE/Plugins/'+name,(REPO/'build/skyrim/Release'/name).read_bytes())
        for mod in (CORE,PATHS):
            add(mod,'SKSE/Plugins/FightWithMe/Translations/FightWithMe_ENGLISH.txt',
                (REPO/'config/Translations/FightWithMe_ENGLISH.txt').read_bytes())
        add(PATHS,'SKSE/Plugins/FightWithMe/Translations/FightWithMePaths_ENGLISH.txt',
            (REPO/'paths/config/Translations/FightWithMePaths_ENGLISH.txt').read_bytes())
    for mod, name in ((CORE, 'FightWithMe.dll'), (PATHS, 'FightWithMePaths.dll')):
        assert updates[(MODS / mod / 'SKSE/Plugins' / name).resolve()] == (REPO / 'build/skyrim/Release' / name).read_bytes()
    if not args.current_build:
        assert updates[(MODS / WALK / 'SKSE/Plugins/Wayfarer.dll').resolve()] == (WORKSPACE / 'Wayfarer/build/modern/bin/Release/Wayfarer.dll').read_bytes()
    add(PATHS, 'docs/SKALD.md', (REPO / 'docs/SKALD.md').read_bytes())
    if not args.current_build:
        add(WALK, 'docs/skald-taverns.md', (WORKSPACE / 'Wayfarer/docs/skald-taverns.md').read_bytes())

    # MO2 records higher-priority loose mods first; Overwrite has final priority.
    providers = [WORKSPACE / 'MODS/overwrite'] + [MODS / line[1:] for line in enabled if line.startswith('+')]
    for destination, data in updates.items():
        relative = destination.relative_to(MODS.resolve())
        runtime = Path(*relative.parts[1:])
        if runtime.parts[0].lower() not in {'skse', 'interface', 'meshes', 'textures', 'sound'}:
            continue
        winner = next((base / runtime for base in providers
                       if (base / runtime).resolve() in updates or (base / runtime).is_file()), None)
        assert winner is not None
        actual = updates.get(winner.resolve())
        if actual is None:
            actual = winner.read_bytes()
        assert actual == data, f'A higher-priority mod would hide the update: {winner}'

    updates={path:data for path,data in updates.items() if not path.exists() or path.read_bytes()!=data}
    if args.check_only:
        print(json.dumps({'status':'prepared; no files installed','updated_files':len(updates),
                          'files':{str(path.relative_to(MODS.resolve())):digest(data) for path,data in updates.items()},
                          'protected_files':len(protected),'load_priority':'All prepared runtime files win or have identical winning copies.'},indent=2))
        return
    backup = WORKSPACE / 'Backups' / ('Skald-main-mods-' + datetime.now().strftime('%Y%m%d-%H%M%S'))
    backup.mkdir(parents=True, exist_ok=False)
    record = {'archives': {str(p.relative_to(WORKSPACE)): digest(p.read_bytes()) for p in archives},
              'current_build': args.current_build,
              'files': {}, 'status': 'backed up; installation pending'}
    for destination, data in updates.items():
        relative = destination.relative_to(MODS.resolve())
        old = destination.read_bytes() if destination.exists() else None
        record['files'][relative.as_posix()] = {'before': digest(old) if old is not None else None,
                                               'after': digest(data)}
        if old is not None:
            saved = backup / 'files' / relative
            saved.parent.mkdir(parents=True, exist_ok=True)
            saved.write_bytes(old)
    report = backup / 'INSTALL-MANIFEST.json'
    report.write_text(json.dumps(record, indent=2))
    game_closed()
    for destination, data in updates.items():
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
        assert destination.read_bytes() == data
    for path, data in protected.items():
        assert path.read_bytes() == data, f'Protected configuration changed: {path}'
    record['status'] = 'installed and verified'
    record['protected_files_verified'] = len(protected)
    report.write_text(json.dumps(record, indent=2))
    print(json.dumps({'updated_files': len(updates), 'existing_mods': [CORE, PATHS, WALK],
                      'backup': str(backup), 'protected_files_verified': len(protected),
                      'load_priority': 'All updated runtime files win or have identical winning copies.'}, indent=2))


if __name__ == '__main__':
    main()
