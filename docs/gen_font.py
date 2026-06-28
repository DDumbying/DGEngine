"""
Generate assets/font.png — the DGEngine glyph atlas.

Usage:
    python3 gen_font.py                        # auto-detect a system monospace font
    python3 gen_font.py /path/to/MyFont.ttf    # use a specific font
    python3 gen_font.py /path/to/MyFont.ttf 8  # custom font + point size

The atlas layout (must match font_atlas.h):
    96 x 60 px  (16 cols x 6 px,  6 rows x 10 px)
    ASCII chars 32..126, left-to-right, top-to-bottom.
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFont

CHAR_W  = 6
CHAR_H  = 10
COLS    = 16
ROWS    = 6
FIRST   = 32
LAST    = 126

def load_font(path=None, size=8):
    if path:
        if not os.path.exists(path):
            print(f"Error: font not found at '{path}'", file=sys.stderr)
            sys.exit(1)
        return ImageFont.truetype(path, size)

    candidates = [
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Bold.ttf",
        "/usr/share/fonts/truetype/ubuntu/UbuntuMono-B.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono-Bold.ttf",
        "/usr/share/fonts/truetype/hack/Hack-Bold.ttf",
        "/usr/share/fonts/truetype/firacode/FiraCode-Regular.ttf",
    ]
    for p in candidates:
        if os.path.exists(p):
            print(f"Using font: {p}")
            return ImageFont.truetype(p, size)

    print("Warning: no system monospace font found, falling back to PIL default.")
    return ImageFont.load_default()

def generate(font_path=None, size=8):
    font = load_font(font_path, size)
    img  = Image.new('RGBA', (COLS * CHAR_W, ROWS * CHAR_H), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    for code in range(FIRST, LAST + 1):
        ch  = chr(code)
        idx = code - FIRST
        cx  = (idx % COLS) * CHAR_W
        cy  = (idx // COLS) * CHAR_H

        try:
            bbox = font.getbbox(ch)
            gw   = bbox[2] - bbox[0]
            gh   = bbox[3] - bbox[1]
            ox   = (CHAR_W - gw) // 2 - bbox[0]
            oy   = (CHAR_H - gh) // 2 - bbox[1]
        except AttributeError:
            ox, oy = 1, 1

        draw.text((cx + ox, cy + oy), ch, font=font, fill=(255, 255, 255, 255))

    out = os.path.join(os.path.dirname(__file__), '..', 'assets', 'font.png')
    out = os.path.normpath(out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    img.save(out)
    print(f"Generated {out}  ({COLS*CHAR_W}x{ROWS*CHAR_H} px)")

if __name__ == "__main__":
    fpath = sys.argv[1] if len(sys.argv) > 1 else None
    fsize = int(sys.argv[2]) if len(sys.argv) > 2 else 8
    generate(fpath, fsize)
