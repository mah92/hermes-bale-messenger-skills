
# In the name of my Unique Lovely God
# Hush C++ Denoiser

C++ inference for [Hush](https://huggingface.co/facebook/hush) — a speech denoising model by Meta (Facebook Research).

This project runs the Hush ONNX models directly using ONNX Runtime, with STFT/ISTFT from [kaldi-native-fbank](https://github.com/csukuangfj/kaldi-native-fbank) (knf).

## References

This project is based on two sources:

1. **Hush model (HuggingFace)**: [facebook/hush](https://huggingface.co/facebook/hush) — the pre-trained denoising model and ONNX export.
2. **Hush repository (GitHub)**: [facebookresearch/hush](https://github.com/facebookresearch/hush) — the original PyTorch training code, deployment examples, and libdf feature extraction reference.

## Prerequisites

- **ONNX Runtime** (libonnxruntime.so + headers) installed system-wide or in `/usr/local`
- **CMake** ≥ 3.14
- **C++17** compiler (GCC ≥ 9 or Clang ≥ 10)

## Build

```bash
cd hush_cpp
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

## Usage

```bash
./hush_enhance_onnx <input.wav> <output.wav>
```

- Input must be 16-bit PCM, mono, 16 kHz.
- Output is saved as 16-bit PCM WAV.

### Example

```bash
./hush_enhance_onnx ../test_audios/mixed.wav ../test_audios/mixed_enhanced.wav
```

## Project Structure

```
hush_cpp/
├── CMakeLists.txt          # CMake build configuration
├── README.md               # This file
├── onnx/                   # ONNX model files
│   ├── enc.onnx            # Encoder model
│   ├── erb_dec.onnx        # ERB decoder model
│   ├── df_dec.onnx         # DF decoder model
│   ├── config.ini          # Model configuration
│   └── version.txt         # Model version
├── src/                    # Source code
│   ├── hush_enhance_onnx.cpp   # Main denoising pipeline
│   ├── hush_config.h           # Configuration constants
│   ├── feature_extract.h/cc    # ERB & unit-norm feature extraction
│   ├── wav_io.h/cc             # WAV file I/O
│   └── knf/                    # kaldi-native-fbank library
│       ├── stft.h/cc           # STFT
│       ├── istft.h/cc          # ISTFT
│       ├── rfft.h/cc           # Real FFT
│       ├── feature-window.h/cc # Window functions
│       ├── kiss_fft.c/h        # Kiss FFT implementation
│       ├── kiss_fftr.c/h       # Kiss FFT real transforms
│       └── ...                 # Supporting files
├── test_audios/            # Test audio files
│   ├── mixed.wav           # Example noisy input
│   └── mixed_enhanced.wav  # Denoised output (generated)
└── build/                  # Build output directory
```

## How It Works

1. **STFT**: Audio is transformed to the frequency domain using a Vorbis window (matching libdf).
2. **Feature extraction**: ERB-band features and unit-normalized spectrogram are computed (matching libdf's normalization).
3. **ONNX inference**: Three sub-models run sequentially:
   - **Encoder** (`enc.onnx`): Extracts latent features from ERB and spectral inputs.
   - **ERB decoder** (`erb_dec.onnx`): Predicts a per-ERB-band gain mask.
   - **DF decoder** (`df_dec.onnx`): Predicts complex FIR filter coefficients for the DF (deep filter) branch.
4. **Masking & synthesis**: The ERB mask is expanded to full frequency resolution, the DF filter is applied as a complex FIR convolution, and the result is transformed back to time-domain via ISTFT.

## License

See [LICENSE](LICENSE) (Apache 2.0) — inherited from the original Hush project.
