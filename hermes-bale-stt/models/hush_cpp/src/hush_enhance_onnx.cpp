/**
 * hush_enhance_onnx.cpp
 *
 * C++ sample: denoise a WAV file using Hush's ONNX sub-models directly
 * via ONNX Runtime (open source, MIT license).
 *
 * Uses kaldi-native-fbank (knf) for STFT/ISTFT.
 *
 * Build (via CMake):
 *   cd hush_cpp && mkdir build && cd build
 *   cmake .. && make -j
 *
 * Usage:
 *   ./hush_enhance_onnx <input.wav> <output.wav>
 *
 * Accepts 16-bit PCM WAV, mono, 16 kHz sample rate.
 */

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <libgen.h>
#include <unistd.h>

#include <onnxruntime_cxx_api.h>

#include "stft.h"
#include "istft.h"
#include "hush_config.h"
#include "wav_io.h"
#include "feature_extract.h"

// Resolve ONNX directory relative to the executable's location.
// Falls back to cwd-relative "onnx" if /proc/self/exe is unavailable.
static std::string resolve_onnx_dir() {
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';
        std::string exe_dir = dirname(exe_path);
        std::string onnx_dir = exe_dir + "/onnx";
        // Verify the directory exists
        if (access((onnx_dir + "/enc.onnx").c_str(), F_OK) == 0)
            return onnx_dir;
    }
    return "onnx";  // fallback
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    bool quiet = false;
    int arg_idx = 1;

    // Parse flags
    for (; arg_idx < argc; ++arg_idx) {
        std::string a = argv[arg_idx];
        if (a == "--quiet" || a == "-q") {
            quiet = true;
        } else if (a[0] != '-') {
            break;  // positional args start
        }
    }

#define LOG(fmt, ...) do { if (!quiet) std::fprintf(stderr, fmt, ##__VA_ARGS__); } while(0)

    if (argc - arg_idx < 2) {
        LOG( "Usage: %s [--quiet] <input.wav> <output.wav>\n", argv[0]);
        return 1;
    }
    const std::string in_path  = argv[arg_idx];
    const std::string out_path = argv[arg_idx + 1];

    // ONNX model paths
    const std::string onnx_dir = resolve_onnx_dir();
    const std::string enc_path  = onnx_dir + "/enc.onnx";
    const std::string erb_path  = onnx_dir + "/erb_dec.onnx";
    const std::string df_path   = onnx_dir + "/df_dec.onnx";

    try {
        // -------------------------------------------------------------------
        // 1. Load audio
        // -------------------------------------------------------------------
        LOG("[1/5] Loading audio...\n");
        WavData wav = wav_load(in_path);
        if (wav.sample_rate != SR)
            throw std::runtime_error("input must be 16 kHz");
        LOG( "  Audio loaded: %.2fs, %u Hz, %zu samples\n",
                     static_cast<double>(wav.samples.size()) / wav.sample_rate,
                     wav.sample_rate, wav.samples.size());

        // Convert to float32 [-1, 1]
        std::vector<float> audio_f32(wav.samples.size());
        for (size_t i = 0; i < wav.samples.size(); ++i)
            audio_f32[i] = static_cast<float>(wav.samples[i]) / 32768.0f;

        // Pad to match libdf's behavior:
        // Python: pad(wav, (0, FFT_SIZE)) then libdf analysis with center=true
        // libdf center=true pads FFT_SIZE/2 on each side internally
        // So total padding = FFT_SIZE/2 at start + FFT_SIZE/2 + FFT_SIZE at end
        // We manually pad to match, then use center=false
        // Total size: half_fft (reflect) + audio + FFT_SIZE (zeros at end)
        int half_fft = FFT_SIZE / 2;
        std::vector<float> audio_padded(half_fft + audio_f32.size() + FFT_SIZE, 0.0f);
        // Copy audio starting at offset half_fft
        for (size_t i = 0; i < audio_f32.size(); ++i) {
            audio_padded[half_fft + i] = audio_f32[i];
        }
        // Reflect padding at start: mirror first half_fft samples
        for (int i = 0; i < half_fft; ++i) {
            audio_padded[i] = audio_f32[half_fft - 1 - i];
        }
        // Zero padding at end (the extra FFT_SIZE zeros)
        // Already zero-initialized

        // -------------------------------------------------------------------
        // 2. Feature extraction using knf::Stft with libdf window
        // -------------------------------------------------------------------
        LOG( "\n[2/5] Computing features...\n");

        std::vector<float> libdf_window_vec(LIBDF_WINDOW, LIBDF_WINDOW + FFT_SIZE);
        knf::StftConfig stft_config;
        stft_config.n_fft = FFT_SIZE;
        stft_config.hop_length = HOP_SIZE;
        stft_config.win_length = FFT_SIZE;
        stft_config.window = libdf_window_vec;
        stft_config.center = false;
        stft_config.pad_mode = "constant";
        stft_config.normalized = false;

        knf::Stft stft(stft_config);
        knf::StftResult stft_result = stft.Compute(audio_padded.data(),
                                                     static_cast<int32_t>(audio_padded.size()));

        // libdf normalization factor applied to STFT output
        // wnorm = 1 / (FFT_SIZE^2 / (2 * HOP_SIZE))
        float wnorm = 1.0f / (static_cast<float>(FFT_SIZE * FFT_SIZE) / (2.0f * HOP_SIZE));

        // Create a copy of STFT with wnorm applied for feature computation
        knf::StftResult stft_norm = stft_result;
        for (size_t i = 0; i < stft_norm.real.size(); ++i) {
            stft_norm.real[i] *= wnorm;
            stft_norm.imag[i] *= wnorm;
        }

        int T = stft_result.num_frames;
        LOG( "  STFT: T=%d, F=%d\n", T, N_FREQS);

        // ERB widths
        auto widths = compute_erb_widths(SR, FFT_SIZE, NB_ERB, MIN_NB_ERB_FREQS);

        float alpha = std::exp(-static_cast<float>(HOP_SIZE) / SR / NORM_TAU);

        // --- ERB features (from normalized STFT) ---
        std::vector<float> erb_feat(T * NB_ERB, 0.0f);
        compute_erb_features(stft_norm.real.data(), stft_norm.imag.data(),
                             T, N_FREQS, widths, alpha, erb_feat.data());

        // --- DF features (unit_norm, from normalized STFT) ---
        std::vector<float> spec_df_re(T * NB_DF, 0.0f);
        std::vector<float> spec_df_im(T * NB_DF, 0.0f);
        compute_unit_norm(stft_norm.real.data(), stft_norm.imag.data(),
                          T, N_FREQS, NB_DF, alpha,
                          spec_df_re.data(), spec_df_im.data());

        LOG( "  Features extracted\n");

        // -------------------------------------------------------------------
        // 3. Load ONNX models
        // -------------------------------------------------------------------
        LOG( "\n[3/5] Loading ONNX models...\n");

        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "hush");
        Ort::SessionOptions session_options;
        session_options.SetIntraOpNumThreads(1);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        Ort::Session sess_enc(env, enc_path.c_str(), session_options);
        Ort::Session sess_erb(env, erb_path.c_str(), session_options);
        Ort::Session sess_df(env, df_path.c_str(), session_options);

        Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(
            OrtArenaAllocator, OrtMemTypeDefault);

        LOG( "  3 ONNX models loaded\n");

        // -------------------------------------------------------------------
        // 4. Run inference
        // -------------------------------------------------------------------
        LOG( "\n[4/5] Running inference...\n");

        // Encoder inputs: feat_erb [1,1,T,32], feat_spec [1,2,T,64]
        std::vector<int64_t> erb_shape = {1, 1, T, NB_ERB};
        std::vector<int64_t> spec_shape = {1, 2, T, NB_DF};

        // Reshape feat_spec from [T,64] real/imag to [1,2,T,64]
        std::vector<float> feat_spec_enc(2 * T * NB_DF, 0.0f);
        for (int t = 0; t < T; ++t) {
            for (int f = 0; f < NB_DF; ++f) {
                feat_spec_enc[0 * T * NB_DF + t * NB_DF + f] = spec_df_re[t * NB_DF + f];
                feat_spec_enc[1 * T * NB_DF + t * NB_DF + f] = spec_df_im[t * NB_DF + f];
            }
        }

        Ort::Value erb_tensor = Ort::Value::CreateTensor<float>(
            mem_info, erb_feat.data(), erb_feat.size(), erb_shape.data(), erb_shape.size());
        Ort::Value spec_tensor = Ort::Value::CreateTensor<float>(
            mem_info, feat_spec_enc.data(), feat_spec_enc.size(), spec_shape.data(), spec_shape.size());

        // Run encoder
        const char* enc_input_names[] = {"feat_erb", "feat_spec"};
        const char* enc_output_names[] = {"e0", "e1", "e2", "e3", "emb", "c0", "lsnr"};
        std::array<Ort::Value, 2> enc_inputs{
            std::move(erb_tensor),
            std::move(spec_tensor)
        };
        std::vector<Ort::Value> enc_out = sess_enc.Run(
            Ort::RunOptions{},
            enc_input_names, enc_inputs.data(), 2,
            enc_output_names, 7);

        LOG( "  Encoder done\n");

        // Copy emb and c0 for DF decoder (Ort::Value is move-only)
        auto emb_shape = enc_out[4].GetTensorTypeAndShapeInfo().GetShape();
        size_t emb_size = enc_out[4].GetTensorTypeAndShapeInfo().GetElementCount();
        std::vector<float> emb_copy(emb_size);
        std::memcpy(emb_copy.data(), enc_out[4].GetTensorData<float>(), emb_size * sizeof(float));

        auto c0_shape = enc_out[5].GetTensorTypeAndShapeInfo().GetShape();
        size_t c0_size = enc_out[5].GetTensorTypeAndShapeInfo().GetElementCount();
        std::vector<float> c0_copy(c0_size);
        std::memcpy(c0_copy.data(), enc_out[5].GetTensorData<float>(), c0_size * sizeof(float));

        // Run ERB decoder
        const char* erb_input_names[] = {"emb", "e3", "e2", "e1", "e0"};
        const char* erb_output_names[] = {"m"};
        std::array<Ort::Value, 5> erb_inputs{
            std::move(enc_out[4]),  // emb
            std::move(enc_out[3]),  // e3
            std::move(enc_out[2]),  // e2
            std::move(enc_out[1]),  // e1
            std::move(enc_out[0])   // e0
        };
        std::vector<Ort::Value> erb_out = sess_erb.Run(
            Ort::RunOptions{},
            erb_input_names, erb_inputs.data(), 5,
            erb_output_names, 1);

        float* m_data = erb_out[0].GetTensorMutableData<float>();
        LOG( "  ERB decoder done\n");

        // Run DF decoder
        Ort::Value emb_tensor_df = Ort::Value::CreateTensor<float>(
            mem_info, emb_copy.data(), emb_copy.size(),
            emb_shape.data(), emb_shape.size());
        Ort::Value c0_tensor_df = Ort::Value::CreateTensor<float>(
            mem_info, c0_copy.data(), c0_copy.size(),
            c0_shape.data(), c0_shape.size());

        const char* df_input_names[] = {"emb", "c0"};
        const char* df_output_names[] = {"coefs"};
        std::array<Ort::Value, 2> df_inputs{
            std::move(emb_tensor_df),
            std::move(c0_tensor_df)
        };
        std::vector<Ort::Value> df_out = sess_df.Run(
            Ort::RunOptions{},
            df_input_names, df_inputs.data(), 2,
            df_output_names, 1);

        float* coefs_data = df_out[0].GetTensorMutableData<float>();
        LOG( "  DF decoder done\n");

        // -------------------------------------------------------------------
        // 5. Apply masks and synthesize
        // -------------------------------------------------------------------
        LOG( "\n[5/5] Applying masks and synthesizing...\n");

        // --- Build ERB inverse filterbank ---
        std::vector<float> fb_inv(NB_ERB * N_FREQS, 0.0f);
        {
            int b = 0;
            for (int e = 0; e < NB_ERB; ++e) {
                float inv_w = 1.0f / std::max(widths[e], 1);
                for (int j = 0; j < widths[e]; ++j) {
                    fb_inv[e * N_FREQS + b + j] = inv_w;
                }
                b += widths[e];
            }
        }

        // --- Apply ERB mask ---
        std::vector<float> mask_full(T * N_FREQS, 0.0f);
        for (int t = 0; t < T; ++t) {
            for (int f = 0; f < N_FREQS; ++f) {
                float sum = 0.0f;
                for (int e = 0; e < NB_ERB; ++e) {
                    sum += m_data[t * NB_ERB + e] * fb_inv[e * N_FREQS + f];
                }
                mask_full[t * N_FREQS + f] = sum;
            }
        }

        // --- Apply DF filter ---
        // coefs_data: [1, T, 64, 10] -> permute to [5, T, 64, 2]
        std::vector<float> coefs(DF_ORDER * T * NB_DF * 2, 0.0f);
        for (int t = 0; t < T; ++t) {
            for (int f = 0; f < NB_DF; ++f) {
                for (int o = 0; o < DF_ORDER; ++o) {
                    int src_idx = (t * NB_DF + f) * 10 + o * 2;
                    int dst_idx = (o * T + t) * NB_DF * 2 + f * 2;
                    coefs[dst_idx + 0] = coefs_data[src_idx + 0];
                    coefs[dst_idx + 1] = coefs_data[src_idx + 1];
                }
            }
        }

        // spec_df from original STFT (before unit_norm)
        std::vector<float> spec_df_2d(T * NB_DF * 2, 0.0f);
        for (int t = 0; t < T; ++t) {
            for (int f = 0; f < NB_DF; ++f) {
                spec_df_2d[(t * NB_DF + f) * 2 + 0] = stft_result.real[t * N_FREQS + f];
                spec_df_2d[(t * NB_DF + f) * 2 + 1] = stft_result.imag[t * N_FREQS + f];
            }
        }

        // Pad with zeros at beginning for order-5 filter
        std::vector<float> padded((T + DF_ORDER - 1) * NB_DF * 2, 0.0f);
        for (int t = 0; t < T; ++t) {
            for (int f = 0; f < NB_DF; ++f) {
                int src_idx = (t * NB_DF + f) * 2;
                int dst_idx = ((t + DF_ORDER - 1) * NB_DF + f) * 2;
                padded[dst_idx + 0] = spec_df_2d[src_idx + 0];
                padded[dst_idx + 1] = spec_df_2d[src_idx + 1];
            }
        }

        // Apply complex FIR filter
        std::vector<float> spec_f_re(T * NB_DF, 0.0f);
        std::vector<float> spec_f_im(T * NB_DF, 0.0f);
        for (int t = 0; t < T; ++t) {
            for (int f = 0; f < NB_DF; ++f) {
                float re_sum = 0.0f, im_sum = 0.0f;
                for (int o = 0; o < DF_ORDER; ++o) {
                    int p_idx = ((t + o) * NB_DF + f) * 2;
                    float p_re = padded[p_idx + 0];
                    float p_im = padded[p_idx + 1];

                    int c_idx = (o * T + t) * NB_DF * 2 + f * 2;
                    float c_re = coefs[c_idx + 0];
                    float c_im = coefs[c_idx + 1];

                    // Complex multiply
                    re_sum += p_re * c_re - p_im * c_im;
                    im_sum += p_im * c_re + p_re * c_im;
                }
                spec_f_re[t * NB_DF + f] = re_sum;
                spec_f_im[t * NB_DF + f] = im_sum;
            }
        }

        // Build output spectrogram
        knf::StftResult spec_enh;
        spec_enh.num_frames = T;
        spec_enh.real = stft_result.real;
        spec_enh.imag = stft_result.imag;
        for (int t = 0; t < T; ++t) {
            for (int f = 0; f < NB_DF; ++f) {
                spec_enh.real[t * N_FREQS + f] = spec_f_re[t * NB_DF + f];
                spec_enh.imag[t * N_FREQS + f] = spec_f_im[t * NB_DF + f];
            }
            for (int f = NB_DF; f < N_FREQS; ++f) {
                float m = mask_full[t * N_FREQS + f];
                spec_enh.real[t * N_FREQS + f] = stft_result.real[t * N_FREQS + f] * m;
                spec_enh.imag[t * N_FREQS + f] = stft_result.imag[t * N_FREQS + f] * m;
            }
        }

        // ISTFT using knf::IStft
        // knf::IStft with center=true trims n_fft/2 from each side
        // But Python only trims delay=160 from start, and takes len(audio)+delay samples
        // So we need to keep the extra samples at the end
        knf::IStft istft(stft_config);
        std::vector<float> enhanced_full = istft.Compute(spec_enh);

        // Trim: take from index delay to len(audio) + delay
        int delay = FFT_SIZE - HOP_SIZE;  // 160
        size_t start = delay;
        size_t end = audio_f32.size() + delay;
        if (end > enhanced_full.size()) {
            end = enhanced_full.size();
        }
        std::vector<float> enhanced(enhanced_full.begin() + start,
                                    enhanced_full.begin() + end);

        // Convert to int16 and save
        std::vector<int16_t> out_i16(enhanced.size());
        for (size_t i = 0; i < enhanced.size(); ++i) {
            float v = std::clamp(enhanced[i] * 32768.0f, -32768.0f, 32767.0f);
            out_i16[i] = static_cast<int16_t>(v);
        }

        wav_write_mono_i16(out_path, out_i16, SR);
        LOG( "  Output saved to: %s\n", out_path.c_str());

        // Stats
        float max_val = 0.0f, min_val = 0.0f, mean_val = 0.0f;
        for (auto s : enhanced) {
            max_val = std::max(max_val, s);
            min_val = std::min(min_val, s);
            mean_val += s;
        }
        mean_val /= enhanced.size();
        float sq_sum = 0.0f;
        for (auto s : enhanced) sq_sum += s * s;
        float rms = std::sqrt(sq_sum / enhanced.size());
        LOG( "  Stats: max=%.4f, min=%.4f, mean=%.6f, rms=%.4f\n",
                     max_val, min_val, mean_val, rms);

        LOG( "\nDone! Enhanced audio saved to '%s'\n", out_path.c_str());

    } catch (const std::exception& e) {
        LOG( "Error: %s\n", e.what());
        return 1;
    }
    return 0;
}
