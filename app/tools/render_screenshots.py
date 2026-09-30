#!/usr/bin/env python3
"""Generate high-resolution product screenshots of PlexusX panels."""
import os
import math
from PIL import Image, ImageDraw, ImageFont

W, H = 1280, 820
SIDE_W = 220
TOP_H = 56

C_BG       = (12, 12, 16)
C_SIDE     = (16, 16, 22)
C_CARD     = (22, 22, 30)
C_CARD2    = (30, 30, 42)
C_CARD_ACT = (36, 36, 52)
C_LINE     = (42, 42, 58)
C_TXT      = (242, 242, 248)
C_SUB      = (145, 145, 160)
C_DIM      = (95, 95, 110)
C_ACC      = (198, 255, 61)     # Lime
C_ACC2     = (79, 227, 255)     # Cyan
C_DARK     = (10, 10, 14)

def get_font(size, bold=False):
    # Try standard system fonts or default
    paths = [
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"
    ]
    for p in paths:
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()

font_logo = get_font(18, bold=True)
font_h1   = get_font(22, bold=True)
font_h2   = get_font(15, bold=True)
font_body = get_font(13, bold=False)
font_sm   = get_font(11, bold=False)
font_mono = get_font(13, bold=True)

def draw_base_frame(draw, active_tab_idx=0):
    # Background
    draw.rectangle([0, 0, W, H], fill=C_BG)
    # Sidebar
    draw.rectangle([0, 0, SIDE_W, H], fill=C_SIDE)
    # Separators
    draw.line([(SIDE_W, 0), (SIDE_W, H)], fill=C_LINE, width=1)
    draw.line([(0, TOP_H), (W, TOP_H)], fill=C_LINE, width=1)

    # Logo
    draw.rounded_rectangle([18, 14, 48, 44], radius=6, fill=C_ACC)
    draw.line([(25, 21), (41, 37)], fill=C_DARK, width=3)
    draw.line([(41, 21), (25, 37)], fill=C_DARK, width=3)
    draw.text((56, 14), "PLEXUSX", fill=C_TXT, font=font_logo)
    draw.text((56, 34), "DISPLAY OPTIMIZER", fill=C_SUB, font=font_sm)

    # Window controls
    draw.text((W - 90, 16), "-", fill=C_SUB, font=font_h1)
    draw.text((W - 50, 16), "✕", fill=C_SUB, font=font_h2)

    # Nav items
    nav = ["Dashboard", "Games", "Display", "Color Engine", "Presets",
           "Crosshair", "Monitors", "Automation", "Tools", "Settings"]
    for i, item in enumerate(nav):
        y = 84 + i * 46
        is_active = (i == active_tab_idx)
        if is_active:
            draw.rounded_rectangle([14, y, SIDE_W - 14, y + 40], radius=6, fill=C_CARD2)
            draw.rounded_rectangle([18, y + 8, 22, y + 32], radius=2, fill=C_ACC)
            draw.text((36, y + 10), item, fill=C_ACC, font=font_h2)
        else:
            draw.text((36, y + 10), item, fill=C_SUB, font=font_h2)

    # Bottom hardware status
    draw.rounded_rectangle([16, H - 90, SIDE_W - 16, H - 16], radius=8, fill=C_CARD, outline=C_LINE)
    draw.text((26, H - 82), "NVIDIA GeForce RTX", fill=C_ACC, font=font_sm)
    draw.text((26, H - 64), "2560x1440 @ 280Hz", fill=C_TXT, font=font_sm)
    draw.text((26, H - 46), "● Active · No Hooking", fill=C_SUB, font=font_sm)

def render_home(out_path):
    img = Image.new("RGB", (W, H), C_BG)
    draw = ImageDraw.Draw(img)
    draw_base_frame(draw, 0)

    # Header
    draw.text((248, 80), "Gaming Display Dashboard", fill=C_TXT, font=font_h1)
    draw.text((248, 118), "Active foreground status, display parameters & instant game tuning", fill=C_SUB, font=font_body)

    # Game card
    draw.rounded_rectangle([248, 150, 728, 250], radius=8, fill=C_CARD, outline=C_LINE)
    draw.text((264, 162), "CURRENT FOREGROUND", fill=C_DIM, font=font_sm)
    draw.text((264, 180), "Rust", fill=C_TXT, font=font_h1)
    draw.text((264, 218), "Active Profile: Competitive  ·  Survival FPS", fill=C_SUB, font=font_sm)

    # Hardware card
    draw.rounded_rectangle([744, 150, 1234, 250], radius=8, fill=C_CARD, outline=C_LINE)
    draw.text((760, 162), "ACTIVE DISPLAY HARDWARE", fill=C_DIM, font=font_sm)
    draw.text((760, 180), "2560 × 1440 @ 280 Hz", fill=C_TXT, font=font_h1)
    draw.text((760, 218), "NVIDIA  ·  Native Resolution", fill=C_SUB, font=font_sm)

    # Color engine card
    draw.rounded_rectangle([248, 266, 1234, 342], radius=8, fill=C_CARD, outline=C_LINE)
    draw.text((264, 276), "ACTIVE COLOR PROFILE ENGINE", fill=C_DIM, font=font_sm)
    draw.text((264, 294), "Sat: 245%   Vib: 200%   Bri: 106%   Con: 116%   Gamma: 0.94   Temp: 6400K", fill=C_TXT, font=font_h2)
    draw.text((264, 318), "● Engine Active · Magnification API + GPU Gamma Ramps", fill=C_ACC, font=font_sm)

    # Quick actions
    draw.text((248, 356), "ONE-CLICK QUICK MODES", fill=C_DIM, font=font_sm)
    draw.line([(390, 362), (1234, 362)], fill=C_CARD2, width=1)

    modes = ["Competitive", "300% Vibrance", "Night Visibility", "Cinematic", "Natural", "Reset All"]
    for i, m in enumerate(modes):
        x = 248 + i * 164
        fill = C_ACC if i == 0 else C_CARD2
        tc = C_DARK if i == 0 else C_TXT
        draw.rounded_rectangle([x, 386, x + 150, 426], radius=6, fill=fill, outline=C_LINE)
        draw.text((x + 24, 398), m, fill=tc, font=font_h2)

    # Live preview section
    draw.text((248, 442), "LIVE VISUAL PREVIEW (DRAG DIVIDER TO COMPARE)", fill=C_DIM, font=font_sm)
    draw.line([(550, 448), (1234, 448)], fill=C_CARD2, width=1)

    px, py, pw, ph = 248, 472, 986, 250
    # Left side (Neutral)
    draw.rectangle([px, py, px + pw // 2, py + ph], fill=(45, 60, 85))
    draw.ellipse([px + 80, py + 30, px + 140, py + 90], fill=(210, 190, 140))
    draw.polygon([(px, py + ph - 40), (px + pw // 4, py + 70), (px + pw // 2, py + ph - 40)], fill=(35, 45, 55))
    draw.rectangle([px, py + ph - 60, px + pw // 2, py + ph], fill=(30, 50, 40))
    draw.text((px + 16, py + 16), "BEFORE (NEUTRAL)", fill=(200, 200, 210), font=font_sm)

    # Right side (PlexusX Tuned)
    draw.rectangle([px + pw // 2, py, px + pw, py + ph], fill=(25, 75, 145))
    draw.ellipse([px + 80, py + 30, px + 140, py + 90], fill=(255, 230, 120))
    draw.polygon([(px + pw // 4, py + 70), (px + (3 * pw) // 4, py + 70), (px + pw, py + ph - 40)], fill=(25, 50, 85))
    draw.rectangle([px + pw // 2, py + ph - 60, px + pw, py + ph], fill=(20, 85, 45))
    draw.text((px + pw - 140, py + 16), "AFTER (PLEXUSX)", fill=C_ACC, font=font_sm)

    # Divider
    draw.line([(px + pw // 2, py), (px + pw // 2, py + ph)], fill=C_ACC, width=2)
    draw.ellipse([px + pw // 2 - 12, py + ph // 2 - 12, px + pw // 2 + 12, py + ph // 2 + 12], fill=C_ACC)

    # Outer border
    draw.rectangle([px, py, px + pw, py + ph], outline=C_LINE)

    # Bottom buttons
    draw.rounded_rectangle([248, 746, 468, 782], radius=6, fill=C_CARD2, outline=C_LINE)
    draw.text((264, 756), "★ Gaming Mode: ACTIVE", fill=C_ACC, font=font_sm)
    draw.rounded_rectangle([480, 746, 680, 782], radius=6, fill=C_CARD, outline=C_LINE)
    draw.text((496, 756), "Backup Display State", fill=C_TXT, font=font_sm)

    img.save(out_path)
    print(f"Saved {out_path}")

def render_color(out_path):
    img = Image.new("RGB", (W, H), C_BG)
    draw = ImageDraw.Draw(img)
    draw_base_frame(draw, 3) # Color tab

    draw.text((248, 70), "Global Color Engine", fill=C_TXT, font=font_h1)
    draw.text((248, 106), "Hardware-accelerated saturation, vibrance, gamma curve, shadows and tone controls.", fill=C_SUB, font=font_body)

    # Quick saturation buttons
    draw.text((560, 142), "Quick Saturation:", fill=C_SUB, font=font_body)
    sats = ["100%", "150%", "200%", "250%", "300%"]
    for i, s in enumerate(sats):
        x = 710 + i * 72
        fill = C_ACC if i == 2 else C_CARD
        tc = C_DARK if i == 2 else C_TXT
        draw.rounded_rectangle([x, 136, x + 64, 168], radius=6, fill=fill, outline=C_LINE)
        draw.text((x + 14, 144), s, fill=tc, font=font_sm)

    # Sliders left column
    sliders_l = [
        ("Saturation (0–300%)", "200%", 0.66),
        ("Vibrance (Smart Saturation)", "160%", 0.53),
        ("Brightness", "105%", 0.52),
        ("Contrast", "112%", 0.56),
        ("Shadows (Toe Lift / Crush)", "135%", 0.67),
        ("Highlights (Shoulder Compress)", "95%", 0.47),
        ("Clarity / Dehaze S-Curve", "120%", 0.60)
    ]
    for i, (name, val, pos) in enumerate(sliders_l):
        y = 186 + i * 74
        draw.text((248, y), name, fill=C_TXT, font=font_body)
        draw.text((670, y), val, fill=C_ACC, font=font_mono)
        # Track
        draw.rounded_rectangle([248, y + 36, 718, y + 42], radius=3, fill=C_CARD2)
        # Fill
        draw.rounded_rectangle([248, y + 36, int(248 + 470 * pos), y + 42], radius=3, fill=C_ACC)
        # Thumb
        tx = int(248 + 470 * pos)
        draw.ellipse([tx - 8, y + 39 - 8, tx + 8, y + 39 + 8], fill=(255, 255, 255))

    # Sliders right column
    sliders_r = [
        ("GPU Gamma Ramp", "0.95", 0.45),
        ("Color Temperature (Kelvin)", "6500K", 0.50),
        ("Tint (Green / Magenta)", "0%", 0.50),
        ("Red Channel", "100%", 0.50),
        ("Black Level Floor", "100%", 0.50),
    ]
    for i, (name, val, pos) in enumerate(sliders_r):
        y = 186 + i * 74
        draw.text((740, y), name, fill=C_TXT, font=font_body)
        draw.text((1170, y), val, fill=C_ACC, font=font_mono)
        draw.rounded_rectangle([740, y + 36, 1210, y + 42], radius=3, fill=C_CARD2)
        draw.rounded_rectangle([740, y + 36, int(740 + 470 * pos), y + 42], radius=3, fill=C_ACC)
        tx = int(740 + 470 * pos)
        draw.ellipse([tx - 8, y + 39 - 8, tx + 8, y + 39 + 8], fill=(255, 255, 255))

    # Curve preview
    draw.rounded_rectangle([740, 560, 1210, 698], radius=8, fill=C_CARD, outline=C_LINE)
    draw.text((752, 570), "Hardware Gamma Ramp Curve Preview", fill=C_SUB, font=font_sm)
    pts = []
    for x in range(752, 1198):
        norm = (x - 752) / (1198 - 752)
        y = 688 - int((norm ** 0.95) * 100)
        pts.append((x, y))
    for idx in range(len(pts) - 1):
        draw.line([pts[idx], pts[idx + 1]], fill=C_ACC, width=2)

    # Actions
    actions = ["Reset All", "Save as Preset", "Copy Preset", "Export JSON", "Import JSON"]
    for i, act in enumerate(actions):
        x = 248 + i * 194
        fill = C_ACC if i == 0 else C_CARD
        tc = C_DARK if i == 0 else C_TXT
        draw.rounded_rectangle([x, 726, x + 180, 770], radius=6, fill=fill, outline=C_LINE)
        draw.text((x + 28, 740), act, fill=tc, font=font_h2)

    img.save(out_path)
    print(f"Saved {out_path}")

def render_games(out_path):
    img = Image.new("RGB", (W, H), C_BG)
    draw = ImageDraw.Draw(img)
    draw_base_frame(draw, 1) # Games tab

    draw.text((248, 80), "Game Profiles Library", fill=C_TXT, font=font_h1)
    draw.text((248, 118), "Dedicated starting looks per game. Automatically switches when game launches.", fill=C_SUB, font=font_body)

    games = [
        ("Rust", "RustClient.exe", "Survival FPS", "Competitive (11 modes)", True),
        ("Counter-Strike 2", "cs2.exe", "Tactical Shooter", "Competitive (6 modes)", False),
        ("Fortnite", "FortniteClient-Win64-Shipping.exe", "Battle Royale", "Colorful (6 modes)", False),
        ("Valorant", "VALORANT-Win64-Shipping.exe", "Tactical Shooter", "High Contrast (4 modes)", False),
        ("Escape From Tarkov", "EscapeFromTarkov.exe", "Hardcore Extraction", "Dark Room (5 modes)", False),
        ("PUBG: BATTLEGROUNDS", "TslGame.exe", "Battle Royale", "Sunny (4 modes)", False),
        ("Call of Duty: Warzone", "cod.exe", "FPS / Battle Royale", "Gulag Visibility (3 modes)", False)
    ]

    for i, (name, exe, tag, mode, active) in enumerate(games):
        y = 160 + i * 72
        fill = C_CARD_ACT if active else C_CARD
        outline = C_ACC if active else C_LINE
        draw.rounded_rectangle([248, y, 1234, y + 60], radius=8, fill=fill, outline=outline)
        draw.text((264, y + 10), name, fill=C_ACC if active else C_TXT, font=font_h2)
        draw.text((264, y + 34), f"{exe}  ·  {tag}", fill=C_SUB, font=font_sm)
        draw.text((980, y + 18), mode, fill=C_ACC2, font=font_body)

    img.save(out_path)
    print(f"Saved {out_path}")

def render_crosshair(out_path):
    img = Image.new("RGB", (W, H), C_BG)
    draw = ImageDraw.Draw(img)
    draw_base_frame(draw, 5) # Crosshair tab

    draw.text((248, 80), "Desktop Overlay Crosshair", fill=C_TXT, font=font_h1)
    draw.text((248, 118), "Layered click-through overlay. Zero game file modifications or injection.", fill=C_SUB, font=font_body)

    # Shapes
    shapes = ["Dot", "Cross", "Circle", "Square", "Plus", "Chevron", "T", "T-Type"]
    for i, s in enumerate(shapes):
        x = 248 + i * 122
        fill = C_CARD_ACT if i == 1 else C_CARD
        outline = C_ACC if i == 1 else C_LINE
        draw.rounded_rectangle([x, 180, x + 114, 256], radius=6, fill=fill, outline=outline)
        draw.text((x + 36, 230), s, fill=C_ACC if i == 1 else C_SUB, font=font_sm)
        # Draw sample glyph
        cx, cy = x + 57, 205
        if s in ("Cross", "Plus"):
            draw.line([(cx - 12, cy), (cx + 12, cy)], fill=C_ACC, width=2)
            draw.line([(cx, cy - 12), (cx, cy + 12)], fill=C_ACC, width=2)
        elif s == "Dot":
            draw.ellipse([cx - 4, cy - 4, cx + 4, cy + 4], fill=C_ACC)
        elif s == "Circle":
            draw.ellipse([cx - 10, cy - 10, cx + 10, cy + 10], outline=C_ACC, width=2)

    # Crosshair controls
    controls = [
        ("Size", "16px", 0.3),
        ("Center Gap", "4px", 0.15),
        ("Thickness", "2px", 0.2),
        ("Opacity", "100%", 1.0)
    ]
    for i, (name, val, pos) in enumerate(controls):
        x = 248 if i % 2 == 0 else 740
        y = 280 + (i // 2) * 80
        draw.text((x, y), name, fill=C_TXT, font=font_body)
        draw.text((x + 420, y), val, fill=C_ACC, font=font_mono)
        draw.rounded_rectangle([x, y + 36, x + 470, y + 42], radius=3, fill=C_CARD2)
        draw.rounded_rectangle([x, y + 36, int(x + 470 * pos), y + 42], radius=3, fill=C_ACC)
        tx = int(x + 470 * pos)
        draw.ellipse([tx - 8, y + 39 - 8, tx + 8, y + 39 + 8], fill=(255, 255, 255))

    img.save(out_path)
    print(f"Saved {out_path}")

def main():
    os.makedirs("site/media", exist_ok=True)
    render_home("site/media/screenshot-home.png")
    render_color("site/media/screenshot-color.png")
    render_games("site/media/screenshot-games.png")
    render_crosshair("site/media/screenshot-crosshair.png")
    print("All screenshots generated successfully!")

if __name__ == "__main__":
    main()
