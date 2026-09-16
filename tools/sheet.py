"""Composes the renders from tools/uishot into one labelled sheet, for comparing layouts at a glance.

Run after the harness: python tools/sheet.py
Writes docs/shots/all-layouts.png (the four layouts, our turn and someone else's) and
docs/shots/all-states.png (the states one layout can be in).
"""
import pathlib

from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parent.parent
SHOTS = ROOT / "docs" / "shots"

BG = (18, 20, 24)
TEXT = (228, 228, 233)
GREY = (150, 150, 158)
PAD = 18
LABEL_H = 26


def font(size, bold=False):
    path = "C:/Windows/Fonts/segoeuib.ttf" if bold else "C:/Windows/Fonts/segoeui.ttf"
    try:
        return ImageFont.truetype(path, size)
    except OSError:
        return ImageFont.load_default()


def sheet(rows, out_name, title):
    """rows: list of (row label, [(caption, file name), ...])"""
    loaded = []
    for label, cells in rows:
        images = [(caption, Image.open(SHOTS / f"{name}.png").convert("RGB")) for caption, name in cells
                  if (SHOTS / f"{name}.png").exists()]
        if images:
            loaded.append((label, images))
    if not loaded:
        return None

    width = PAD
    height = PAD + 34
    row_sizes = []
    for label, images in loaded:
        row_width = PAD + sum(image.width + PAD for _, image in images)
        row_height = LABEL_H + max(image.height for _, image in images) + PAD + 16
        row_sizes.append((row_width, row_height))
        width = max(width, row_width)
        height += row_height
    canvas = Image.new("RGB", (width, height), BG)
    draw = ImageDraw.Draw(canvas)

    draw.text((PAD, PAD), title, font=font(19, True), fill=TEXT)
    y = PAD + 34
    for (label, images), (_, row_height) in zip(loaded, row_sizes):
        draw.text((PAD, y), label, font=font(15, True), fill=TEXT)
        x = PAD
        top = y + LABEL_H
        for caption, image in images:
            canvas.paste(image, (x, top))
            draw.text((x + 2, top + image.height + 2), caption, font=font(12), fill=GREY)
            x += image.width + PAD
        y += row_height
    canvas.save(SHOTS / out_name)
    return SHOTS / out_name


def main():
    layouts = sheet(
        [
            ("1 - compact list", [("our turn", "compact-your-turn"), ("someone else", "compact-someone-else"),
                                  ("nine players", "compact-nine")]),
            ("2 - big bars", [("our turn", "bars-your-turn"), ("we are the backup", "bars-backup"),
                              ("someone else", "bars-someone-else"), ("nine players", "bars-nine")]),
            ("3 - focus card", [("our turn", "focus-your-turn"), ("we are the backup", "focus-backup"),
                               ("someone else", "focus-someone-else")]),
            ("4 - horizontal strip", [("someone else", "strip-someone-else"), ("nine, wrapped", "strip-nine-wrapped")]),
            ("5 - next up", [("someone else", "nextup-someone-else"), ("our turn", "nextup-your-turn"),
                             ("no limit set", "nextup-all")]),
        ],
        "all-layouts.png",
        "Rezz Order - turn window layouts (rendered from the addon's own code)",
    )
    states = sheet(
        [
            ("states", [("nobody ready", "compact-nobody-ready"), ("casting", "compact-casting"),
                        ("not watched yet", "compact-just-joined"), ("out of range", "compact-out-of-range")]),
            ("empty and locked", [("no order", "compact-empty"), ("only us", "compact-only-us"),
                                  ("locked", "compact-locked"), ("text size 1.6", "compact-big")]),
            ("cooldown display", [("bar + seconds", "bars-someone-else"), ("seconds only", "bars-no-cooldown-bar"),
                                  ("bar only", "bars-no-seconds")]),
            ("precast", [("marked with a star", "compact-precast")]),
            ("sharing", [("someone asked", "share-request"),
                         ("...and is not in the order", "share-request-newcomer"),
                         ("shared order", "share-offer")]),
            ("demo mode", [("12 s in", "demo-early"), ("50 s in", "demo-mid")]),
            ("the edge flash", [("your turn, 0.25", "flash-your-turn"), ("backup, 0.6", "flash-backup-strong")]),
        ],
        "all-states.png",
        "Rezz Order - states and options",
    )
    for path in (layouts, states):
        if path:
            print(f"wrote {path.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
