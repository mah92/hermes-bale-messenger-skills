#!/usr/bin/env python3
"""
Download model files for hermes-stt.

Downloads:
  - Shenava-Koochik v1.0 int8 ONNX model + tokens (from HuggingFace, ~126 MB)
  - hush_cpp denoiser source (from GitHub, optional)

Usage:
  python3 download_models.py              # download everything
  python3 download_models.py --model-only # only the speech model
  python3 download_models.py --force      # re-download even if exists
"""

import os, sys, argparse, tarfile, tempfile, shutil
from urllib.request import urlopen, Request
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
SKILL_DIR = SCRIPT_DIR.parent  # hermes-stt/
MODEL_DIR = SKILL_DIR / "models" / "shenava-koochik"
HUSH_DIR = SKILL_DIR / "models" / "hush_cpp"

MODEL_FILES = [
    ("model.int8.onnx",   "https://huggingface.co/mah92/sherpa-onnx-nemo-ctc-fa-shenava-koochik-v1.0-non-streaming-int8-2026-06-26/resolve/main/model.int8.onnx"),
    ("tokens.txt",        "https://huggingface.co/mah92/sherpa-onnx-nemo-ctc-fa-shenava-koochik-v1.0-non-streaming-int8-2026-06-26/resolve/main/tokens.txt"),
]

HUSH_URL = "https://github.com/mah92/hush_cpp/archive/refs/heads/developing.tar.gz"

CHUNK = 1024 * 1024  # 1 MB


def download_file(url: str, dest: Path, label: str):
    """Download a file with progress indicator. Skips if dest exists."""
    if dest.exists():
        print(f"  {label}: already exists ({dest.stat().st_size / 1024 / 1024:.0f} MB), skipping")
        return

    print(f"  {label}: downloading...", end=" ", flush=True)
    req = Request(url, headers={"User-Agent": "hermes-stt/1.0"})
    with urlopen(req) as resp:
        size = int(resp.headers.get("Content-Length", 0))
        downloaded = 0
        dest.parent.mkdir(parents=True, exist_ok=True)
        with open(dest, "wb") as f:
            while True:
                chunk = resp.read(CHUNK)
                if not chunk:
                    break
                f.write(chunk)
                downloaded += len(chunk)
                if size:
                    pct = downloaded * 100 // size
                    print(f"\r  {label}: {pct}% ({downloaded // 1024 // 1024}MB)", end="", flush=True)
        print(f"\r  {label}: done ({downloaded / 1024 / 1024:.0f} MB)        ")


def download_hush():
    """Download and extract hush_cpp source from GitHub."""
    dest_dir = HUSH_DIR
    marker = dest_dir / ".downloaded"

    if marker.exists():
        print(f"  hush_cpp: already downloaded, skipping")
        return

    print(f"  hush_cpp: downloading...", end=" ", flush=True)

    req = Request(HUSH_URL, headers={"User-Agent": "hermes-stt/1.0"})
    with urlopen(req) as resp:
        with tempfile.NamedTemporaryFile(suffix=".tar.gz", delete=False) as tmp:
            shutil.copyfileobj(resp, tmp)
            tmp_path = tmp.name

    print("extracting...", end=" ", flush=True)
    with tarfile.open(tmp_path, "r:gz") as tar:
        # tar contains a single top-level dir like "hush_cpp-developing/"
        members = tar.getmembers()
        top_dir = members[0].name.split("/")[0] if members else "hush_cpp-developing"

        # Extract to temp location first
        with tempfile.TemporaryDirectory() as extract_tmp:
            tar.extractall(extract_tmp)
            src = Path(extract_tmp) / top_dir

            # Remove old if exists
            if dest_dir.exists():
                shutil.rmtree(dest_dir)
            dest_dir.parent.mkdir(parents=True, exist_ok=True)

            shutil.move(str(src), str(dest_dir))

    os.unlink(tmp_path)
    marker.touch()
    print("done")


def main():
    parser = argparse.ArgumentParser(description="Download models for hermes-stt")
    parser.add_argument("--model-only", action="store_true", help="Only download speech model (skip hush_cpp)")
    parser.add_argument("--force", action="store_true", help="Re-download even if files exist")
    args = parser.parse_args()

    if args.force:
        import shutil
        if MODEL_DIR.exists():
            shutil.rmtree(MODEL_DIR)
        if HUSH_DIR.exists():
            shutil.rmtree(HUSH_DIR)
        print("Removed existing files (--force)")

    print("Downloading Shenava-Koochik model:")
    for filename, url in MODEL_FILES:
        download_file(url, MODEL_DIR / filename, filename)

    if not args.model_only:
        print("\nDownloading hush_cpp denoiser (optional):")
        download_hush()

    print("\nDone! Models are in:", SKILL_DIR / "models")
    if not args.model_only:
        print("\nTo build hush_cpp (optional):")
        print(f"  cd {HUSH_DIR}")
        print("  mkdir -p build && cd build")
        print("  cmake .. -DONNX_RUNTIME_LIB=/usr/local/lib/libonnxruntime.so")
        print("  make -j$(nproc)")


if __name__ == "__main__":
    main()
