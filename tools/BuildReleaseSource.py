"""Capture current plugin source, including untracked work and matching dependencies."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
EXCLUDED = {'.git', '__pycache__', 'node_modules', 'build', 'package', 'out', '.vs', '.github', '.claude', '.factory'}
TEXT = {'.c', '.cpp', '.h', '.hpp', '.in', '.cmake', '.json', '.txt', '.ini',
        '.ps1', '.py', '.js', '.cjs', '.mjs', '.html', '.css', '.md', '.svg', '.natvis', '.inc', '.rc'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    files = {}

    def add(path, name):
        assert path.is_file(), path
        assert '..' not in Path(name).parts and not name.startswith('/')
        assert name not in files, name
        files[name] = path.read_bytes()

    def tree(path, prefix, predicate=lambda p: True):
        assert path.is_dir(), path
        for p in sorted(path.rglob('*')):
            relative = p.relative_to(path)
            if p.is_file() and not (set(relative.parts) & EXCLUDED) and predicate(p):
                add(p, prefix + relative.as_posix())

    for folder in ('src', 'include', 'cmake', 'tests', 'config', 'tools',
                   'paths/src', 'paths/include', 'paths/cmake', 'paths/config', 'paths/tools'):
        path = ROOT / folder
        if path.is_dir():
            tree(path, folder + '/', lambda p: p.suffix.lower() in TEXT and not p.name.endswith('_user.ini'))
    for name in ('CMakeLists.txt', 'CMakePresets.json', 'vcpkg.json', 'vcpkg-configuration.json',
                 'README.md', 'THIRD-PARTY-NOTICES.md', 'paths/README.md', 'docs/RELEASE-NOTES.md'):
        add(ROOT / name, name)
    tree(ROOT / 'assets/fonts', 'assets/fonts/')
    tree(ROOT / 'assets/interface/FightWithMe', 'assets/interface/FightWithMe/',
         lambda p: p.suffix.lower() in {'.svg', '.txt'})

    common = ROOT / 'extern/CommonLibSSE-NG'
    tree(common, 'extern/CommonLibSSE-NG/',
         lambda p: 'extern' not in p.relative_to(common).parts and
         (p.suffix.lower() in TEXT or 'licenses' in p.relative_to(common).parts or
          p.name in {'COPYING', 'LICENSE', '.clang-format'}))
    openvr = common / 'extern/openvr'
    for folder in ('headers', 'src'):
        tree(openvr / folder, 'extern/CommonLibSSE-NG/extern/openvr/' + folder + '/')
    for name in ('CMakeLists.txt', 'LICENSE', 'README.md', 'Toolchain-clang.cmake', 'lib/win64/openvr_api.lib'):
        add(openvr / name, 'extern/CommonLibSSE-NG/extern/openvr/' + name)
    tree(ROOT / 'build/skyrim/_deps/hde64-src', 'third-party-sources/minhook/',
         lambda p: p.suffix.lower() in TEXT or p.name == 'LICENSE')
    dependencies = {
        'directxmath': 'jun2026-d9b23f80e1.clean',
        'directxtk': 'may2026-a68e548ea6.clean',
        'fmt': '12.2.0-d352ede310.clean',
        'rapidcsv': 'v8.99-fcbbd6a5ea.clean',
        'spdlog': 'v1.17.0-8a1a388e84.clean',
    }
    for name, source in dependencies.items():
        tree(Path('X:/vcpkg/buildtrees') / name / 'src' / source,
             'third-party-sources/' + name + '/',
             lambda p: p.suffix.lower() not in {'.exe', '.dll', '.pdb', '.obj', '.lib'})
        share = ROOT / 'build/skyrim/vcpkg_installed/x64-windows-static-md/share' / name
        for file in ('copyright', 'vcpkg.spdx.json'):
            add(share / file, 'third-party-sources/provenance/' + name + '-' + file)
    add(ROOT / 'build/skyrim/vcpkg_installed/vcpkg/status', 'third-party-sources/provenance/vcpkg-status.txt')
    add(common / 'COPYING', 'COPYING')
    files['BUILD-SOURCE.md'] = b'''# Fight With Me 0.5.0 / Path of the Companions 1.0.0 source

This is the current working source, including new untracked files, for both DLLs.
It is a developer download, not a game mod. Large effect models/textures and class
backdrops are supplied separately in POTC Effects-1.0.21.zip. To rebuild
the runtime archives, copy that download's meshes, textures and interface folders
into assets/. Theme fonts and small UI icons are already included here.

Use Visual Studio 2022 C++ Build Tools (v143, Windows SDK), CMake, Git and vcpkg.
Set VCPKG_ROOT to your vcpkg checkout. From this extracted directory run:

    cmake --preset skyrim -DFETCHCONTENT_SOURCE_DIR_HDE64=<absolute-path-to>/third-party-sources/minhook
    cmake --build build/skyrim --config Release
    ctest --test-dir build/skyrim -C Release --output-on-failure

Native DLLs are in build/skyrim/Release. Packaging scripts are tools/BuildPackage.ps1
and paths/tools/BuildPathsPackage.ps1. Keep their version defaults together with
CMakeLists.txt; the scripts reject DLLs whose version does not match the archive.

CommonLibSSE-NG 7.2.0, its OpenVR dependency, MinHook and the exact patched vcpkg
library sources are included. vcpkg manifests and provenance preserve versions.
vcpkg still needs its own tool/registry caches or network for normal configuration;
this is not an entirely offline compiler/toolchain distribution.

The plugin code declares GPL-3.0-or-later. See COPYING and the dependency licenses,
CommonLib's exceptions, font licenses and THIRD-PARTY-NOTICES.md. Skyrim-derived
effect assets have separate terms and are intended for use with Skyrim only.

SOURCE-MANIFEST.json records every included file and the matching DLL hashes.
Build paths/timestamps/compiler versions can change the resulting binary hashes.
'''
    provenance = {
        'versions': {'FightWithMe': '0.5.0', 'FightWithMePaths': '1.0.0'},
        'working_tree_included': True,
        'repository_revision': subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip(),
        'dlls': {name: hashlib.sha256((ROOT / 'build/skyrim/Release' / name).read_bytes()).hexdigest()
                 for name in ('FightWithMe.dll', 'FightWithMePaths.dll')},
        'files': {name: hashlib.sha256(data).hexdigest() for name, data in sorted(files.items())},
    }
    files['SOURCE-MANIFEST.json'] = json.dumps(provenance, indent=2).encode()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, 'x', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, data in sorted(files.items()):
            z.writestr(name, data)
    with zipfile.ZipFile(args.output) as z:
        assert z.testzip() is None
        for name, data in files.items():
            assert z.read(name) == data, name
    print(f'{args.output.name}: {len(files)} files, {args.output.stat().st_size:,} bytes; verified.')


if __name__ == '__main__':
    main()
