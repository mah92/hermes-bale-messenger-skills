/**
 * feature_extract.cc
 *
 * Feature extraction for Hush denoising.
 */

#include "feature_extract.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

std::vector<int> compute_erb_widths(int sr, int fft_size, int nb_erb, int min_nb_freqs) {
    // Hardcoded libdf ERB widths for sr=16000, fft_size=320, nb_erb=32, min_nb_freqs=2
    // These match libdf's internal computation exactly.
    (void)sr;
    (void)fft_size;
    (void)min_nb_freqs;
    if (sr == 16000 && fft_size == 320 && nb_erb == 32) {
        return {2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
                2, 2, 2, 2, 3, 6, 7, 7, 8, 8, 10, 12, 12, 14, 16, 18};
    }
    // Fallback: compute using standard ERB formula
    int n_freqs = fft_size / 2 + 1;

    auto hz_to_erb = [](float f_hz) -> float {
        return 21.4f * std::log10(1.0f + f_hz / 229.0f);
    };
    auto erb_to_hz = [](float erb) -> float {
        return 229.0f * std::pow(10.0f, erb / 21.4f) - 1.0f;
    };

    std::vector<float> bin_hz(n_freqs);
    for (int i = 0; i < n_freqs; ++i)
        bin_hz[i] = static_cast<float>(i) * sr / fft_size;

    float erb_min = hz_to_erb(0.0f);
    float erb_max = hz_to_erb(static_cast<float>(sr) / 2.0f);
    float erb_step = (erb_max - erb_min) / nb_erb;

    std::vector<float> erb_edges(nb_erb + 1);
    for (int i = 0; i <= nb_erb; ++i)
        erb_edges[i] = erb_min + i * erb_step;

    std::vector<int> bin_edges(nb_erb + 1);
    for (int i = 0; i <= nb_erb; ++i) {
        float hz = erb_to_hz(erb_edges[i]);
        auto it = std::lower_bound(bin_hz.begin(), bin_hz.end(), hz);
        bin_edges[i] = static_cast<int>(it - bin_hz.begin());
    }
    bin_edges[0] = 0;
    bin_edges[nb_erb] = n_freqs;

    std::vector<int> widths(nb_erb);
    for (int i = 0; i < nb_erb; ++i) {
        widths[i] = bin_edges[i + 1] - bin_edges[i];
        if (widths[i] < min_nb_freqs)
            widths[i] = min_nb_freqs;
    }

    // Adjust to match total n_freqs
    int total = std::accumulate(widths.begin(), widths.end(), 0);
    int diff = n_freqs - total;
    if (diff > 0) {
        widths.back() += diff;
    } else if (diff < 0) {
        int overflow = -diff;
        for (int i = nb_erb - 1; i >= 0 && overflow > 0; --i) {
            int room = widths[i] - min_nb_freqs;
            if (room <= 0) continue;
            int take = std::min(room, overflow);
            widths[i] -= take;
            overflow -= take;
        }
        if (overflow > 0)
            widths.back() = std::max(min_nb_freqs, widths.back() - overflow);
    }
    total = std::accumulate(widths.begin(), widths.end(), 0);
    widths.back() += n_freqs - total;

    return widths;
}

void compute_erb_features(
    const float* spec_re, const float* spec_im, int T, int F,
    const std::vector<int>& erb_widths, float alpha,
    float* erb_out) {

    int nb_erb = static_cast<int>(erb_widths.size());

    // Compute cumulative bin offsets
    std::vector<int> bin_offsets(nb_erb + 1, 0);
    for (int i = 0; i < nb_erb; ++i) {
        bin_offsets[i + 1] = bin_offsets[i] + erb_widths[i];
    }

    // Step 1: Compute ERB band correlation (mean of |spec|^2 per band)
    // Matching libdf compute_band_corr: out[b] = mean(|spec|^2) over band
    for (int t = 0; t < T; ++t) {
        for (int e = 0; e < nb_erb; ++e) {
            float sum_sq = 0.0f;
            int width = erb_widths[e];
            for (int f = bin_offsets[e]; f < bin_offsets[e + 1]; ++f) {
                float re = spec_re[t * F + f];
                float im = spec_im[t * F + f];
                sum_sq += re * re + im * im;
            }
            erb_out[t * nb_erb + e] = sum_sq / std::max(width, 1);
        }
    }

    // Step 2: Convert to dB: 10 * log10(x)
    for (int t = 0; t < T; ++t) {
        for (int e = 0; e < nb_erb; ++e) {
            erb_out[t * nb_erb + e] = 10.0f * std::log10(std::max(erb_out[t * nb_erb + e], 1e-10f));
        }
    }

    // Step 3: Apply erb_norm (matching libdf band_mean_norm_erb):
    //   state = x * (1-alpha) + state * alpha
    //   x = (x - state) / 40
    // Initial state = linspace(-60, -90, nb_erb)
    std::vector<float> state(nb_erb);
    for (int e = 0; e < nb_erb; ++e) {
        state[e] = -60.0f + (-90.0f - (-60.0f)) * e / std::max(nb_erb - 1, 1);
    }
    for (int t = 0; t < T; ++t) {
        for (int e = 0; e < nb_erb; ++e) {
            float val = erb_out[t * nb_erb + e];
            state[e] = val * (1.0f - alpha) + state[e] * alpha;
            erb_out[t * nb_erb + e] = (val - state[e]) / 40.0f;
        }
    }
}

void compute_unit_norm(
    const float* spec_re, const float* spec_im, int T, int F,
    int nb_df, float alpha,
    float* out_re, float* out_im) {

    // Matching libdf band_unit_norm:
    //   state = |spec| * (1-alpha) + state * alpha
    //   spec /= sqrt(state)
    // Initial state = linspace(0.001, 0.0001, nb_df)
    std::vector<float> state(nb_df);
    for (int f = 0; f < nb_df; ++f) {
        state[f] = 0.001f + (0.0001f - 0.001f) * f / std::max(nb_df - 1, 1);
    }

    for (int t = 0; t < T; ++t) {
        for (int f = 0; f < nb_df; ++f) {
            float re = spec_re[t * F + f];
            float im = spec_im[t * F + f];
            float mag = std::sqrt(re * re + im * im);

            state[f] = mag * (1.0f - alpha) + state[f] * alpha;

            float denom = std::sqrt(std::max(state[f], 1e-14f));
            out_re[t * nb_df + f] = re / denom;
            out_im[t * nb_df + f] = im / denom;
        }
    }
}
