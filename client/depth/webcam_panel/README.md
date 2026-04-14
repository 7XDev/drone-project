# Webcam Depth Panel

This directory contains a webcam-first version of `depth_map.py` with a fullscreen comparison panel.

## What it does

- Captures live webcam frames
- Runs Depth Anything V2 continuously
- Streams side-by-side output in a browser (left: webcam, right: black-and-white depth map)
- Shows live performance stats in a small black corner panel (FPS, inference time, frame time, resolution, device, precision)

## Run

From this folder:

```bash
python depth_map.py --device gpu --precision fp16 --model-size small --inference-width 256
```

Then open:

- http://127.0.0.1:5000

If CUDA is unavailable, it falls back to CPU automatically.

## Useful options

- `--device {cpu,gpu,cuda}`
- `--precision {fp32,fp16,bf16}`
- `--model-size {small,base,large}` (set at startup)
- `--camera-index N`
- `--camera-width N` and `--camera-height N`
- `--inference-width N` (lower is faster)
- `--target-capture-fps N`
- `--compile` (PyTorch 2.0+, can improve speed after warm-up)

## Near real-time tuning tips

1. Start with `small` model.
2. Use `--inference-width 224` or `256`.
3. Use `--device gpu --precision fp16` when available.
4. Keep camera resolution at `640x480` unless you need more detail.
5. If CPU-only, reduce inference width further and keep `small` model.
