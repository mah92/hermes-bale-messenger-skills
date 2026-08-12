#!/usr/bin/env python3
"""
Download model files for hermes-persian-tts.

Downloads:
  - Matcha-TTS ONNX model (Zahra voice, ~72 MB)
  - Vocos universal vocoder ONNX model (~30 MB)
  - Token file (tokens_sherpa_with_fa.txt)

Usage:
  python3 download_models.py              # download everything
  python3 download_models.py --force      # re-download even if exists
"""

import os, sys, argparse
from urllib.request import urlopen, Request
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
SKILL_DIR = SCRIPT_DIR.parent  # hermes-persian-tts/
MODEL_DIR = SKILL_DIR / "models"

# ── Model files ──────────────────────────────────────────────────────────
# Change these URLs to use a different voice or update model versions.
# Current defaults: Zahra voice (22050 Hz, 5 ODE steps).

MODEL_FILES = [
    (
        "matcha-fa_en-zahra-22050-5.onnx",
        "https://huggingface.co/mah92/Zahra-FA_EN-22KHz-Matcha-TTS-Model/resolve/main/matcha-fa_en-zahra-22050-5.onnx",
        "Matcha-TTS ONNX model (Zahra voice, 5 ODE steps)",
    ),
    (
        "vocos22.onnx",
        "https://huggingface.co/k2-fsa/sherpa-onnx-models/resolve/main/vocoder-models/vocos-22khz-univ.onnx",
        "Vocos universal vocoder (any sample rate)",
    ),
    (
        "tokens_sherpa_with_fa.txt",
        "https://huggingface.co/mah92/Zahra-FA_EN-22KHz-Matcha-TTS-Model/resolve/main/tokens_sherpa_with_fa.txt",
        "Token map (IPA → model IDs)",
    ),
]

CHUNK = 1024 * 1024  # 1 MB


def download_file(url: str, dest: Path, label: str):
    """Download a file with progress indicator. Skips if dest exists."""
    if dest.exists():
        size_mb = dest.stat().st_size / 1024 / 1024
        print(f"  {label}: already exists ({size_mb:.0f} MB), skipping")
        return

    print(f"  {label}: downloading...", end=" ", flush=True)
    req = Request(url, headers={"User-Agent": "hermes-persian-tts/1.0"})

    try:
        with urlopen(req, timeout=120) as resp:
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
                        print(f"\r  {label}: {pct}% ({downloaded // 1024 // 1024}MB)",
                              end="", flush=True)
            print(f"\r  {label}: done ({downloaded / 1024 / 1024:.0f} MB)        ")
    except Exception as e:
        print(f"\n  {label}: FAILED — {e}")
        # Remove partial download
        if dest.exists():
            dest.unlink()
        raise


def main():
    parser = argparse.ArgumentParser(description="Download models for hermes-persian-tts")
    parser.add_argument("--force", action="store_true",
                        help="Re-download even if files exist")
    args = parser.parse_args()

    if args.force:
        import shutil
        if MODEL_DIR.exists():
            shutil.rmtree(MODEL_DIR)
            print("Removed existing files (--force)")
        print()

    missing = False
    print("Downloading TTS models to:", MODEL_DIR)
    print()

    for filename, url, label in MODEL_FILES:
        dest = MODEL_DIR / filename
        try:
            download_file(url, dest, f"{filename} ({label})")
        except Exception:
            missing = True

    print()
    if missing:
        print("⚠  Some files failed to download.")
        print("   Check your internet connection and try again, or download manually.")
        print()
        print("Manual download URLs:")
        for filename, url, label in MODEL_FILES:
            dest = MODEL_DIR / filename
            if not dest.exists():
                print(f"  {filename}: {url}")
        sys.exit(1)

    print("Done! Models are in:", MODEL_DIR)
    print()
    print("Next steps:")
    print(f"  1. Clone the C++ inference repo (if not already done):")
    print(f"     mkdir -p ~/Basir/TTS")
    print(f"     cd ~/Basir/TTS")
    print(f"     git clone --recurse-submodules https://github.com/mah92/matcha_tts_infer.git")
    print()
    print(f"  2. Install build dependencies:")
    print(f"     sudo apt-get install -y cmake build-essential libespeak-ng-dev libicu-dev")
    print()
    print(f"  3. Install ONNX Runtime (see SKILL.md for download + install steps)")
    print()
    print(f"  4. Build the C++ binary:")
    print(f"     cd ~/Basir/TTS/matcha_tts_infer && mkdir -p build && cd build")
    print(f"     cmake .. && make -j$(nproc)")
    print()
    print(f"  5. Start the daemon:")
    print(f"     cd ~/Basir/TTS/matcha_tts_infer/NormalizeText")
    print(f"     ~/Basir/TTS/matcha_tts_infer/build/MatchaTTSInfer --daemon &")
    print()
    print(f"  6. Configure Hermes — see SKILL.md for config.yaml snippet.")


if __name__ == "__main__":
    main()
