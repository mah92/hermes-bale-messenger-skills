/**
 * feature_extract.h
 *
 * Feature extraction for Hush denoising:
 * - ERB filterbank computation
 * - ERB features (power per band + EMA normalization)
 * - Unit norm features (EMA-normalized complex STFT)
 */

#ifndef FEATURE_EXTRACT_H_
#define FEATURE_EXTRACT_H_

#include <vector>

// Compute ERB widths matching libdf
std::vector<int> compute_erb_widths(int sr, int fft_size, int nb_erb, int min_nb_freqs);

// Compute ERB features matching libdf::erb() + erb_norm()
// spec_re, spec_im: [T, F] flattened row-major
// erb_out: [T, nb_erb] flattened row-major
void compute_erb_features(
    const float* spec_re, const float* spec_im, int T, int F,
    const std::vector<int>& erb_widths, float alpha,
    float* erb_out);

// Compute unit_norm features matching libdf::unit_norm()
// spec_re, spec_im: [T, F] flattened row-major
// out_re, out_im: [T, nb_df] flattened row-major
void compute_unit_norm(
    const float* spec_re, const float* spec_im, int T, int F,
    int nb_df, float alpha,
    float* out_re, float* out_im);

#endif  // FEATURE_EXTRACT_H_
