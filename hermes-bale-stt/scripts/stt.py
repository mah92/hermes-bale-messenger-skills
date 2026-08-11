#!/usr/bin/env python3
"""
STT - Speech to Text for Persian audio files.
Uses Shenava-Koochik v1.0 ONNX model via sherpa-onnx.

Usage:
    stt.py <audio_file>
    stt.py --play  (record from mic and transcribe)

Requirements:
    pip install sherpa-onnx soundfile numpy scipy
    ffmpeg (for format conversion)
"""

import os, sys, time, subprocess, tempfile, argparse
import numpy as np
import soundfile as sf
import sherpa_onnx

SAMPLE_RATE = 16000
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_SKILL_DIR = os.path.dirname(_SCRIPT_DIR)
MODEL_DIR = os.path.join(_SKILL_DIR, "models", "shenava-koochik")
MODEL_PATH = os.path.join(MODEL_DIR, "model.int8.onnx")
TOKENS_PATH = os.path.join(MODEL_DIR, "tokens.txt")
HUSH_BINARY = os.getenv("STT_HUSH_BINARY", os.path.join(_SKILL_DIR, "models", "hush_cpp", "build", "hush_enhance_onnx"))

# Ensure ~/local/bin is in PATH (for ffmpeg static, etc.)
_local_bin = os.path.expanduser("~/local/bin")
if os.path.isdir(_local_bin):
    os.environ["PATH"] = _local_bin + ":" + os.environ.get("PATH", "")

# Global recognizer singleton
_recognizer = None

def get_recognizer():
    global _recognizer
    if _recognizer is None:
        if not os.path.exists(MODEL_PATH):
            print(f"Error: Model not found at {MODEL_PATH}", file=sys.stderr)
            print(f"Run: python3 {_SCRIPT_DIR}/download_models.py", file=sys.stderr)
            sys.exit(1)
        _recognizer = sherpa_onnx.OfflineRecognizer.from_nemo_ctc(
            model=MODEL_PATH,
            tokens=TOKENS_PATH,
            num_threads=4,
        )
    return _recognizer

def validate_and_convert(audio_path: str) -> str:
    """
    Validate audio format. Convert to WAV/mono/16bit/16kHz if needed.
    Returns path to valid WAV file (may be same as input or temp file).
    """
    # Check if already valid WAV
    try:
        info = sf.info(audio_path)
        is_valid = (
            info.subtype == "PCM_16" and
            info.samplerate == SAMPLE_RATE and
            info.channels == 1
        )
        if is_valid:
            return audio_path
        print(f"  Input: {info.samplerate}Hz, {info.channels}ch, {info.subtype} -> converting...")
    except Exception:
        print(f"  Input: unknown format -> converting...")

    # Convert with ffmpeg
    tmp = tempfile.NamedTemporaryFile(suffix=".wav", delete=False)
    tmp.close()
    out_path = tmp.name

    cmd = [
        "ffmpeg", "-y", "-i", audio_path,
        "-acodec", "pcm_s16le",
        "-ar", str(SAMPLE_RATE),
        "-ac", "1",
        out_path
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  ffmpeg error: {result.stderr[-500:]}")
        sys.exit(1)

    return out_path

def read_wave(path: str):
    samples, sr = sf.read(path, dtype="float32")
    if samples.ndim == 2:
        samples = samples[:, 0]
    if sr != SAMPLE_RATE:
        import scipy.signal
        ratio = SAMPLE_RATE / sr
        new_len = int(len(samples) * ratio)
        samples = scipy.signal.resample(samples, new_len)
    return samples

def denoise_wav(wav_path: str, verbose: bool = True) -> str:
    """Run hush_enhance_onnx to denoise a 16kHz mono WAV. Returns path to denoised file."""
    if not os.path.exists(HUSH_BINARY):
        if verbose:
            print(f"  [hush] binary not found at {HUSH_BINARY}, skipping denoise")
        return wav_path

    tmp = tempfile.NamedTemporaryFile(suffix=".wav", delete=False)
    tmp.close()
    out_path = tmp.name

    if verbose:
        print(f"  [hush] Denoising...", end=" ", flush=True)
        t0 = time.time()

    hush_env = os.environ.copy()
    # Strip LD_LIBRARY_PATH so hush links against the SYSTEM ONNX Runtime
    # it was built against (/usr/local/lib via ldconfig), not a possibly
    # incompatible version in ~/local/lib or elsewhere.
    hush_env.pop("LD_LIBRARY_PATH", None)
    hush_env.pop("LD_PRELOAD", None)

    # First try with --quiet, fall back without it (some platforms segfault with --quiet)
    for args in ([HUSH_BINARY, "--quiet", wav_path, out_path],
                 [HUSH_BINARY, wav_path, out_path]):
        result = subprocess.run(
            args, capture_output=True, text=True, env=hush_env
        )
        if result.returncode == 0:
            break
    if result.returncode != 0:
        if verbose:
            print(f"failed (exit {result.returncode}), using original")
        try:
            os.unlink(out_path)
        except Exception:
            pass
        return wav_path

    if verbose:
        print(f"done ({time.time() - t0:.1f}s)")

    return out_path


def transcribe(audio_path: str, verbose: bool = True) -> str:
    """Transcribe audio file. Returns Persian text."""
    wav_path = validate_and_convert(audio_path)

    # Denoise with Hush
    denoised_path = denoise_wav(wav_path, verbose=verbose)

    if verbose:
        print(f"  Loading model...", end=" ", flush=True)
        t0 = time.time()

    recognizer = get_recognizer()

    if verbose:
        print(f"done ({time.time() - t0:.1f}s)")

    samples = read_wave(denoised_path)
    total_sec = len(samples) / SAMPLE_RATE

    if verbose:
        print(f"  Audio: {total_sec:.1f}s")
        print(f"  Transcribing...", end=" ", flush=True)

    t0 = time.time()
    stream = recognizer.create_stream()
    stream.accept_waveform(SAMPLE_RATE, samples)
    recognizer.decode_stream(stream)
    text = stream.result.text.strip()
    decode_time = time.time() - t0

    if verbose:
        print(f"done ({decode_time:.2f}s)\n")

    # Cleanup temp files
    for tmp in [wav_path, denoised_path]:
        if tmp not in (audio_path,):
            try:
                os.unlink(tmp)
            except Exception:
                pass

    return text

def main():
    parser = argparse.ArgumentParser(description="Persian Speech to Text (STT)")
    parser.add_argument("audio", nargs="?", help="Path to audio file")
    parser.add_argument("--quiet", "-q", action="store_true", help="Only output text")
    args = parser.parse_args()

    if not args.audio:
        print("Usage: stt.py <audio_file>")
        print("  Supported: wav, mp3, m4a, ogg, flac, etc.")
        sys.exit(1)

    if not os.path.exists(args.audio):
        print(f"File not found: {args.audio}")
        sys.exit(1)

    text = transcribe(args.audio, verbose=not args.quiet)
    print(text)

if __name__ == "__main__":
    main()
