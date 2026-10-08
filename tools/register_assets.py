#!/usr/bin/env python3
"""Write Backstab Chess's asset library from the chess set in assets/chess_set.

A record is the recipe the cooker bakes from: a name and a source saying how to rebuild the
asset, filed under the engine's own key, fnv1a64("<type>:<name>"). The materials are made in
code (src/chess_look.cpp), so only meshes and textures are recorded here. Run this, then
`vkm cook`.
"""

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LIB = ROOT / "library"
MODEL = "assets/chess_set/chess_set_2k.gltf"
TEXTURES = "assets/chess_set/textures"

# One mesh per piece: the set's white pieces, whose black twins are the same shapes. The
# numbers are the importer's, which counts a glTF mesh's primitives one by one: the bishop has
# two, its body and the ball on top.
MESHES = {
    "chess:rook":       0,
    "chess:pawn":       1,
    "chess:bishop":     2,
    "chess:bishop_top": 3,
    "chess:queen":      4,
    "chess:king":       5,
    "chess:knight":     6,
    "chess:board":      7,
}

TEXTURE_USAGE = {"diff": "Color", "nor_gl": "Normal", "arm": "Data"}


def fnv1a64(text: str) -> int:
    h = 0xCBF29CE484222325
    for byte in text.encode("utf-8"):
        h ^= byte
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def write(folder: str, asset_type: str, name: str, source: dict) -> None:
    path = LIB / folder / f"{fnv1a64(f'{asset_type}:{name}'):016x}.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({"name": name, "source": source}, indent=2, sort_keys=True) + "\n")


for name, mesh in MESHES.items():
    write("meshes", "mesh", name, {"kind": "model", "path": MODEL, "mesh": mesh})

for part in ("board", "pieces_white", "pieces_black"):
    for suffix, usage in TEXTURE_USAGE.items():
        ref = f"{TEXTURES}/chess_set_{part}_{suffix}_2k.jpg"
        write("textures", "texture", ref,
              {"kind": "file", "path": ref, "usage": usage, "generateMipmaps": True, "wrap": "repeat"})

print(f"wrote {len(MESHES)} meshes and 9 textures under {LIB}")
