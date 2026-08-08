---
name: hermes-bale-stt
description: "Use when adding Persian STT to Bale on Hermes. Configures speech-to-text so voice messages are transcribed."
version: 1.1.0
author: علی محمودی
license: MIT
metadata:
  hermes:
    tags: [bale, persian, stt, asr, voice, transcription, shenava, koochik]
    related_skills: [hermes-bale-messenger]
---

# Bale STT (Speech-to-Text)

Adds Persian voice transcription to Bale on Hermes. Without this, voice
messages arrive as downloadable files with empty text. After setup, the
agent hears and responds to voice messages.

## Quick Setup

### 1. Install the skill

```bash
npx skills add mah92/hermes-bale-messenger-skills
```

Or manually:

```bash
cd ~/.hermes/skills/
git clone https://github.com/mah92/hermes-bale-messenger-skills.git
```

### 2. Download models

```bash
python3 ~/.hermes/skills/hermes-bale-messenger-skills/hermes-bale-stt/scripts/download_models.py
```

This downloads:
- Shenava-Koochik v1.0 int8 ONNX model (~126 MB)
- hush_cpp denoiser source (optional)

### 3. Install dependencies

```bash
pip install sherpa-onnx soundfile numpy scipy
sudo apt-get install ffmpeg
```

### 4. Build Hush denoiser (strongly recommended)

Hush denoises voice audio before transcription — cleaner input means better accuracy.
The source is downloaded by `download_models.py`, so run that first.

Build fails without `cmake` or ONNX Runtime. Install whatever is missing, then retry.

#### 4a. Install build tools (required)

```bash
sudo apt-get install -y cmake build-essential
```

#### 4b. Install ONNX Runtime (required for hush_cpp)

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

#### 4c. Build hush_cpp

```bash
cd ~/.hermes/skills/hermes-bale-messenger-skills/hermes-bale-stt/models/hush_cpp
mkdir -p build && cd build
cmake .. \
  -DONNX_RUNTIME_LIB=/usr/local/lib/libonnxruntime.so \
  -DONNX_RUNTIME_INCLUDE=/usr/local/include/onnxruntime
make -j$(nproc)
```

#### 4d. Verify the build

```bash
ls -la build/hush_enhance_onnx
```

If the binary exists (±240 KB), hush is ready. stt.py auto-discovers it.
If any step fails, denoising is skipped — transcription still works.

### 5. Configure Hermes

Add to `~/.hermes/config.yaml`:

```yaml
stt:
  provider: shenava
  enabled: true
  providers:
    shenava:
      type: command
      command: python3 ~/.hermes/skills/hermes-bale-messenger-skills/hermes-bale-stt/scripts/stt.py --quiet {input_path}
```

Then restart the gateway.

## How It Works

```
Bale voice message → adapter downloads OGG → gateway runs stt.py → Persian text → agent replies
```

The Bale plugin (`hermes-bale-messenger`) already downloads voice/audio files.
This skill adds the transcription step.

## Pipeline

```
audio file → ffmpeg → WAV 16kHz mono → hush_enhance_onnx (denoise) → clean WAV → Shenava-Koochik → Persian text
```

Graceful degradation: if hush binary is missing or build fails, denoise step is skipped.
Models are downloaded via `download_models.py` — no submodules needed.

## Dependencies

| Component | Required | Notes |
|-----------|----------|-------|
| Python: sherpa-onnx | ✅ | `pip install sherpa-onnx` |
| Python: soundfile, numpy, scipy | ✅ | `pip install soundfile numpy scipy` |
| System: ffmpeg | ✅ | `sudo apt-get install ffmpeg` |
| Shenava-Koochik model | ✅ | Downloaded by `download_models.py` (~126 MB) |
| hush_cpp | — | Optional denoiser. Downloaded by `download_models.py` |
| ONNX Runtime | — | Required only if building hush_cpp |

## Files

| File | Purpose |
|------|---------|
| `scripts/stt.py` | Main transcription script |
| `scripts/download_models.py` | Download model + hush_cpp from HF and GitHub |
| `models/shenava-koochik/` | Shenava-Koochik v1.0 int8 model (downloaded) |
| `models/hush_cpp/` | Hush-CPP denoiser source (downloaded, optional) |

## Model Info

- **Name:** Shenava-Koochik v1.0 (114M parameters)
- **Architecture:** FastConformer Hybrid RNNT/CTC (CTC head)
- **Sample rate:** 16 kHz, mono, 16-bit
- **WER:** 7.49% (golden-6669)
- **RTF:** ~0.013x on CPU, 4 threads
- **Format:** int8 quantized ONNX
- **Download:** `python3 scripts/download_models.py`

## Testing

Send a voice message to your bot on Bale. If the agent replies with the
transcribed text, STT is working.

To test from the terminal:

```bash
python3 ~/.hermes/skills/hermes-bale-messenger-skills/hermes-bale-stt/scripts/stt.py test_voice.ogg
```

## Common Pitfalls

1. **Voice messages still empty.** Check:
   - `stt.provider` set to `shenava` in config.yaml
   - Gateway restarted after config change
   - `sherpa-onnx` installed (`pip list | grep sherpa-onnx`)
   - Model files downloaded (`python3 scripts/download_models.py`)
2. **ModuleNotFoundError: sherpa_onnx.** Install: `pip install sherpa-onnx`
3. **ffmpeg not found.** Install: `sudo apt-get install ffmpeg`
4. **Model not downloaded.** Run `python3 scripts/download_models.py` to download model (~126 MB) + optional hush_cpp.
5. **Large voice files skipped.** Bale plugin skips audio > `BALE_MAX_VOICE_DURATION` seconds (default 30). Set to 0 in `.env` to disable limit.
6. **Hush denoiser missing.** Gracefully skipped. Set `STT_HUSH_BINARY` env var to custom path.
