﷽

# Hermes Persian Skills

Persian language skills for Hermes Agent — speech-to-text, text-to-speech, and
the Bale (بله) messenger platform adapter. One repo, three skills.

| Skill | Install | Purpose |
|-------|---------|---------|
| `hermes-bale-messenger` | `--skill hermes-bale-messenger` | Bale platform adapter (messages, voice, images, documents) |
| `hermes-stt` | `--skill hermes-stt` | Persian speech-to-text via Shenava-Koochik |
| `hermes-tts` | `--skill hermes-tts` | Persian text-to-speech via MatchaTTS daemon (Zahra voice) |

## Quick Install

```bash
# Bale messenger platform
npx skills add mah92/hermes-persian-skills --skill hermes-bale-messenger

# Persian STT (speech-to-text)
npx skills add mah92/hermes-persian-skills --skill hermes-stt
python3 ~/.hermes/skills/hermes-persian-skills/hermes-stt/scripts/download_models.py

# Persian TTS (text-to-speech)
npx skills add mah92/hermes-persian-skills --skill hermes-tts
python3 ~/.hermes/skills/hermes-persian-skills/hermes-tts/scripts/download_models.py
```

## What You Get

- **hermes-bale-messenger:** Bale platform adapter for Hermes Gateway.
  Sends/receives text, voice, images, documents. Group chat support, typing
  indicators, user allowlisting, cron delivery. Native `sendVoice` delivery.
- **hermes-stt:** Transcribes Persian voice messages from any platform.
  Shenava-Koochik v1.0 (114M params, WER 7.49%), optional hush_cpp denoiser.
- **hermes-tts:** Synthesizes Persian speech via MatchaTTS C++ daemon.
  Zahra voice (22050 Hz), vocos22 vocoder, ~200ms per request after daemon warm-up.

## Requirements

- Hermes Agent with gateway
- Bale bot token from @BotFather (for hermes-bale-messenger)
- Python: `sherpa-onnx soundfile numpy scipy` (for hermes-stt)
- System: `ffmpeg`, `cmake`, `build-essential` (for hermes-stt + hermes-tts)

## Links

- Hermes Agent: [hermes-agent.nousresearch.com](https://hermes-agent.nousresearch.com)
- Bale messenger: [bale.ai](https://bale.ai)
- Shenava-Koochik STT model: [HuggingFace](https://huggingface.co/mah92/sherpa-onnx-nemo-ctc-fa-shenava-koochik-v1.0-non-streaming-int8-2026-06-26)
- Zahra Matcha-TTS model: [HuggingFace](https://huggingface.co/mah92/Zahra-FA_EN-22KHz-Matcha-TTS-Model)
- Vocos vocoder: [HuggingFace](https://huggingface.co/k2-fsa/sherpa-onnx-models)
