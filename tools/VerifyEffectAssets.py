#!/usr/bin/env python3
"""Audit FWM's external FX catalog without extracting or redistributing assets.

python tools/VerifyEffectAssets.py --mods <MO2 mods> --plugins <profile plugins.txt>
Checks plugin activation, model references, archive/loose-file availability, and
Vibrant Weapons' embedded attachment nodes. Does not test rendering in Skyrim.
"""

import argparse
from pathlib import Path
import re
import struct
import zlib


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


def plugin_models(path):
    """Read MODL references from the source plugin, including compressed records."""
    data = path.read_bytes()
    models = set()

    def walk(start, end):
        pos = start
        while pos < end:
            tag, size = struct.unpack_from("<4sI", data, pos)
            if tag == b"GRUP":
                if size < 24 or pos + size > end:
                    raise ValueError(f"Malformed GRUP in {path}")
                walk(pos + 24, pos + size)
                pos += size
                continue
            stop = pos + 24 + size
            if stop > end:
                raise ValueError(f"Malformed record in {path}")
            flags = struct.unpack_from("<I", data, pos + 8)[0]
            body = data[pos + 24:stop]
            if flags & 0x40000:
                expected = struct.unpack_from("<I", body)[0]
                body = zlib.decompress(body[4:])
                if len(body) != expected:
                    raise ValueError(f"Incorrect decompressed length in {path}")
            cursor, extended = 0, None
            while cursor < len(body):
                field, length = struct.unpack_from("<4sH", body, cursor)
                cursor += 6
                if field == b"XXXX":
                    extended = struct.unpack_from("<I", body, cursor)[0]
                    cursor += length
                    continue
                length = extended if extended is not None else length
                extended = None
                if cursor + length > len(body):
                    raise ValueError(f"Malformed subrecord in {path}")
                if field == b"MODL":
                    models.add(normal(body[cursor:cursor + length].decode("cp1252")))
                cursor += length
            pos = stop

    walk(0, len(data))
    return models


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mods", type=Path, required=True)
    parser.add_argument("--plugins", type=Path, required=True)
    args = parser.parse_args()
    header = (Path(__file__).resolve().parents[1] / "include/FWM/EffectAssets.h").read_text()
    sources = re.search(r"enum class Source[^\{]*\{([^}]+)", header)[1]
    sources = [name.strip() for name in sources.split(",") if name.strip() != "kCount"]
    plugins = re.findall(r'"([^"]+\.(?:esp|esl|esm))"', header)
    if len(sources) != len(plugins):
        raise ValueError("Catalog source/plugin counts differ")
    art = re.findall(r'Art\{ Look::(\w+), Source::(\w+), R"\(([^)]+)\)"', header)
    weapons = re.findall(r'Weapon\{ Look::(\w+), "([^"]+)"', header)
    shape_block = re.search(r"weaponShapes\{([^}]+)", header)[1]
    shapes = re.findall(r'"([^"]+)"', shape_block)
    if not art or not weapons or not shapes:
        raise ValueError("Could not read the FX catalog")
    active = {line[1:].strip().casefold() for line in args.plugins.read_text(encoding="utf-8-sig").splitlines() if line.startswith("*")}
    failures = []
    directories = [path for path in args.mods.iterdir() if path.is_dir()]
    checked = 0
    for source, plugin in zip(sources, plugins):
        matches = [folder / plugin for folder in directories if (folder / plugin).is_file()]
        if len(matches) != 1:
            failures.append(f"{plugin}: expected one installed source, found {len(matches)}")
            continue
        installed = matches[0]
        if plugin.casefold() not in active:
            failures.append(f"{plugin}: not enabled in {args.plugins}")
        references = plugin_models(installed)
        available = set()
        for archive in installed.parent.glob("*.bsa"):
            available.update(bsa_names(archive))
        specs = [(look, model, None) for look, owner, model in art if owner == source]
        if source == "kVibrant":
            specs.extend((look, f"vibrant weapons\\magic\\{element}{shape}.nif", shape)
                         for look, element in weapons for shape in shapes)
        for look, model, shape in specs:
            checked += 1
            key = normal(model)
            mesh = "meshes\\" + key
            loose = installed.parent.joinpath("meshes", *model.replace("/", "\\").split("\\"))
            if key not in references:
                failures.append(f"{look}: {model} is not referenced by {plugin}")
            if not loose.is_file() and mesh not in available:
                failures.append(f"{look}: missing model {model}")
            if shape:
                if not loose.is_file():
                    failures.append(f"{look}: attachment inspection needs the original loose Vibrant mesh {model}")
                else:
                    target = b"NamedNode&SHIELD" if shape.endswith("L") else b"NamedNode&WEAPON"
                    if target not in loose.read_bytes():
                        failures.append(f"{look}: {model} lacks expected {target.decode()} attachment")
        print(f"{plugin}: {len(specs)} model selections inspected")
    if failures:
        for message in failures:
            print(f"FAIL: {message}")
        return 1
    print(f"PASS: {checked} selections ({len(art)} body/impact, {len(weapons) * len(shapes)} weapon variants); all source plugins enabled.")
    print("Asset availability and native weapon attachment metadata verified; in-game rendering remains a separate check.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
