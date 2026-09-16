"""Builds docs/images/banner.mp4: a small looping showcase, forum-banner shaped.

Every scene is a real render from tools/uishot, scaled down, with a caption. Nothing is mocked up, so the
banner cannot drift away from what the addon actually draws.

H.264 rather than a GIF: flat panels and text come out exactly as rendered instead of being squeezed into
256 colours, and the file is a fraction of the size. Needs ffmpeg on PATH. --gif writes the old version too.

Run after the harness:  python tools/gen_banner.py [--gif]
"""
import argparse
import pathlib
import shutil
import subprocess
import tempfile

from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parent.parent
SHOTS = ROOT / "docs" / "shots"
OUT_MP4 = ROOT / "docs" / "images" / "banner.mp4"
OUT_GIF = ROOT / "docs" / "images" / "banner.gif"
OUT_POSTER = ROOT / "docs" / "images" / "banner-poster.png"

WIDTH, HEIGHT = 640, 260
CAPTION_H = 30
BG = (16, 18, 22)
CAPTION_BG = (24, 27, 32)
TEXT = (232, 232, 238)
ACCENT = (120, 220, 130)

# caption, shot, how long it stays up in milliseconds.
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
        # Never enlarged: scaling a screenshot up only softens text that is already pixel-exact.
        scale = min(room[0] / image.width, room[1] / image.height, 1.0)
        size = (max(1, int(image.width * scale)), max(1, int(image.height * scale)))
        image = image.resize(size, Image.LANCZOS)
        canvas.paste(image, ((WIDTH - size[0]) // 2, CAPTION_H + (HEIGHT - CAPTION_H - size[1]) // 2))

    draw.rectangle((0, 0, WIDTH, CAPTION_H), fill=CAPTION_BG)
    draw.line((0, CAPTION_H, WIDTH, CAPTION_H), fill=(44, 48, 54))
    draw.rectangle((0, 0, 3, CAPTION_H), fill=ACCENT)
    draw.text((14, 7), caption, font=font(15, True), fill=TEXT)
    return canvas


def write_poster(frames):
    """A still with a play button, for the README to link to the video with. GitHub will not play a video
    from a markdown page, and a linked poster keeps it from starting on its own anyway."""
    image = frames[0][0].copy()
    draw = ImageDraw.Draw(image, "RGBA")
    centre = (WIDTH // 2, CAPTION_H + (HEIGHT - CAPTION_H) // 2)
    radius = 30
    draw.ellipse((centre[0] - radius, centre[1] - radius, centre[0] + radius, centre[1] + radius),
                 fill=(12, 14, 17, 190), outline=(232, 232, 238, 230), width=2)
    triangle = [(centre[0] - 9, centre[1] - 15), (centre[0] - 9, centre[1] + 15), (centre[0] + 17, centre[1])]
    draw.polygon(triangle, fill=(232, 232, 238, 235))
    image.save(OUT_POSTER)
    return OUT_POSTER


def write_mp4(frames):
    if shutil.which("ffmpeg") is None:
        print("ffmpeg not on PATH; run with --gif instead, or install ffmpeg")
        return None

    with tempfile.TemporaryDirectory() as folder:
        work = pathlib.Path(folder)
        lines = []
        for index, (image, milliseconds) in enumerate(frames):
            name = work / f"{index:02d}.png"
            image.save(name)
            # The concat demuxer holds each still for as long as its scene asks for.
            lines.append("file '" + name.as_posix() + "'")
            lines.append(f"duration {milliseconds / 1000:.3f}")
        last = work / f"{len(frames) - 1:02d}.png"
        lines.append("file '" + last.as_posix() + "'")  # concat drops the final entry without this
        script = work / "scenes.txt"
        script.write_text("\n".join(lines), encoding="utf-8")

        command = [
            "ffmpeg", "-y", "-loglevel", "error",
            "-f", "concat", "-safe", "0", "-i", str(script),
            "-vf", "fps=12,format=yuv420p",  # yuv420p is the format every browser will play
            "-c:v", "libx264", "-preset", "veryslow", "-crf", "20",
            "-movflags", "+faststart",       # play before the whole file has arrived
            "-an", str(OUT_MP4),
        ]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode != 0:
            print(result.stderr.strip()[:400])
            return None
    return OUT_MP4


def write_gif(frames):
    # A palette per scene, not one for the banner: GIF allows a local colour table per frame, and the
    # scenes want very different colours. One shared palette spends itself on the grey UI and leaves
    # nothing for the green flash, which then comes out in bands.
    images = []
    for image, _ in frames:
        colours = len(image.getcolors(maxcolors=1 << 20) or [])
        # Flat panels and text quantise cleanly, and dithering would only add speckle. A big smooth
        # gradient is the opposite case, and those frames are the ones with many distinct colours.
        dither = Image.FLOYDSTEINBERG if colours > 20000 else Image.NONE
        images.append(image.quantize(colors=256, method=Image.MAXCOVERAGE, dither=dither))
    images[0].save(OUT_GIF, save_all=True, append_images=images[1:],
                   duration=[milliseconds for _, milliseconds in frames], loop=0, optimize=True)
    return OUT_GIF


def main():
    parser = argparse.ArgumentParser(description="Builds the showcase banner.")
    parser.add_argument("--gif", action="store_true", help="also write the GIF version")
    arguments = parser.parse_args()

    frames = [(frame(caption, shot), milliseconds) for caption, shot, milliseconds in SCENES]
    if not frames:
        print("no scenes; run build/release/rezz_uishot.exe first")
        return

    OUT_MP4.parent.mkdir(parents=True, exist_ok=True)
    written = [write_mp4(frames), write_poster(frames)]
    if arguments.gif:
        written.append(write_gif(frames))

    seconds = sum(milliseconds for _, milliseconds in frames) / 1000
    for path in written:
        if path is not None:
            print(f"wrote {path.relative_to(ROOT)} ({path.stat().st_size / 1024:.0f} KB, "
                  f"{len(frames)} scenes, {seconds:.1f} s)")


if __name__ == "__main__":
    main()
