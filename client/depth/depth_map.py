#!/usr/bin/env python3
"""
Converts any sized input.png to a depth map output.png using Depth Anything model.
Usage: python depth_map.py input.png output.png
"""

import sys
from pathlib import Path
import cv2
import numpy as np
from PIL import Image
import torch

try:
    from transformers import AutoImageProcessor, AutoModelForDepthEstimation
except ImportError:
    print("Installing required packages...")
    import subprocess
    subprocess.check_call([sys.executable, "-m", "pip", "install", "-q", 
                          "transformers", "torch", "torchvision", "opencv-python", "pillow"])
    from transformers import AutoImageProcessor, AutoModelForDepthEstimation


def convert_to_depth_map(input_path: str, output_path: str) -> None:
    """Convert an image to a depth map using Depth Anything model."""
    
    input_path = Path(input_path)
    output_path = Path(output_path)
    
    if not input_path.exists():
        raise FileNotFoundError(f"Input image not found: {input_path}")
    
    # Load image
    image = Image.open(input_path).convert('RGB')
    original_size = image.size
    
    print(f"Loading model...")
    device = "cuda" if torch.cuda.is_available() else "cpu"
    print(f"Using device: {device}")
    
    # Load model and processor
    model_name = "depth-anything/Depth-Anything-V2-Large-hf"
    processor = AutoImageProcessor.from_pretrained(model_name)
    model = AutoModelForDepthEstimation.from_pretrained(model_name).to(device)
    model.eval()
    
    print(f"Processing image: {input_path} ({original_size[0]}x{original_size[1]})")
    
    # Process image
    with torch.no_grad():
        inputs = processor(images=image, return_tensors="pt").to(device)
        outputs = model(**inputs)
        predicted_depth = outputs.predicted_depth
    
    # Convert to numpy and normalize
    depth_numpy = predicted_depth.squeeze().cpu().numpy()
    depth_normalized = (depth_numpy - depth_numpy.min()) / (depth_numpy.max() - depth_numpy.min() + 1e-8)
    depth_normalized = (depth_normalized * 255).astype(np.uint8)
    
    # Resize to original image size
    depth_resized = cv2.resize(depth_normalized, original_size, interpolation=cv2.INTER_LINEAR)
    
    # Save depth map
    Image.fromarray(depth_resized).save(output_path)
    print(f"Depth map saved: {output_path}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python depth_map.py input.png output.png")
        sys.exit(1)
    
    convert_to_depth_map(sys.argv[1], sys.argv[2])
