#!/usr/bin/env python3
"""
Persian TTS: MatchaTTS daemon client → OGG OPUS.
Hermes command provider. Reads text from {input_path}, writes OGG to {output_path}.

Uses the MatchaTTSInfer daemon on Unix socket /tmp/tts_infer.sock.
Models loaded once by daemon — requests ~200ms instead of 2.5s.
"""
import sys
import os
import json
import socket
import subprocess
import tempfile
import time
import argparse

SOCKET_PATH = "/tmp/tts_infer.sock"
MAX_RETRIES = 3  # try 3 times total
DEFAULT_SPEED = 1.5

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SKILL_DIR = os.path.dirname(SCRIPT_DIR)
MODELS_DIR = os.path.join(SKILL_DIR, "models")

BIN = os.environ.get(
    "MATCHA_TTS_BIN",
    os.path.expanduser("~/Basir/TTS/match_tts_infer/build/MatchaTTSInfer"),
)

# Model paths — passed explicitly to the binary (no hardcoded defaults in C++).
MATCHA_MODEL = os.path.join(MODELS_DIR, "matcha-fa_en-zahra-22050-5.onnx")
VOCODER_MODEL = os.path.join(MODELS_DIR, "vocos22.onnx")
TOKENS_FILE = os.path.join(MODELS_DIR, "tokens_sherpa_with_fa.txt")
ESPEAK_DATA = os.path.expanduser("~/Basir/TTS/Piper/piper_linux_x86_64/piper/espeak-ng-data")


def _start_daemon() -> bool:
    """Start the MatchaTTSInfer daemon with explicit model paths."""
    norm_dir = os.path.join(os.path.dirname(BIN), "..", "NormalizeText")
    cmd = [
        BIN, "--daemon",
        "--matcha-model", MATCHA_MODEL,
        "--vocoder-model", VOCODER_MODEL,
        "--tokens", TOKENS_FILE,
        "--espeak-data", ESPEAK_DATA,
    ]
    try:
        subprocess.Popen(
            cmd,
            cwd=norm_dir,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
        return True
    except Exception as e:
        print(f"Error: failed to start daemon: {e}", file=sys.stderr)
        return False


def _send_request(text: str, output_wav: str, speed: float = 1.5) -> dict:
    """Send a synthesis request to the daemon. Retries up to MAX_RETRIES times."""
    request = json.dumps({
        "text": text,
        "output": output_wav,
        "speed": speed,
    }, ensure_ascii=False)

    last_error = ""
    for attempt in range(MAX_RETRIES):
        if attempt > 0:
            time.sleep(1)
        try:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.settimeout(30)
            sock.connect(SOCKET_PATH)
            sock.sendall((request + "\n").encode("utf-8"))

            response = b""
            while True:
                ch = sock.recv(1)
                if not ch or ch == b"\n":
                    break
                response += ch
            sock.close()

            if not response:
                last_error = "no response from daemon"
                continue

            result = json.loads(response.decode("utf-8"))
            if result.get("status") == "ok":
                return result
            last_error = result.get("message", "unknown error")
            continue
        except (socket.error, ConnectionRefusedError, OSError) as e:
            last_error = str(e)
            continue

    return {"status": "error", "message": last_error}


def _wav_to_ogg(wav_path: str, ogg_path: str) -> None:
    """Convert WAV to OGG OPUS (mono 16kHz, voip-optimized). Retries on failure."""
    last_err = None
    for attempt in range(MAX_RETRIES):
        if attempt > 0:
            time.sleep(1)
        try:
            subprocess.run([
                "ffmpeg", "-y", "-i", wav_path,
                "-c:a", "libopus", "-b:a", "32k",
                "-ar", "16000", "-ac", "1",
                "-application", "voip", ogg_path,
            ], capture_output=True, check=True, timeout=30)
            return
        except subprocess.CalledProcessError as e:
            last_err = e
            continue
    raise last_err if last_err else RuntimeError("ffmpeg failed after retries")


def main():
    parser = argparse.ArgumentParser(description="Persian TTS via MatchaTTS daemon")
    parser.add_argument("--speed", type=float, default=DEFAULT_SPEED,
                        help=f"Speaking speed multiplier (default: {DEFAULT_SPEED})")
    parser.add_argument("input_path", nargs="?", help="Text input file path")
    parser.add_argument("output_path", nargs="?", help="OGG output file path")
    args = parser.parse_args()

    if not args.input_path or not args.output_path:
        parser.print_help()
        sys.exit(1)

    with open(args.input_path, encoding="utf-8") as f:
        text = f.read().strip()
    if not text:
        sys.exit(1)

    # Start daemon if not running
    if not os.path.exists(SOCKET_PATH):
        if not _start_daemon():
            sys.exit(1)
        # Wait for daemon to be ready (up to 15s for model loading)
        for _ in range(150):
            if os.path.exists(SOCKET_PATH):
                break
            time.sleep(0.1)
        if not os.path.exists(SOCKET_PATH):
            print("Error: daemon did not start within 15s", file=sys.stderr)
            sys.exit(1)

    # Synthesize via daemon
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        tmp_wav = f.name

    try:
        result = _send_request(text, tmp_wav, speed=args.speed)
        if result.get("status") != "ok":
            print(f"Error: {result.get('message', 'unknown')}", file=sys.stderr)
            sys.exit(1)

        wav_output = result.get("output", tmp_wav)
        if not os.path.exists(wav_output) or os.path.getsize(wav_output) < 100:
            print("Error: daemon produced no audio", file=sys.stderr)
            sys.exit(1)

        # Convert to OGG
        _wav_to_ogg(wav_output, args.output_path)
    finally:
        try:
            os.unlink(tmp_wav)
        except OSError:
            pass


if __name__ == "__main__":
    main()
