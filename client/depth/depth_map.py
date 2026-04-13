#!/usr/bin/env python3
"""
Converts any sized input.png to a depth map output.png using Depth Anything model.
Usage: 
  python depth_map.py input.png output.png [options]

Options:
  --device {cpu,gpu,cuda}     Device to use (default: cpu)
  --precision {fp32,fp16,bf16} Numerical precision (default: fp32)
                               fp16/bf16 faster on GPU
  --model-size {small,base,large} Model size (default: large)
                               small=fastest, large=most accurate
  --num-threads N             CPU threads (default: -1 = all available)
  --compile                   Use torch.compile() optimization (PyTorch 2.0+)

Examples:
  # GPU with mixed precision (fastest)
  python depth_map.py input.png output.png --device gpu --precision fp16 --compile
  
  # CPU with small model (balanced)
  python depth_map.py input.png output.png --device cpu --model-size small
  
  # CPU with limited threads
  python depth_map.py input.png output.png --device cpu --num-threads 4
"""

import sys
import argparse
import time
from pathlib import Path
import cv2
import numpy as np
from PIL import Image
import torch
import os

try:
    from transformers import AutoImageProcessor, AutoModelForDepthEstimation
except ImportError:
    print("Installing required packages...")
    import subprocess
    subprocess.check_call([sys.executable, "-m", "pip", "install", "-q", 
                          "transformers", "torch", "torchvision", "opencv-python", "pillow"])
    from transformers import AutoImageProcessor, AutoModelForDepthEstimation


def convert_to_depth_map(input_path: str, output_path: str, device: str = "cpu", 
                        precision: str = "fp32", model_size: str = "large", 
                        num_threads: int = -1, compile: bool = False) -> None:
    """Convert an image to a depth map using Depth Anything model."""
    
    # Optimize CPU threading if using CPU
    if device.lower() == "cpu" and num_threads > 0:
        torch.set_num_threads(num_threads)
        os.environ["OMP_NUM_THREADS"] = str(num_threads)
        print(f"CPU threads set to: {num_threads}")
    
    input_path = Path(input_path)
    output_path = Path(output_path)
    
    # Remove output file if it already exists
    if output_path.exists():
        output_path.unlink()
    
    if not input_path.exists():
        raise FileNotFoundError(f"Input image not found: {input_path}")
    
    # Load image
    image = Image.open(input_path).convert('RGB')
    original_size = image.size
    
    print(f"Loading model...")
    # Validate and set device
    if device.lower() in ("gpu", "cuda"):
        if torch.cuda.is_available():
            device = "cuda"
        else:
            print("Warning: CUDA not available, falling back to CPU")
            device = "cpu"
    else:
        device = "cpu"
    print(f"Using device: {device}")
    
    # Map model size to HF model names
    model_variants = {
        "small": "depth-anything/Depth-Anything-V2-Small-hf",
        "base": "depth-anything/Depth-Anything-V2-Base-hf",
        "large": "depth-anything/Depth-Anything-V2-Large-hf"
    }
    model_name = model_variants.get(model_size.lower(), model_variants["large"])
    print(f"Model: {model_size.upper()} - {model_name}")
    
    # Load model and processor
    processor = AutoImageProcessor.from_pretrained(model_name)
    model = AutoModelForDepthEstimation.from_pretrained(model_name).to(device)
    model.eval()
    
    # Apply precision optimization
    if precision.lower() == "fp16" and device.lower() != "cpu":
        model = model.half()
        print(f"Using precision: float16")
    elif precision.lower() == "bf16" and device.lower() != "cpu" and torch.cuda.get_device_capability()[0] >= 8:
        model = model.bfloat16()
        print(f"Using precision: bfloat16")
    else:
        print(f"Using precision: float32")
    
    # Compile model if using PyTorch 2.0+
    if compile and hasattr(torch, "compile"):
        print("Compiling model (torch.compile)...")
        model = torch.compile(model)
    
    print(f"Processing image: {input_path} ({original_size[0]}x{original_size[1]})")
    
    # Start timer
    start_time = time.time()
    
    # Process image
    with torch.no_grad():
        inputs = processor(images=image, return_tensors="pt").to(device)
        
        # Apply precision to inputs if using fp16
        if precision.lower() == "fp16" and device.lower() != "cpu":
            inputs = {k: v.half() if v.dtype == torch.float32 else v for k, v in inputs.items()}
        elif precision.lower() == "bf16" and device.lower() != "cpu":
            inputs = {k: v.bfloat16() if v.dtype == torch.float32 else v for k, v in inputs.items()}
        
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
    
    # Calculate and display elapsed time
    elapsed_time = time.time() - start_time
    print(f"Time elapsed: {elapsed_time:.2f} seconds")
    print(f"Depth map saved: {output_path}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Convert image to depth map using Depth Anything V2 model")
    parser.add_argument("input", help="Path to input image")
    parser.add_argument("output", help="Path to output depth map")
    parser.add_argument("--device", default="cpu", choices=["cpu", "gpu", "cuda"],
                        help="Device to use for inference (default: cpu)")
    parser.add_argument("--precision", default="fp32", choices=["fp32", "fp16", "bf16"],
                        help="Numerical precision (default: fp32). fp16/bf16 faster on GPU, CPU uses fp32")
    parser.add_argument("--model-size", default="large", choices=["small", "base", "large"],
                        help="Model size (default: large). small=fastest, large=most accurate")
    parser.add_argument("--num-threads", type=int, default=-1,
                        help="CPU threads for inference (default: -1 = use all). Set 1-N to limit")
    parser.add_argument("--compile", action="store_true",
                        help="Use torch.compile() for optimization (requires PyTorch 2.0+, GPU recommended)")
    
    args = parser.parse_args()
    
    convert_to_depth_map(args.input, args.output, device=args.device, 
                        precision=args.precision, model_size=args.model_size,
                        num_threads=args.num_threads, compile=args.compile)
