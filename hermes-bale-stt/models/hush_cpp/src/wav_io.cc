/**
 * wav_io.cc
 *
 * Simple WAV I/O for 16-bit PCM mono files.
 */

#include "wav_io.h"

#include <cstring>
#include <fstream>
#include <stdexcept>

WavData wav_load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);

    auto rd_u16 = [&]() -> uint16_t {
        unsigned char b[2];
        f.read(reinterpret_cast<char*>(b), 2);
        return static_cast<uint16_t>(b[0]) | (static_cast<uint16_t>(b[1]) << 8);
    };
    auto rd_u32 = [&]() -> uint32_t {
        unsigned char b[4];
        f.read(reinterpret_cast<char*>(b), 4);
        return static_cast<uint32_t>(b[0])
             | (static_cast<uint32_t>(b[1]) << 8)
             | (static_cast<uint32_t>(b[2]) << 16)
             | (static_cast<uint32_t>(b[3]) << 24);
    };

    char id[4];
    f.read(id, 4);
    rd_u32();  // riff size
    char wave[4];
    f.read(wave, 4);
    if (std::memcmp(id, "RIFF", 4) != 0 || std::memcmp(wave, "WAVE", 4) != 0)
        throw std::runtime_error("not a RIFF/WAVE file");

    WavData w;
    bool got_fmt = false;
    uint16_t fmt_tag = 0, channels = 0, bits = 0;

    while (f) {
        char cid[4];
        f.read(cid, 4);
        if (f.gcount() != 4) throw std::runtime_error("EOF in chunk header");
        uint32_t sz = rd_u32();

        if (std::memcmp(cid, "fmt ", 4) == 0) {
            std::streampos start = f.tellg();
            fmt_tag  = rd_u16();
            channels = rd_u16();
            w.sample_rate = rd_u32();
            rd_u32();  // byte rate
            rd_u16();  // block align
            bits = rd_u16();
            std::streamoff consumed = f.tellg() - start;
            if (static_cast<std::streamoff>(sz) > consumed)
                f.seekg(sz - consumed, std::ios::cur);
            if (sz & 1) f.seekg(1, std::ios::cur);
            got_fmt = true;
        } else if (std::memcmp(cid, "data", 4) == 0) {
            if (!got_fmt) throw std::runtime_error("data before fmt");
            if (fmt_tag != 1) throw std::runtime_error("only PCM supported");
            if (bits != 16) throw std::runtime_error("only 16-bit PCM supported");
            if (channels != 1) throw std::runtime_error("only mono supported");

            size_t total = sz / sizeof(int16_t);
            w.samples.resize(total);
            f.read(reinterpret_cast<char*>(w.samples.data()), sz);
            if (static_cast<size_t>(f.gcount()) != sz)
                throw std::runtime_error("truncated data chunk");
            return w;
        } else {
            f.seekg(sz + (sz & 1), std::ios::cur);
        }
    }
    throw std::runtime_error("no data chunk");
}

void wav_write_mono_i16(const std::string& path,
                        const std::vector<int16_t>& samples,
                        uint32_t sr) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);

    auto wr_u16 = [&](uint16_t v) {
        unsigned char b[2] = {
            static_cast<unsigned char>(v & 0xff),
            static_cast<unsigned char>((v >> 8) & 0xff)};
        f.write(reinterpret_cast<const char*>(b), 2);
    };
    auto wr_u32 = [&](uint32_t v) {
        unsigned char b[4] = {
            static_cast<unsigned char>(v & 0xff),
            static_cast<unsigned char>((v >> 8) & 0xff),
            static_cast<unsigned char>((v >> 16) & 0xff),
            static_cast<unsigned char>((v >> 24) & 0xff)};
        f.write(reinterpret_cast<const char*>(b), 4);
    };

    const uint32_t data_bytes = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    const uint16_t channels = 1, bits = 16, block_align = 2, fmt_tag = 1;
    const uint32_t byte_rate = sr * channels * (bits / 8);
    const uint32_t riff_size = 4 + (8 + 16) + (8 + data_bytes);

    f.write("RIFF", 4); wr_u32(riff_size); f.write("WAVE", 4);
    f.write("fmt ", 4); wr_u32(16);
    wr_u16(fmt_tag); wr_u16(channels);
    wr_u32(sr); wr_u32(byte_rate);
    wr_u16(block_align); wr_u16(bits);
    f.write("data", 4); wr_u32(data_bytes);
    f.write(reinterpret_cast<const char*>(samples.data()), data_bytes);
}
