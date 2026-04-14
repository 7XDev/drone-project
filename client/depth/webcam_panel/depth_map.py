#!/usr/bin/env python3
"""
Real-time webcam depth mapping with a local web panel.

Usage:
  python depth_map.py [options]

Examples:
  # Fastest practical setup (GPU + small model + lower inference width)
  python depth_map.py --device gpu --precision fp16 --model-size small --inference-width 256

  # CPU fallback (lower resolution for responsiveness)
  python depth_map.py --device cpu --model-size small --camera-width 640 --camera-height 480 --inference-width 224
"""

import argparse
import os
import sys
import threading
import time
from collections import deque
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Optional, Tuple

import cv2
import numpy as np
from PIL import Image
import torch

try:
    from flask import Flask, Response, jsonify, render_template
    from transformers import AutoImageProcessor, AutoModelForDepthEstimation
except ImportError:
    print("Installing required packages...")
    import subprocess

    subprocess.check_call(
        [
            sys.executable,
            "-m",
            "pip",
            "install",
            "-q",
            "flask",
            "transformers",
            "torch",
            "torchvision",
            "opencv-python",
            "pillow",
            "numpy",
        ]
    )
    from flask import Flask, Response, jsonify, render_template
    from transformers import AutoImageProcessor, AutoModelForDepthEstimation


MODEL_VARIANTS = {
    "small": "depth-anything/Depth-Anything-V2-Small-hf",
    "base": "depth-anything/Depth-Anything-V2-Base-hf",
    "large": "depth-anything/Depth-Anything-V2-Large-hf",
}


@dataclass
class RuntimeConfig:
    device: str
    precision: str
    model_size: str
    camera_index: int
    camera_width: int
    camera_height: int
    inference_width: int
    num_threads: int
    compile_model: bool
    jpeg_quality: int
    target_capture_fps: int


class SharedState:
    def __init__(self, initial_model_size: str) -> None:
        self.lock = threading.Lock()
        self.running = True

        self.latest_frame: Optional[np.ndarray] = None
        self.latest_jpeg: Optional[bytes] = None

        self.start_time = time.time()
        self.processed_frames = 0
        self.inference_ms_samples = deque(maxlen=60)
        self.frame_ms_samples = deque(maxlen=60)

        self.capture_resolution = "-"
        self.inference_resolution = "-"

        self.current_model_size = initial_model_size
        self.current_precision = "fp32"
        self.current_device = "cpu"

        self.model_loading = True
        self.last_error = ""

    def set_latest_frame(self, frame: np.ndarray) -> None:
        with self.lock:
            self.latest_frame = frame
            self.capture_resolution = f"{frame.shape[1]}x{frame.shape[0]}"

    def get_latest_frame_copy(self) -> Optional[np.ndarray]:
        with self.lock:
            if self.latest_frame is None:
                return None
            return self.latest_frame.copy()

    def set_latest_output(
        self,
        jpeg_bytes: bytes,
        inference_size: Tuple[int, int],
        inference_ms: float,
        frame_ms: float,
        model_size: str,
        precision: str,
        device: str,
    ) -> None:
        with self.lock:
            self.latest_jpeg = jpeg_bytes
            self.inference_resolution = f"{inference_size[0]}x{inference_size[1]}"
            self.inference_ms_samples.append(inference_ms)
            self.frame_ms_samples.append(frame_ms)
            self.processed_frames += 1
            self.current_model_size = model_size
            self.current_precision = precision
            self.current_device = device

    def get_latest_jpeg(self) -> Optional[bytes]:
        with self.lock:
            return self.latest_jpeg

    def set_model_loading(self, loading: bool) -> None:
        with self.lock:
            self.model_loading = loading

    def set_error(self, error: str) -> None:
        with self.lock:
            self.last_error = error

    def stats_snapshot(self) -> Dict[str, object]:
        with self.lock:
            avg_inference_ms = (
                sum(self.inference_ms_samples) / len(self.inference_ms_samples)
                if self.inference_ms_samples
                else 0.0
            )
            avg_frame_ms = (
                sum(self.frame_ms_samples) / len(self.frame_ms_samples)
                if self.frame_ms_samples
                else 0.0
            )
            fps = 1000.0 / avg_frame_ms if avg_frame_ms > 0 else 0.0

            return {
                "processed_frames": self.processed_frames,
                "uptime_seconds": time.time() - self.start_time,
                "fps": fps,
                "avg_inference_ms": avg_inference_ms,
                "avg_frame_ms": avg_frame_ms,
                "capture_resolution": self.capture_resolution,
                "inference_resolution": self.inference_resolution,
                "current_model_size": self.current_model_size,
                "precision": self.current_precision,
                "device": self.current_device,
                "model_loading": self.model_loading,
                "last_error": self.last_error,
            }


class DepthModelRunner:
    def __init__(self, config: RuntimeConfig, state: SharedState) -> None:
        self.config = config
        self.state = state
        self.model_lock = threading.Lock()

        self.device = self._resolve_device(config.device)
        self.processor = None
        self.model = None
        self.model_size = config.model_size
        self.actual_precision = "fp32"

        if self.device == "cuda":
            torch.backends.cudnn.benchmark = True
            if hasattr(torch, "set_float32_matmul_precision"):
                torch.set_float32_matmul_precision("high")

        if self.device == "cpu" and config.num_threads > 0:
            torch.set_num_threads(config.num_threads)
            os.environ["OMP_NUM_THREADS"] = str(config.num_threads)
            print(f"CPU threads set to: {config.num_threads}")

    @staticmethod
    def _resolve_device(requested_device: str) -> str:
        if requested_device.lower() in ("gpu", "cuda"):
            if torch.cuda.is_available():
                return "cuda"
            print("Warning: CUDA not available, falling back to CPU")
        return "cpu"

    def load_model(self, model_size: str) -> None:
        model_size = model_size.lower()
        if model_size not in MODEL_VARIANTS:
            raise ValueError(f"Unsupported model size: {model_size}")

        self.state.set_model_loading(True)
        model_name = MODEL_VARIANTS[model_size]
        print(f"Loading model: {model_size.upper()} - {model_name}")

        processor = AutoImageProcessor.from_pretrained(model_name)
        model = AutoModelForDepthEstimation.from_pretrained(model_name).to(self.device)
        model.eval()

        actual_precision = "fp32"
        if self.config.precision == "fp16" and self.device == "cuda":
            model = model.half()
            actual_precision = "fp16"
        elif self.config.precision == "bf16" and self.device == "cuda":
            capability_major = torch.cuda.get_device_capability()[0]
            if capability_major >= 8:
                model = model.bfloat16()
                actual_precision = "bf16"
            else:
                print("Warning: bf16 unsupported on this GPU, using fp32")

        if self.config.compile_model and hasattr(torch, "compile"):
            print("Compiling model (torch.compile)...")
            model = torch.compile(model)

        with self.model_lock:
            self.processor = processor
            self.model = model
            self.model_size = model_size
            self.actual_precision = actual_precision

        self.state.set_model_loading(False)
        print(
            f"Active model: {self.model_size.upper()}, device: {self.device}, precision: {self.actual_precision}"
        )

    def _prepare_inference_image(self, frame_bgr: np.ndarray) -> Tuple[Image.Image, Tuple[int, int]]:
        height, width = frame_bgr.shape[:2]
        target_width = self.config.inference_width if self.config.inference_width > 0 else width
        target_width = min(target_width, width)

        if target_width < width:
            target_height = max(1, int(height * (target_width / width)))
            resized_bgr = cv2.resize(frame_bgr, (target_width, target_height), interpolation=cv2.INTER_AREA)
        else:
            resized_bgr = frame_bgr

        resized_rgb = cv2.cvtColor(resized_bgr, cv2.COLOR_BGR2RGB)
        return Image.fromarray(resized_rgb), (resized_bgr.shape[1], resized_bgr.shape[0])

    def infer_depth_colormap(
        self, frame_bgr: np.ndarray
    ) -> Tuple[np.ndarray, float, Tuple[int, int], str, str, str]:
        image, inference_size = self._prepare_inference_image(frame_bgr)

        with self.model_lock:
            processor = self.processor
            model = self.model
            precision = self.actual_precision
            model_size = self.model_size

        if processor is None or model is None:
            raise RuntimeError("Model is not loaded")

        start = time.perf_counter()
        with torch.inference_mode():
            inputs = processor(images=image, return_tensors="pt").to(self.device)
            if precision == "fp16":
                inputs = {
                    key: value.half() if value.dtype == torch.float32 else value
                    for key, value in inputs.items()
                }
            elif precision == "bf16":
                inputs = {
                    key: value.bfloat16() if value.dtype == torch.float32 else value
                    for key, value in inputs.items()
                }

            outputs = model(**inputs)
            predicted_depth = outputs.predicted_depth

        inference_ms = (time.perf_counter() - start) * 1000.0

        depth_numpy = predicted_depth.squeeze().detach().cpu().numpy()
        depth_min = float(depth_numpy.min())
        depth_max = float(depth_numpy.max())

        if depth_max - depth_min < 1e-8:
            depth_normalized = np.zeros_like(depth_numpy, dtype=np.uint8)
        else:
            depth_normalized = ((depth_numpy - depth_min) / (depth_max - depth_min) * 255.0).astype(np.uint8)

        depth_resized_gray = cv2.resize(
            depth_normalized,
            (frame_bgr.shape[1], frame_bgr.shape[0]),
            interpolation=cv2.INTER_LINEAR,
        )
        depth_resized = cv2.cvtColor(depth_resized_gray, cv2.COLOR_GRAY2BGR)

        return depth_resized, inference_ms, inference_size, model_size, precision, self.device


def capture_loop(state: SharedState, config: RuntimeConfig) -> None:
    backend = cv2.CAP_DSHOW if os.name == "nt" else cv2.CAP_ANY
    cap = cv2.VideoCapture(config.camera_index, backend)

    if not cap.isOpened():
        state.set_error(f"Unable to open webcam index {config.camera_index}")
        state.running = False
        return

    cap.set(cv2.CAP_PROP_FRAME_WIDTH, config.camera_width)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, config.camera_height)
    cap.set(cv2.CAP_PROP_FPS, config.target_capture_fps)

    # Reduces latency on backends that support this property.
    cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)

    target_interval = 1.0 / config.target_capture_fps if config.target_capture_fps > 0 else 0.0

    print(
        f"Webcam opened: index={config.camera_index}, requested={config.camera_width}x{config.camera_height}"
    )

    while state.running:
        loop_start = time.perf_counter()
        ok, frame = cap.read()
        if not ok:
            state.set_error("Failed to read frame from webcam")
            time.sleep(0.05)
            continue

        state.set_latest_frame(frame)

        if target_interval > 0:
            elapsed = time.perf_counter() - loop_start
            sleep_for = target_interval - elapsed
            if sleep_for > 0:
                time.sleep(sleep_for)

    cap.release()


def processing_loop(state: SharedState, config: RuntimeConfig) -> None:
    runner = DepthModelRunner(config, state)

    try:
        runner.load_model(config.model_size)
    except Exception as exc:
        state.set_error(f"Model initialization failed: {exc}")
        state.running = False
        return

    while state.running:
        frame = state.get_latest_frame_copy()
        if frame is None:
            time.sleep(0.01)
            continue

        frame_start = time.perf_counter()

        try:
            depth_map, inference_ms, inference_size, model_size, precision, device = runner.infer_depth_colormap(
                frame
            )
            combined = np.hstack((frame, depth_map))

            cv2.putText(
                combined,
                "Webcam",
                (10, 28),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.8,
                (255, 255, 255),
                2,
                cv2.LINE_AA,
            )
            cv2.putText(
                combined,
                "Depth",
                (frame.shape[1] + 10, 28),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.8,
                (255, 255, 255),
                2,
                cv2.LINE_AA,
            )

            frame_ms = (time.perf_counter() - frame_start) * 1000.0
            ok, encoded = cv2.imencode(
                ".jpg", combined, [int(cv2.IMWRITE_JPEG_QUALITY), config.jpeg_quality]
            )
            if not ok:
                state.set_error("Failed to encode output frame")
                continue

            state.set_latest_output(
                encoded.tobytes(),
                inference_size,
                inference_ms,
                frame_ms,
                model_size,
                precision,
                device,
            )
            state.set_error("")

        except Exception as exc:
            state.set_error(f"Inference error: {exc}")
            time.sleep(0.02)


def stream_generator(state: SharedState):
    while state.running:
        frame = state.get_latest_jpeg()
        if frame is None:
            time.sleep(0.03)
            continue

        yield (
            b"--frame\r\n"
            b"Content-Type: image/jpeg\r\n\r\n" + frame + b"\r\n"
        )


def create_app(state: SharedState) -> Flask:
    templates_dir = Path(__file__).resolve().parent / "templates"
    app = Flask(__name__, template_folder=str(templates_dir))

    @app.get("/")
    def index():
        return render_template("index.html")

    @app.get("/stream")
    def stream():
        return Response(
            stream_generator(state),
            mimetype="multipart/x-mixed-replace; boundary=frame",
        )

    @app.get("/api/stats")
    def stats():
        return jsonify(state.stats_snapshot())

    return app


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Real-time webcam depth map with web control panel"
    )

    parser.add_argument(
        "--device",
        default="cpu",
        choices=["cpu", "gpu", "cuda"],
        help="Device for inference (default: cpu)",
    )
    parser.add_argument(
        "--precision",
        default="fp32",
        choices=["fp32", "fp16", "bf16"],
        help="Numerical precision (default: fp32)",
    )
    parser.add_argument(
        "--model-size",
        default="small",
        choices=["small", "base", "large"],
        help="Initial model size (default: small)",
    )
    parser.add_argument(
        "--camera-index",
        type=int,
        default=0,
        help="Webcam index (default: 0)",
    )
    parser.add_argument(
        "--camera-width",
        type=int,
        default=640,
        help="Capture width (default: 640)",
    )
    parser.add_argument(
        "--camera-height",
        type=int,
        default=480,
        help="Capture height (default: 480)",
    )
    parser.add_argument(
        "--inference-width",
        type=int,
        default=256,
        help="Inference frame width before model processing (default: 256)",
    )
    parser.add_argument(
        "--num-threads",
        type=int,
        default=-1,
        help="CPU threads when --device cpu (default: -1 = all)",
    )
    parser.add_argument(
        "--compile",
        action="store_true",
        help="Use torch.compile() for potential speedup",
    )
    parser.add_argument(
        "--jpeg-quality",
        type=int,
        default=80,
        help="Stream JPEG quality 40-95 (default: 80)",
    )
    parser.add_argument(
        "--target-capture-fps",
        type=int,
        default=30,
        help="Capture target FPS (default: 30)",
    )
    parser.add_argument(
        "--host",
        default="127.0.0.1",
        help="Web server host (default: 127.0.0.1)",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=5000,
        help="Web server port (default: 5000)",
    )

    return parser.parse_args()


def main() -> None:
    args = parse_args()

    jpeg_quality = max(40, min(95, args.jpeg_quality))

    config = RuntimeConfig(
        device=args.device,
        precision=args.precision,
        model_size=args.model_size,
        camera_index=args.camera_index,
        camera_width=args.camera_width,
        camera_height=args.camera_height,
        inference_width=args.inference_width,
        num_threads=args.num_threads,
        compile_model=args.compile,
        jpeg_quality=jpeg_quality,
        target_capture_fps=args.target_capture_fps,
    )

    state = SharedState(initial_model_size=config.model_size)
    app = create_app(state)

    capture_thread = threading.Thread(target=capture_loop, args=(state, config), daemon=True)
    process_thread = threading.Thread(target=processing_loop, args=(state, config), daemon=True)

    capture_thread.start()
    process_thread.start()

    print(f"Open http://{args.host}:{args.port} in your browser")

    try:
        app.run(host=args.host, port=args.port, debug=False, threaded=True, use_reloader=False)
    finally:
        state.running = False
        capture_thread.join(timeout=2.0)
        process_thread.join(timeout=2.0)


if __name__ == "__main__":
    main()
