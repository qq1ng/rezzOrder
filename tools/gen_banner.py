"""Builds docs/images/banner.gif: a small looping showcase, forum-banner shaped.

Every frame is a real render from tools/uishot, scaled down, with a caption. Nothing is mocked up, so the
banner cannot drift away from what the addon actually draws.

Run after the harness:  python tools/gen_banner.py
"""
import pathlib

from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parent.parent
SHOTS = ROOT / "docs" / "shots"
OUT = ROOT / "docs" / "images" / "banner.gif"

WIDTH, HEIGHT = 640, 260
CAPTION_H = 30
BG = (16, 18, 22)
CAPTION_BG = (24, 27, 32)
TEXT = (232, 232, 238)
ACCENT = (120, 220, 130)

# caption, shot, how long it stays up in milliseconds. Written out per scene rather than by repeating
# frames: GIF optimisation merges identical frames, which would throw the timing away.
SCENES = [
    ("Rezz Order: whose turn it is to revive", "compact-someone-else", 2200),
    ("Recharge, downed and out of range at a glance", "compact-out-of-range", 2000),
    ("Five layouts: compact list", "compact-your-turn", 1100),
    ("Five layouts: big bars", "bars-your-turn", 1100),
    ("Five layouts: focus card", "focus-your-turn", 1100),
    ("Five layouts: next up", "nextup-someone-else", 1100),
    ("Five layouts: horizontal strip", "strip-someone-else", 1100),
    ("A sound and a screen flash when the turn is yours", "flash-your-turn", 2200),
    ("Share the order in squad chat", "share-offer", 2200),
    ("Latecomers ask with ?rezzorder", "share-request-newcomer", 2200),
]


def font(size, bold=False):
    path = "C:/Windows/Fonts/segoeuib.ttf" if bold else "C:/Windows/Fonts/segoeui.ttf"
    try:
        return ImageFont.truetype(path, size)
    except OSError:
        return ImageFont.load_default()


def frame(caption, shot):
    canvas = Image.new("RGB", (WIDTH, HEIGHT), BG)
    draw = ImageDraw.Draw(canvas)

    path = SHOTS / f"{shot}.png"
    if path.exists():
        image = Image.open(path).convert("RGB")
        # The harness leaves a margin around each window; the banner has its own, so drop it.
        if image.width > 120 and image.height > 120:
            image = image.crop((20, 20, image.width - 20, image.height - 20))
        room = (WIDTH - 20, HEIGHT - CAPTION_H - 14)
        # Never enlarged: scaling a screenshot up invents in-between tones, and those are exactly what a
        # 256-colour palette cannot hold without speckling.
        scale = min(room[0] / image.width, room[1] / image.height, 1.0)
        size = (max(1, int(image.width * scale)), max(1, int(image.height * scale)))
        image = image.resize(size, Image.LANCZOS)
        canvas.paste(image, ((WIDTH - size[0]) // 2, CAPTION_H + (HEIGHT - CAPTION_H - size[1]) // 2))

    draw.rectangle((0, 0, WIDTH, CAPTION_H), fill=CAPTION_BG)
    draw.line((0, CAPTION_H, WIDTH, CAPTION_H), fill=(44, 48, 54))
    draw.rectangle((0, 0, 3, CAPTION_H), fill=ACCENT)
    draw.text((14, 7), caption, font=font(15, True), fill=TEXT)
    return canvas


def main():
    frames = [(frame(caption, shot), ms) for caption, shot, ms in SCENES]
    if not frames:
        print("no frames; run build/release/rezz_uishot.exe first")
        return

    # A palette per scene, not one for the banner: GIF allows a local colour table per frame, and the
    # scenes want very different colours. A shared palette has to spend most of itself on the grey UI and
    # then has nothing left for the green flash, which comes out in bands.
    images = []
    for image, _ in frames:
        colours = len(image.getcolors(maxcolors=1 << 20) or [])
        # Flat panels and text quantise cleanly and dithering would only add speckle. A big smooth gradient
        # is the opposite case, and those frames are the ones with a lot of distinct colours.
        dither = Image.FLOYDSTEINBERG if colours > 20000 else Image.NONE
        images.append(image.quantize(colors=256, method=Image.MAXCOVERAGE, dither=dither))

    OUT.parent.mkdir(parents=True, exist_ok=True)
    images[0].save(OUT, save_all=True, append_images=images[1:], duration=[ms for _, ms in frames],
                   loop=0, optimize=True)
    print(f"wrote {OUT.relative_to(ROOT)} ({OUT.stat().st_size / 1024:.0f} KB, {len(images)} scenes, "
          f"{sum(ms for _, ms in frames) / 1000:.1f} s per loop)")


if __name__ == "__main__":
    main()
