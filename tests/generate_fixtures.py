#!/usr/bin/env python3
"""
generate_fixtures.py - Generate test fixtures for liblamina tests
Creates simple PNG images for testing textures, fonts, and tilemaps.
"""

import sys

try:
    from PIL import Image, ImageDraw
except ImportError:
    print("Error: PIL (Pillow) is required. Install with: pip install Pillow")
    sys.exit(1)

def create_test_texture():
    """Create a 64x64 test pattern with colored quadrants"""
    img = Image.new('RGBA', (64, 64), (0, 0, 0, 255))
    draw = ImageDraw.Draw(img)
    
    # Four colored quadrants
    draw.rectangle([0, 0, 31, 31], fill=(255, 0, 0, 255))  # Red
    draw.rectangle([32, 0, 63, 31], fill=(0, 255, 0, 255))  # Green
    draw.rectangle([0, 32, 31, 63], fill=(0, 0, 255, 255))  # Blue
    draw.rectangle([32, 32, 63, 63], fill=(255, 255, 0, 255))  # Yellow
    
    # Add a white border
    draw.rectangle([0, 0, 63, 63], outline=(255, 255, 255, 255), width=2)
    
    img.save('tests/fixtures/test_texture.png')
    print("Created: tests/fixtures/test_texture.png")

def create_test_font():
    """Create a simple 8x8 ASCII bitmap font"""
    # 16x16 grid of 8x8 characters for ASCII 0-255
    img = Image.new('RGBA', (128, 128), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    
    # Draw a simple representation for printable ASCII (32-126)
    for i in range(32, 127):
        x = (i % 16) * 8
        y = (i // 16) * 8
        
        # Draw a simple pixel pattern representing the character
        # For testing, just draw some unique pattern per character
        char_pattern = i % 8
        
        if char_pattern == 0:  # Space and multiples of 8
            pass  # Leave empty
        elif char_pattern == 1:
            draw.rectangle([x+2, y+2, x+5, y+5], fill=(255, 255, 255, 255))
        elif char_pattern == 2:
            draw.line([x+1, y+1, x+6, y+6], fill=(255, 255, 255, 255))
        elif char_pattern == 3:
            draw.line([x+6, y+1, x+1, y+6], fill=(255, 255, 255, 255))
        elif char_pattern == 4:
            draw.rectangle([x+1, y+1, x+6, y+6], outline=(255, 255, 255, 255))
        elif char_pattern == 5:
            draw.rectangle([x+2, y+2, x+5, y+5], fill=(255, 255, 255, 255))
        elif char_pattern == 6:
            draw.line([x+3, y+1, x+3, y+6], fill=(255, 255, 255, 255))
        else:
            draw.line([x+1, y+3, x+6, y+3], fill=(255, 255, 255, 255))
    
    img.save('tests/fixtures/test_font.png')
    print("Created: tests/fixtures/test_font.png")

def create_test_tilemap():
    """Create a 128x128 tilemap with 16x16 tiles (8x8 grid)"""
    img = Image.new('RGBA', (128, 128), (0, 0, 0, 255))
    draw = ImageDraw.Draw(img)
    
    colors = [
        (255, 0, 0, 255),    # Red
        (0, 255, 0, 255),    # Green
        (0, 0, 255, 255),    # Blue
        (255, 255, 0, 255),  # Yellow
        (255, 0, 255, 255),  # Magenta
        (0, 255, 255, 255),  # Cyan
        (128, 128, 128, 255),# Gray
        (255, 255, 255, 255),# White
    ]
    
    # Create 8x8 grid of 16x16 tiles
    for ty in range(8):
        for tx in range(8):
            x = tx * 16
            y = ty * 16
            color_idx = (tx + ty * 8) % len(colors)
            
            # Fill tile
            draw.rectangle([x, y, x+15, y+15], fill=colors[color_idx])
            
            # Add border
            draw.rectangle([x, y, x+15, y+15], outline=(0, 0, 0, 255))
            
            # Add diagonal line for identification
            draw.line([x, y, x+15, y+15], fill=(0, 0, 0, 128))
    
    img.save('tests/fixtures/test_tilemap.png')
    print("Created: tests/fixtures/test_tilemap.png")

def create_small_texture():
    """Create a 16x16 small test texture"""
    img = Image.new('RGBA', (16, 16), (0, 0, 0, 255))
    draw = ImageDraw.Draw(img)
    
    # Checkerboard pattern
    for y in range(16):
        for x in range(16):
            if (x + y) % 2 == 0:
                draw.point([x, y], fill=(255, 255, 255, 255))
    
    img.save('tests/fixtures/test_small.png')
    print("Created: tests/fixtures/test_small.png")

if __name__ == '__main__':
    print("Generating liblamina test fixtures...")
    create_test_texture()
    create_test_font()
    create_test_tilemap()
    create_small_texture()
    print("All fixtures generated successfully!")
