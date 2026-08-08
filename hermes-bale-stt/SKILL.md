---
name: hermes-bale-stt
description: "Use when adding Persian STT to Bale on Hermes. Configures speech-to-text so voice messages are transcribed."
version: 1.2.0
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
- hush_cpp denoiser source

#### ⚠️ Verify the download — do NOT skip this step

`download_models.py` may report success even when files failed to write.
Always verify:

```bash
# Must show ~126MB file, NOT 134 bytes (LFS pointer) and NOT missing
ls -lh ~/.hermes/skills/hermes-bale-messenger-skills/hermes-bale-stt/models/shenava-koochik/model.int8.onnx

# Must show hush_cpp source files (CMakeLists.txt, src/, onnx/)
ls ~/.hermes/skills/hermes-bale-messenger-skills/hermes-bale-stt/models/hush_cpp/CMakeLists.txt
```

If files are missing, re-run with `--force`:

```bash
python3 ~/.hermes/skills/hermes-bale-messenger-skills/hermes-bale-stt/scripts/download_models.py --force
```

### 3. Install dependencies

**IMPORTANT:** Install into Hermes' own venv, NOT system Python. The STT
provider command runs under the Hermes agent's Python so sherpa-onnx must be
discoverable there.

```bash
~/.hermes/hermes-agent/venv/bin/pip install sherpa-onnx soundfile numpy scipy
sudo apt-get install ffmpeg
```

### 4. Build Hush denoiser (strongly recommended)

Hush denoises voice audio before transcription — cleaner input means better accuracy.
The source is downloaded by `download_models.py`, so run that first.

**Try hard to build this.** Install any missing dependencies (cmake, ONNX Runtime) and
retry — do NOT give up at the first error. Only skip if you've exhausted every option.

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
      command: ~/.hermes/hermes-agent/venv/bin/python ~/.hermes/skills/hermes-bale-messenger-skills/hermes-bale-stt/scripts/stt.py --quiet {input_path}
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
| Python: sherpa-onnx | ✅ | Install via `~/.hermes/hermes-agent/venv/bin/pip` |
| Python: soundfile, numpy, scipy | ✅ | Install via `~/.hermes/hermes-agent/venv/bin/pip` |
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
~/.hermes/hermes-agent/venv/bin/python ~/.hermes/skills/hermes-bale-messenger-skills/hermes-bale-stt/scripts/stt.py test_voice.ogg
```

## Common Pitfalls

1. **Voice messages still empty.** Check:
   - `stt.provider` set to `shenava` in config.yaml
   - Gateway restarted after config change
   - `sherpa-onnx` installed in Hermes venv: `~/.hermes/hermes-agent/venv/bin/pip list | grep sherpa-onnx`
   - Model files downloaded (`python3 scripts/download_models.py`)
2. **ModuleNotFoundError: sherpa_onnx.** Install into Hermes venv: `~/.hermes/hermes-agent/venv/bin/pip install sherpa-onnx soundfile numpy scipy`
3. **ffmpeg not found.** Install: `sudo apt-get install ffmpeg`
4. **Model not downloaded.** Run `python3 scripts/download_models.py` to download model (~126 MB) + hush_cpp. **Always verify files exist after download** — the script may report "Done!" even when files failed to write. Check: `ls -lh models/shenava-koochik/model.int8.onnx` (must be ~126MB).
5. **Large voice files skipped.** Bale plugin skips audio > `BALE_MAX_VOICE_DURATION` seconds (default 30). Set to 0 in `.env` to disable limit.
6. **Hush denoiser missing.** Set `STT_HUSH_BINARY` env var to custom path. But you should BUILD it instead — see step 4.
7. **Using system python3 instead of Hermes venv.** The STT provider runs inside Hermes, so sherpa-onnx must be installed in `~/.hermes/hermes-agent/venv/`, not system-wide. Always use `~/.hermes/hermes-agent/venv/bin/pip` for installs and `~/.hermes/hermes-agent/venv/bin/python` for testing.
8. **download_models.py silent failure.** The script can print "Done!" but files may be empty or missing (e.g. if a previous run left broken files and the script's existence check skips re-download). Always verify the actual file sizes after download. If model.int8.onnx is not ~126MB, use `--force` flag.
9. **Agent gave up on hush_cpp too early.** The label "optional" means the pipeline works without it, not that you should skip it. Install missing tools (cmake, build-essential, ONNX Runtime) and retry. Exhaust every option before conceding — denoising makes a real difference for voice quality.
