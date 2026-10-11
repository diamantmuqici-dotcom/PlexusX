#!/usr/bin/env python3
# ARCHIVED ONE-OFF — DO NOT RE-RUN.
#
# Historical record of the bulk preset retune that clamped every scene / game
# sub-mode in app/src/games/game_preset_manager.c to saturation <= 180 and
# vibrance <= 150 (and forced "Natural" modes to true neutral).  The result of
# this migration is already committed in the source table.
#
# Re-running it now would be WRONG: it would flatten deliberate outliers such as
# the "Vivid 300%" showcase preset (300/240 by design, parity-checked against
# site/assets/presets.js) and it knows nothing about the scene<->seed
# consistency rule enforced by tests/test_all.c.  Kept under scripts/ only to
# document where the current preset values came from.
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
