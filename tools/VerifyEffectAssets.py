#!/usr/bin/env python3
"""Audit standalone FWM effect meshes against only packaged files and stock Skyrim."""

import argparse
from pathlib import Path
import re
import struct


def normal(path):
    return path.replace("/", "\\").casefold().rstrip("\x00")


def bsa_names(path):
    """Read only the directory of a named Skyrim BSA (104/105)."""
    with path.open("rb") as stream:
        magic, version, offset, flags, folders, files, _, name_bytes, _ = struct.unpack(
            "<4s8I", stream.read(36)
        )
        if magic != b"BSA\0" or version not in (104, 105) or flags & 3 != 3:
            raise ValueError(f"Unsupported or unnamed archive: {path}")
        size = 24 if version == 105 else 16
        stream.seek(offset)
        counts = [struct.unpack_from("<I", stream.read(size), 8)[0] for _ in range(folders)]
        folder_names = []
        for count in counts:
            length = stream.read(1)[0]
            name = stream.read(length).rstrip(b"\0").decode("cp1252")
            folder_names.extend([name] * count)
            stream.seek(count * 16, 1)
        names = stream.read(name_bytes).rstrip(b"\0").split(b"\0")
        if len(folder_names) != files or len(names) != files:
            raise ValueError(f"Inconsistent archive directory: {path}")
        return {normal(folder + "\\" + name.decode("cp1252")) for folder, name in zip(folder_names, names)}


def main():
    import json
    import subprocess
    import shutil
    parser = argparse.ArgumentParser(description="Verify every packaged effect texture against FWM and stock Skyrim only.")
    parser.add_argument("--assets", type=Path)
    parser.add_argument("--skyrim", type=Path, required=True, help="Stock Skyrim Data folder (not a modded virtual filesystem)")
    parser.add_argument("--node", default=shutil.which("node"))
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    assets = args.assets or repo / "assets"
    if not args.node:
        raise ValueError("Node is required to read and validate the actual NIF structures")
    models = json.loads(subprocess.check_output([args.node, str(repo / "tools/fx/InspectAssets.cjs"), str(assets)]))
    own = {normal(str(p.relative_to(assets))) for p in assets.rglob("*") if p.is_file()}
    vanilla = set()
    for archive in args.skyrim.glob("Skyrim - Textures*.bsa"):
        vanilla.update(bsa_names(archive))
    if not vanilla:
        raise ValueError("No stock Skyrim texture archives found")
    failures = []
    textures = set()
    for model in models:
        for raw in model["textures"]:
            texture = normal(raw)
            if not texture.startswith("textures\\"):
                texture = "textures\\" + texture
            textures.add(texture)
            if texture not in own and texture not in vanilla:
                failures.append(f"{model['path']}: texture is neither packaged nor stock Skyrim: {texture}")
    header = (repo / "include/FWM/EffectAssets.h").read_text()
    runtime = (repo / "src/Effects.cpp").read_text()
    wanted = set(re.findall(r'R"\((FightWithMe[^)]+\.nif)\)"', header + runtime))
    elements = re.findall(r'Weapon\{ Look::\w+, "([^"]+)"', header)
    shapes = re.findall(r'"([^"]+)"', re.search(r'weaponShapes\{([^}]+)', header)[1])
    wanted.update(f"FightWithMe/Support/{element}{shape}.nif" for element in elements for shape in shapes)
    wanted.update(f"FightWithMe/Windstep{sex}{suffix}.nif"
                  for sex in ("", "Female") for suffix in ("", "2", "3", "4", "5", "6", "7", "8"))
    for model in wanted:
        if normal("meshes/" + model) not in own:
            failures.append("Missing runtime model: " + model)
    forbidden = ("Triumvirate", "Diviner\\", "ApocNew\\", "vibrant weapons", "windexp01", "fxwatercloak01", "wolf_shell.dds", "bear_shell.dds")
    for file in assets.rglob("*.nif"):
        data=file.read_bytes().lower()
        for name in forbidden:
            if name.lower().encode() in data:
                failures.append(f"External effect reference in {file.name}: {name}")
    for name in ("Triumvirate - Mage Archetypes.esp", "Elemental Destruction Magic Redux.esp", "Vibrant weapons.esl", "wolf_shell.dds", "bear_shell.dds"):
        if name.lower() in (header+runtime).lower(): failures.append("Runtime still references external FX: "+name)
    if normal("textures/FightWithMe/EffectsPack-1.txt") not in own:
        failures.append("Missing required Effects pack contract marker")
    if failures:
        print("\n".join("FAIL: "+message for message in failures))
        return 1
    print(f"PASS: {len(models)} meshes, {len(textures)} unique texture references, {len(wanted)} runtime model selections.")
    print("All references resolve using only the Effects pack and stock Skyrim; no third-party FX mods supplied.")
    print("NIF geometry/animation-node checks passed. Actual in-game attachment/rendering still needs a playtest.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
