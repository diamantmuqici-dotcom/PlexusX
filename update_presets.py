import re

with open('app/src/games/game_preset_manager.c', 'r') as f:
    content = f.read()

def clamp_colors(match):
    # (Look){ 1, sat, vib, bri, con, gamma, temp, tint, r, g, b, shadows, highlights, black, white, clarity, hue }
    # Group 1: prefix
    # Group 2: sat
    # Group 3: vib
    prefix = match.group(1)
    sat = int(match.group(2))
    vib = int(match.group(3))
    
    # clamp logic
    if sat > 180: sat = 180
    if vib > 150: vib = 150
    
    # ensure natural is really neutral
    if "Natural" in prefix or "Default" in prefix:
        if "Neutralizer" not in prefix: # skip "IPS Neutralizer" etc.
            if "Natural" in prefix:
                sat = 100; vib = 100
    
    return f"{prefix}{sat}, {vib},"

# Replace in Look structs
content = re.compile(r'(\{ 1,\s*)(\d+),\s*(\d+),').sub(clamp_colors, content)

with open('app/src/games/game_preset_manager.c', 'w') as f:
    f.write(content)
