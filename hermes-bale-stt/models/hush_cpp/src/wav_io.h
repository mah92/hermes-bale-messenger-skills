/**
 * wav_io.h
 *
 * Simple WAV I/O for 16-bit PCM mono files.
 */

#ifndef WAV_IO_H_
#define WAV_IO_H_

#include <cstdint>
#include <string>
#include <vector>

struct WavData {
    uint32_t sample_rate = 0;
    std::vector<int16_t> samples;  // mono int16 samples
};

// Load a 16-bit PCM mono WAV file.
// Throws std::runtime_error on failure.
WavData wav_load(const std::string& path);

// Write a 16-bit PCM mono WAV file.
// Throws std::runtime_error on failure.
void wav_write_mono_i16(const std::string& path,
                        const std::vector<int16_t>& samples,
                        uint32_t sr);

#endif  // WAV_IO_H_
