"""Build a local UI skin from the supported installed game (UnityPy 1.25.4)."""
import argparse
import hashlib
import pathlib
import re
import struct

import UnityPy

SPRITES = (
    "Button_Gray", "Button_Yellow", "Slot4", "Slot4_bold",
    "Slot Default Outline Bold", "Check Mark", "Icon_SystemBack",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()
    profile = (pathlib.Path(__file__).resolve().parents[1] / "src/profile.h").read_text(encoding="utf-8")
    expected = re.search(r'AssemblySha256\[\] = L"([a-f0-9]+)"', profile)[1]
    assert hashlib.sha256((args.game_dir / "GameAssembly.dll").read_bytes()).hexdigest() == expected
    env = UnityPy.load(str(args.game_dir / "dungeonslasher_Data/data.unity3d"))
    sprites, font = {}, None
    for obj in env.objects:
        if obj.type.name not in ("Sprite", "Font"):
            continue
        data = obj.read()
        if obj.type.name == "Sprite" and data.m_Name in SPRITES:
            assert data.m_Name not in sprites, f"Ambiguous sprite: {data.m_Name}"
            image = data.image.convert("RGBA")
            border = data.m_Border
            sprites[data.m_Name] = (image, (border.x, border.y, border.z, border.w))
        elif obj.type.name == "Font" and data.m_Name == "NotoSansCJKkr-Medium":
            font = bytes(data.m_FontData)
    assert len(sprites) == len(SPRITES) and font, "Required native UI assets missing"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("wb") as out:
        out.write(b"DSUI0001" + struct.pack("<I", len(SPRITES)))
        for name in SPRITES:
            image, border = sprites[name]
            pixels = image.tobytes()
            out.write(struct.pack("<II4fI", *image.size, *border, len(pixels)))
            out.write(pixels)
        out.write(struct.pack("<I", len(font)) + font)
    print(f"Extracted {len(SPRITES)} native sprites and native CJK font: {args.output}")


if __name__ == "__main__":
    main()
