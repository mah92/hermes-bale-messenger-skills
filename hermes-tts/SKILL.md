---
name: hermes-tts
description: "Persian TTS via MatchaTTS C++ daemon — general-purpose, not Bale-specific. Loads models once (2.5s), then ~200ms per request."
version: 1.0.0
author: علی محمودی
license: MIT
metadata:
  hermes:
    tags: [tts, persian, matcha, vocos, daemon, zahra]
    related_skills: [hermes-bale-messenger]
---

# MatchaTTS Daemon — Fast Persian TTS

Adds Persian text-to-speech to Hermes using the MatchaTTS C++ inference engine
running as a background daemon. Models load once (2.5s), then every request
is ~200ms — 12x faster than loading from scratch each time.

## Quick Setup

### 1. Install the skill

```bash
npx skills add mah92/hermes-persian-skills --skill hermes-tts
```

Or manually:

```bash
cd ~/.hermes/skills/
git clone https://github.com/mah92/hermes-persian-skills.git
```

### 2. Download models

```bash
python3 ~/.hermes/skills/hermes-persian-skills/hermes-tts/scripts/download_models.py
```

This downloads:
- Matcha-TTS ONNX model (~72 MB) — Zahra voice, 22050 Hz
- Vocos universal vocoder (~30 MB)
- Token map file

### 3. Build the C++ binary

#### 3a. Clone the repo (if not already done)

```bash
mkdir -p ~/Basir/TTS
cd ~/Basir/TTS
git clone --recurse-submodules https://github.com/mah92/matcha_tts_infer.git
```

This clones `matcha_tts_infer` + the `NormalizeText` submodule (ezafe, hazm, shakkelha, homograph, espeak-ng).

#### 3b. Install build tools and libraries (required)

```bash
sudo apt-get install -y cmake build-essential libespeak-ng-dev libicu-dev
```

#### 3c. Install ONNX Runtime (required)

The binary links against ONNX Runtime for model inference.

Check if already installed:

```bash
ldconfig -p | grep libonnxruntime
```

If not found, download and install ONNX Runtime 1.20.0:

```bash
wget https://github.com/microsoft/onnxruntime/releases/download/v1.20.0/onnxruntime-linux-x64-1.20.0.tgz
tar xzf onnxruntime-linux-x64-1.20.0.tgz
sudo cp -r onnxruntime-linux-x64-1.20.0/lib/* /usr/local/lib/
sudo cp -r onnxruntime-linux-x64-1.20.0/include/* /usr/local/include/
sudo ldconfig
```

#### 3d. Build MatchaTTSInfer

```bash
cd ~/Basir/TTS/matcha_tts_infer
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

#### 3e. Verify the build

```bash
ls -la ~/Basir/TTS/matcha_tts_infer/build/MatchaTTSInfer
```

If the binary exists (~250 KB), it's ready.

### 4. Start the daemon

The daemon MUST run from the `NormalizeText/` directory (assets are at `./assets/`):

```bash
cd ~/Basir/TTS/matcha_tts_infer/NormalizeText
~/Basir/TTS/matcha_tts_infer/build/MatchaTTSInfer --daemon &
```

Wait ~2.5 seconds for models to load, then verify:

```bash
echo '{"text":"سلام","output":"/tmp/test.wav"}' | nc -U /tmp/tts_infer.sock
```

Should return JSON with `"status":"ok"` and a WAV path.

**Auto-start:** Add to crontab or systemd to survive reboots:

```bash
# Crontab
@reboot cd ~/Basir/TTS/matcha_tts_infer/NormalizeText && ~/Basir/TTS/matcha_tts_infer/build/MatchaTTSInfer --daemon &
```

### 5. Configure Hermes

Add to `~/.hermes/config.yaml`:

```yaml
tts:
  provider: matcha
  providers:
    matcha:
      type: command
      command: python3 ~/.hermes/skills/hermes-persian-skills/hermes-tts/scripts/tts.py {input_path} {output_path}
      output_format: ogg
      timeout: 60
      voice_compatible: true
```

Then restart the gateway.

## How It Works

```
Hermes → tts.py → Unix socket /tmp/tts_infer.sock → MatchaTTSInfer daemon → WAV → ffmpeg → OGG
```

The daemon listens on `/tmp/tts_infer.sock` (Unix domain socket). The `tts.py`
wrapper connects, sends JSON with text + params, receives WAV path, converts
to OGG OPUS, and passes it back to Hermes.

The daemon loads all models once at startup:
- Token map (158 tokens)
- Matcha-TTS ONNX (~72 MB)
- Vocos vocoder ONNX (~30 MB)
- NormalizeText pipeline (ezafe, homograph, shakkelha, hazm, espeak-ng)

Subsequent requests skip all loading — just normalize + synthesize.

## Performance

| Metric | Value |
|--------|-------|
| Daemon startup | ~2.5s (one-time) |
| First request | ~2.2s (includes first-time normalizeText init) |
| Subsequent requests | ~200ms |
| MatchaTTS inference | ~35ms |
| Vocos inference | ~10ms |
| NormalizeText (cached) | ~140ms |
| RTF | ~0.15 on CPU (4 threads) |

## Files

| File | Purpose |
|------|---------|
| `scripts/tts.py` | TTS command provider — talks to daemon, converts to OGG |
| `~/Basir/TTS/matcha_tts_infer/build/MatchaTTSInfer` | C++ binary (built separately) |
| `/tmp/tts_infer.sock` | Daemon socket (created at startup, cleaned on stop) |

## Managing the Daemon

```bash
# Start (from NormalizeText directory!)
cd ~/Basir/TTS/matcha_tts_infer/NormalizeText
~/Basir/TTS/matcha_tts_infer/build/MatchaTTSInfer --daemon &

# Check if running
[ -S /tmp/tts_infer.sock ] && echo "running" || echo "stopped"

# Test
echo '{"text":"سلام","output":"/tmp/test.wav"}' | nc -U /tmp/tts_infer.sock

# Stop gracefully
cd ~/Basir/TTS/matcha_tts_infer/NormalizeText
~/Basir/TTS/matcha_tts_infer/build/MatchaTTSInfer --stop

# Force kill
pkill -f "MatchaTTSInfer --daemon"
rm -f /tmp/tts_infer.sock
```

## Common Pitfalls

1. **Socket not found.** Daemon not running. Start it with `--daemon` from the `NormalizeText/` directory.
2. **Failed to load SentencePiece model.** Binary must run from `NormalizeText/` directory so `./assets/` resolves correctly. Always `cd ~/Basir/TTS/matcha_tts_infer/NormalizeText` before starting daemon.
3. **Daemon dies after first request.** SIGPIPE when client disconnects mid-request. The daemon now ignores SIGPIPE — rebuild if using an older version.
4. **Daemon dies on health check.** Don't use `nc -z` or quick-connect tests — each connect consumes one `accept()` slot. Use `test -S /tmp/tts_infer.sock` to check if running, or `echo '{"command":"stop"}' | nc -U` for graceful shutdown.
5. **Empty text passed to tts.py.** Hermes writes text to `{input_path}`. The script reads it with UTF-8 encoding. Non-Persian text or empty files produce silent audio — check the input file content.
6. **OPUS 48kHz metadata.** The OGG output may show 48000 Hz in ffprobe — this is normal OPUS internal rate. Audio decodes correctly at the original sample rate.
7. **Stale socket after crash.** If daemon crashes, the socket file remains. Delete it before restarting: `rm -f /tmp/tts_infer.sock`.
8. **Model paths changed.** Update paths in `tts.py` (MODEL_DIR, MATCHA_BIN, etc.) or pass explicit `--matcha-model`, `--vocoder-model` etc. to the daemon at startup.
