"""Draws PNG mockups of the turn window layouts (docs/mockups/), to pick a layout before building it.

Uses the real profession icons from assets/icons and a system font, at the pixel sizes the addon would use.
Run: python tools/gen_mockups.py
"""
import pathlib

from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parent.parent
ICONS = ROOT / "assets" / "icons"
OUT = ROOT / "docs" / "mockups"
OUT.mkdir(parents=True, exist_ok=True)

FONT = "C:/Windows/Fonts/segoeui.ttf"
FONT_BOLD = "C:/Windows/Fonts/segoeuib.ttf"

BG = (26, 30, 34)
TEXT = (219, 219, 224)
GREY = (150, 150, 155)
GREEN = (102, 230, 102)
ORANGE = (255, 166, 51)
RED = (255, 89, 89)
DARK_RED = (191, 51, 51)
YELLOW = (255, 230, 77)
PANEL = (16, 18, 20)
COOLDOWN = (64, 96, 132)  # recharge fill: blue, so it is not another green/orange

PROF_COLOR = {
    1: (115, 194, 217), 2: (255, 209, 102), 4: (140, 219, 130),
    6: (245, 138, 135), 7: (181, 120, 214), 8: (82, 166, 112),
}

# name, profession, elite spec, status, colour, cooldown fraction (0 = ready, 1 = just used)
PLAYERS = [
    ("Sleeplxss", 7, 73, "ready", GREEN, 0.0),
    ("Gorath", 6, 80, "ready", GREEN, 0.0),
    ("Magaton", 6, 56, "63s", GREY, 0.53),
    ("murako", 7, 59, "DOWN", RED, 0.0),
    ("Moister", 4, 55, "28s", GREY, 0.23),
    ("Sairana", 2, 61, "ready", GREEN, 0.0),
]
MORE = PLAYERS + [
    ("Trubbi", 8, 34, "ready", GREEN, 0.0),
    ("Vivi Prin", 1, 62, "84s", GREY, 0.70),
    ("Enodis", 4, 72, "ready", GREEN, 0.0),
]
SELF = 3  # "you" are murako unless a drawing says otherwise


def font(size, bold=False):
    return ImageFont.truetype(FONT_BOLD if bold else FONT, size)


def icon(profession, spec, size):
    path = ICONS / f"spec_{spec}.png"
    if not path.exists():
        path = ICONS / f"prof_{profession}.png"
    image = Image.open(path).convert("RGBA").resize((size, size), Image.LANCZOS)
    tint = Image.new("RGBA", image.size, PROF_COLOR.get(profession, (200, 200, 200)) + (255,))
    tinted = Image.new("RGBA", image.size)
    for x in range(image.width):
        for y in range(image.height):
            r, g, b, a = image.getpixel((x, y))
            tr, tg, tb, _ = tint.getpixel((x, y))
            tinted.putpixel((x, y), (r * tr // 255, g * tg // 255, b * tb // 255, a))
    return tinted


def canvas(width, height):
    image = Image.new("RGBA", (width, height), BG)
    draw = ImageDraw.Draw(image, "RGBA")
    return image, draw


def window(draw, box, alpha=200):
    draw.rounded_rectangle(box, radius=3, fill=PANEL + (alpha,), outline=(70, 75, 80, 255))


def status_color(status):
    if status == "ready":
        return GREEN
    if status == "DOWN":
        return RED
    if status == "DEAD":
        return DARK_RED
    return GREY


def marker(draw, x, y, index, up, backup, size=13):
    if index == up:
        draw.text((x, y), "UP", font=font(size, True), fill=GREEN)
    elif index == backup:
        draw.text((x, y), "BK", font=font(size, True), fill=ORANGE)
    else:
        draw.text((x, y), str(index + 1), font=font(size), fill=GREY)


def compact(players, up, backup, me):
    """Layout 1: compact list."""
    rows = players[:6]
    width, height = 300, 46 + len(rows) * 20
    image, draw = canvas(width, height)
    x, y = 20, 16
    window(draw, (x - 6, y - 6, x + 224, y + 22 + len(rows) * 20 + 2))
    small, bold = font(14), font(14, True)

    if me == up:
        draw.text((x, y), "YOUR TURN", font=font(15, True), fill=GREEN)
    else:
        draw.text((x, y), "Up:", font=small, fill=GREY)
        draw.text((x + 26, y), players[up][0], font=small, fill=TEXT)
        draw.text((x + 150, y + 1), f"you: {me + 1}.", font=font(12), fill=YELLOW)
    y += 22
    for index, (name, profession, spec, status, _, _) in enumerate(rows):
        if index == up:
            draw.rounded_rectangle((x - 4, y - 2, x + 220, y + 16), radius=2, fill=(60, 170, 60, 110))
            draw.rectangle((x - 6, y - 2, x - 3, y + 16), fill=(120, 230, 120, 230))
        elif index == backup:
            draw.rounded_rectangle((x - 4, y - 2, x + 220, y + 16), radius=2, fill=(200, 130, 30, 80))
            draw.rectangle((x - 6, y - 2, x - 5, y + 16), fill=(240, 170, 60, 200))
        marker(draw, x, y, index, up, backup, 14)
        image.alpha_composite(icon(profession, spec, 16), (x + 26, y))
        draw.text((x + 48, y), name, font=bold if index == me else small, fill=YELLOW if index == me else TEXT)
        draw.text((x + 160, y), status, font=small, fill=status_color(status))
        y += 20
    return image


def big_bars(players, up, backup, me):
    """Layout 2: tall bars, recharge fills the bar."""
    row_h, bar_w, gap = 38, 340, 4
    width, height = 420, 96 + len(players) * (row_h + gap)
    image, draw = canvas(width, height)
    x, y = 20, 16
    window(draw, (x - 8, y - 8, x + bar_w + 8, y + 42 + len(players) * (row_h + gap)))

    # Header: what you have to know, big. Your own place is always on the right.
    if me == up:
        draw.text((x, y), "YOUR TURN", font=font(26, True), fill=GREEN)
    else:
        draw.text((x, y), "UP", font=font(17, True), fill=GREEN)
        image.alpha_composite(icon(players[up][1], players[up][2], 22), (x + 34, y + 3))
        draw.text((x + 62, y - 1), players[up][0], font=font(24, True), fill=TEXT)
        you = f"you: {me + 1}."
        draw.text((x + bar_w - draw.textlength(you, font=font(15)), y + 8), you, font=font(15), fill=YELLOW)
    y += 38

    for index, (name, profession, spec, status, _, cooldown) in enumerate(players):
        box = (x, y, x + bar_w, y + row_h)
        draw.rounded_rectangle(box, radius=3, fill=(38, 42, 46, 235), outline=(60, 66, 72, 255))
        if cooldown > 0:
            draw.rounded_rectangle((x, y, x + int(bar_w * (1 - cooldown)), y + row_h), radius=3, fill=COOLDOWN + (200,))
        if index == up:
            draw.rounded_rectangle(box, radius=3, fill=(50, 150, 50, 120), outline=(120, 230, 120, 255), width=2)
            draw.rectangle((x, y, x + 7, y + row_h), fill=(120, 230, 120, 255))
        elif index == backup:
            draw.rectangle((x, y, x + 3, y + row_h), fill=(240, 170, 60, 230))
        if index == me:  # find yourself without reading
            draw.rectangle((x + bar_w - 3, y, x + bar_w, y + row_h), fill=(255, 230, 77, 220))

        image.alpha_composite(icon(profession, spec, 26), (x + 16, y + 6))
        draw.text((x + 52, y + 8), name, font=font(19, index == me), fill=YELLOW if index == me else TEXT)
        marker(draw, x + 248, y + 11, index, up, backup, 15)
        draw.text((x + 282, y + 10), status, font=font(16), fill=status_color(status))
        y += row_h + gap
    return image


def focus(players, up, backup, me, columns=3):
    """Layout 4: one card, then the queue in reading order, wrapping into lines."""
    queue = [(index, players[index]) for index in
             [(up + step) % len(players) for step in range(1, len(players))]]
    lines = (len(queue) + columns - 1) // columns
    card_w, cell_w, line_h = 390, 130, 20
    width, height = card_w + 60, 128 + lines * line_h
    image, draw = canvas(width, height)
    x, y = 20, 20
    window(draw, (x - 8, y - 8, x + card_w + 8, y + 86 + lines * line_h + 10))

    card = (x, y, x + card_w, y + 72)
    if me == up:
        draw.rounded_rectangle(card, radius=3, fill=(50, 150, 50, 110), outline=(120, 230, 120, 255), width=2)
        draw.text((x + 14, y + 4), "YOUR TURN", font=font(32, True), fill=GREEN)
        draw.text((x + 16, y + 46), f"backup: {players[backup][0]}", font=font(15), fill=(200, 230, 200))
    else:
        draw.rounded_rectangle(card, radius=3, fill=(40, 46, 52, 220), outline=(90, 96, 102, 255))
        draw.text((x + 14, y + 8), "UP", font=font(16, True), fill=GREEN)
        image.alpha_composite(icon(players[up][1], players[up][2], 30), (x + 46, y + 6))
        draw.text((x + 84, y + 2), players[up][0], font=font(30, True), fill=TEXT)
        second = f"backup: {players[backup][0]}"
        draw.text((x + 16, y + 46), second, font=font(15), fill=GREY)
        mine = f"you: {me + 1}. {players[me][3]}"
        draw.text((x + card_w - draw.textlength(mine, font=font(15, True)) - 14, y + 46), mine,
                  font=font(15, True), fill=YELLOW)

    y += 82
    draw.text((x, y), "queue", font=font(12), fill=GREY)
    y += 16
    for position, (index, player) in enumerate(queue):
        column, line = position % columns, position // columns
        left = x + column * cell_w
        top = y + line * line_h
        name, profession, spec, status, _, _ = player
        draw.text((left, top + 1), f"{index + 1}.", font=font(12), fill=ORANGE if index == backup else GREY)
        image.alpha_composite(icon(profession, spec, 14), (left + 18, top + 1))
        name_font = font(13, index == me)
        short = name
        while draw.textlength(short, font=name_font) > 52 and len(short) > 2:
            short = short[:-1]
        if short != name:
            short = short[:-1] + "."
        draw.text((left + 34, top), short, font=name_font, fill=YELLOW if index == me else TEXT)
        draw.text((left + 92, top + 1), status, font=font(12), fill=status_color(status))
    return image


def sheet(images, labels):
    pad, title_h = 16, 24
    width = max(image.width for image in images) + pad * 2
    height = sum(image.height + title_h for image in images) + pad * 2
    out, draw = canvas(width, height)
    y = pad
    for image, label in zip(images, labels):
        draw.text((pad, y), label, font=font(15, True), fill=TEXT)
        out.alpha_composite(image, (pad, y + title_h))
        y += image.height + title_h
    return out


if __name__ == "__main__":
    six = PLAYERS
    nine = MORE
    drawings = [
        ("1-compact-your-turn", "1. Compact list - your turn", compact(six, up=1, backup=5, me=1)),
        ("1-compact-someone-else", "1. Compact list - someone else is up", compact(six, up=0, backup=1, me=4)),
        ("2-big-bars-your-turn", "2. Big bars - your turn", big_bars(six, up=1, backup=5, me=1)),
        ("2-big-bars-someone-else", "2. Big bars - someone else is up (your row marked on the right)",
         big_bars(six, up=0, backup=1, me=4)),
        ("4-focus-your-turn", "4. Focus card - your turn", focus(six, up=1, backup=5, me=1)),
        ("4-focus-someone-else", "4. Focus card - someone else is up", focus(six, up=0, backup=1, me=4)),
        ("4-focus-nine-players", "4. Focus card - nine players, queue wraps to three lines",
         focus(nine, up=0, backup=1, me=4)),
    ]
    for name, _, image in drawings:
        image.convert("RGB").save(OUT / f"{name}.png")
    sheet([image for _, _, image in drawings], [label for _, label, _ in drawings]).convert("RGB").save(
        OUT / "all-layouts.png")
    print(f"wrote {len(drawings) + 1} files to {OUT}")
