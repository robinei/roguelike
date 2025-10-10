#!/usr/bin/env python3
"""
Combines a tileset with a font atlas by adding two rows to the tileset
and filling them with glyphs from the font atlas.
"""

import sys
from PIL import Image

if len(sys.argv) != 2:
    print("Usage: combine_atlases.py <output.png>")
    sys.exit(1)

output_path = sys.argv[1]

# Configuration
TILE_SIZE = 12
SPACING = 1  # 1px transparent between tiles
FONT_GRID = 16  # 16x16 font atlas
ROWS_TO_ADD = 2

# Load images
tileset = Image.open('urizen_onebit_tileset__v2d0.png')
font = Image.open('cp437_12x12.png')

# Calculate tileset dimensions
tileset_width = tileset.width
tiles_per_row = (tileset_width + SPACING) // (TILE_SIZE + SPACING)

# Calculate new height (add 2 rows with spacing + 1px bottom border)
new_height = tileset.height + (ROWS_TO_ADD * (TILE_SIZE + SPACING)) + SPACING

# Create new image with transparency
combined = Image.new('RGBA', (tileset_width, new_height), (0, 0, 0, 0))

# Paste original tileset
combined.paste(tileset, (0, 0))

# Starting Y position for new rows
start_y = tileset.height + SPACING

# Copy glyphs from font atlas to new rows
glyph_index = 0
for row in range(ROWS_TO_ADD):
    for col in range(tiles_per_row):
        if glyph_index >= FONT_GRID * FONT_GRID:
            break

        # Calculate position in font atlas (16x16 grid, no spacing)
        font_x = (glyph_index % FONT_GRID) * TILE_SIZE
        font_y = (glyph_index // FONT_GRID) * TILE_SIZE

        # Extract glyph
        glyph = font.crop((font_x, font_y, font_x + TILE_SIZE, font_y + TILE_SIZE))

        # Calculate position in combined image (with spacing + 1px border on left/bottom)
        dest_x = SPACING + col * (TILE_SIZE + SPACING)
        dest_y = start_y + row * (TILE_SIZE + SPACING)

        # Paste glyph
        combined.paste(glyph, (dest_x, dest_y))

        glyph_index += 1

# Make the very last tile (bottom right) completely white
last_col = tiles_per_row - 1
last_row = ROWS_TO_ADD - 1
last_x = SPACING + last_col * (TILE_SIZE + SPACING)
last_y = start_y + last_row * (TILE_SIZE + SPACING)
white_tile = Image.new('RGBA', (TILE_SIZE, TILE_SIZE), (255, 255, 255, 255))
combined.paste(white_tile, (last_x, last_y))

# Save result
combined.save(output_path)
print(f"Created {output_path} ({combined.width}x{combined.height})")
print(f"Added {glyph_index} glyphs from font atlas in {ROWS_TO_ADD} new rows")
