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

SOCKET_PATH = "/tmp/tts_infer.sock"
MAX_RETRIES = 3  # try 3 times total


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
    text_path = sys.argv[1]
    output_path = sys.argv[2]

    with open(text_path, encoding="utf-8") as f:
        text = f.read().strip()
    if not text:
        sys.exit(1)

    # Check if daemon is running
    if not os.path.exists(SOCKET_PATH):
        print("Error: TTS daemon not running", file=sys.stderr)
        print(f"Start it: cd ~/Basir/TTS/matcha_tts_infer/NormalizeText && "
              f"~/Basir/TTS/matcha_tts_infer/build/MatchaTTSInfer --daemon &",
              file=sys.stderr)
        sys.exit(1)

    # Synthesize via daemon
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        tmp_wav = f.name

    try:
        result = _send_request(text, tmp_wav)
        if result.get("status") != "ok":
            print(f"Error: {result.get('message', 'unknown')}", file=sys.stderr)
            sys.exit(1)

        wav_output = result.get("output", tmp_wav)
        if not os.path.exists(wav_output) or os.path.getsize(wav_output) < 100:
            print("Error: daemon produced no audio", file=sys.stderr)
            sys.exit(1)

        # Convert to OGG
        _wav_to_ogg(wav_output, output_path)
    finally:
        try:
            os.unlink(tmp_wav)
        except OSError:
            pass


if __name__ == "__main__":
    main()
