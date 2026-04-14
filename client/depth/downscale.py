#!/usr/bin/env python3
"""
Downscales any image to 320 width with matching aspect ratio.
Usage: python downscale.py input.png output.png
"""

import sys
from pathlib import Path
from PIL import Image


def downscale(input_path: str, output_path: str) -> None:
    """Downscale image to 320 width preserving aspect ratio."""
    
    input_path = Path(input_path)
    output_path = Path(output_path)
    
    if not input_path.exists():
        raise FileNotFoundError(f"Input image not found: {input_path}")
    
    # Load image
    image = Image.open(input_path).convert('RGB')
    original_size = image.size
    
    # Calculate new height maintaining aspect ratio
    aspect_ratio = original_size[1] / original_size[0]
    new_height = int(180 * aspect_ratio)
    
    print(f"Downscaling: {original_size[0]}x{original_size[1]} → 320x{new_height}")
    
    # Downscale
    image_resized = image.resize((180, new_height), Image.Resampling.LANCZOS)
    
    # Save
    image_resized.save(output_path)
    print(f"Saved: {output_path}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python downscale.py input.png output.png")
        sys.exit(1)
    
    downscale(sys.argv[1], sys.argv[2])
