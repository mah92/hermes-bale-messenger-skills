# Hermes Bale Messenger Skills

Bale (بله) messenger integration for Hermes Agent — Persian messenger support
via the free Bot API.

Three skills in one repo:

| Skill | Install | Purpose |
|-------|---------|---------|
| `hermes-bale-messenger` | `--skill hermes-bale-messenger` | Bale platform adapter + 7 pitfalls |
| `hermes-bale-stt` | `--skill hermes-bale-stt` | Persian speech-to-text via Shenava-Koochik |
| `hermes-matcha-tts` | `--skill hermes-matcha-tts` | Persian TTS via MatchaTTS daemon (Zahra voice) |

## Quick Install

```bash
# Messenger plugin
npx skills add mah92/hermes-bale-messenger-skills --skill hermes-bale-messenger

# Persian STT
npx skills add mah92/hermes-bale-messenger-skills --skill hermes-bale-stt
python3 ~/.hermes/skills/hermes-bale-messenger-skills/hermes-bale-stt/scripts/download_models.py

# Persian TTS
npx skills add mah92/hermes-bale-messenger-skills --skill hermes-matcha-tts
# Then start the daemon: see hermes-matcha-tts SKILL.md
```

## What You Get

- **hermes-bale-messenger:** Sends/receives text, voice, images, documents in Bale.
  Group chat support, typing indicators, user allowlisting, cron delivery.
- **hermes-bale-stt:** Transcribes Persian voice messages.
  Shenava-Koochik v1.0 (114M params, WER 7.49%), optional hush_cpp denoiser.
- **hermes-matcha-tts:** Synthesizes Persian speech via MatchaTTS C++ daemon.
  Zahra voice (22050 Hz), vocos22 vocoder, ~200ms per request after daemon warm-up.

## Requirements

- Hermes Agent with gateway
- Bale bot token from @BotFather
- Python: `sherpa-onnx soundfile numpy scipy`
- System: `ffmpeg`

## Links

- Hermes Agent: [hermes-agent.nousresearch.com](https://hermes-agent.nousresearch.com)
- Bale messenger: [bale.ai](https://bale.ai)
- Shenava-Koochik model: [HuggingFace](https://huggingface.co/mah92/sherpa-onnx-nemo-ctc-fa-shenava-koochik-v1.0-non-streaming-int8-2026-06-26)
