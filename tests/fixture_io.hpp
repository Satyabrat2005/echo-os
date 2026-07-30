// ECHO OS tests — dependency-free fixture loaders.
//
// Reads the checked-in WAV/PPM fixtures (see tests/fixtures/README.md) into plain
// byte/sample buffers with no image or audio library, so the harness stays as
// zero-dependency as the stub build it exercises. The loaded buffers own their
// memory; a SensorFrame built over them is just a view (echo::SensorFrame never
// copies the bytes), so keep the loader result alive for as long as the frame is
// used.
#pragma once

#include <cctype>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#ifndef ECHO_FIXTURES_DIR
#error "ECHO_FIXTURES_DIR must be defined by the build (path to tests/fixtures)"
#endif

namespace echo::test {

inline std::string fixtures_dir() { return ECHO_FIXTURES_DIR; }
inline std::string fixture_path(const std::string& name) {
    return fixtures_dir() + "/" + name;
}

// ---- WAV (canonical PCM int16) ---------------------------------------------

struct WavClip {
    bool                      ok = false;
    int                       sample_rate = 0;
    int                       channels = 0;
    std::vector<std::int16_t> samples;  // interleaved if channels > 1
};

inline std::uint32_t rd_u32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
inline std::uint16_t rd_u16(const unsigned char* p) {
    return static_cast<std::uint16_t>(p[0]) | static_cast<std::uint16_t>(p[1] << 8);
}

// Minimal RIFF/WAVE reader: walks chunks, honors the fmt/data pair, and accepts
// only 16-bit PCM (what the fixtures and the wake-word/ASR path use).
inline WavClip load_wav(const std::string& name) {
    WavClip clip;
    std::ifstream f(fixture_path(name), std::ios::binary);
    if (!f) return clip;
    std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
    if (b.size() < 12 || std::string(b.begin(), b.begin() + 4) != "RIFF" ||
        std::string(b.begin() + 8, b.begin() + 12) != "WAVE")
        return clip;

    std::uint16_t bits = 0;
    std::size_t pos = 12;
    const unsigned char* base = b.data();
    while (pos + 8 <= b.size()) {
        const std::string id(b.begin() + pos, b.begin() + pos + 4);
        const std::uint32_t sz = rd_u32(base + pos + 4);
        const std::size_t body = pos + 8;
        if (body + sz > b.size()) break;
        if (id == "fmt " && sz >= 16) {
            clip.channels    = rd_u16(base + body + 2);
            clip.sample_rate = static_cast<int>(rd_u32(base + body + 4));
            bits             = rd_u16(base + body + 14);
        } else if (id == "data") {
            if (bits == 16) {
                const std::size_t n = sz / 2;
                clip.samples.resize(n);
                for (std::size_t i = 0; i < n; ++i)
                    clip.samples[i] = static_cast<std::int16_t>(rd_u16(base + body + i * 2));
            }
        }
        pos = body + sz + (sz & 1);  // chunks are word-aligned
    }
    clip.ok = (bits == 16 && clip.sample_rate > 0 && !clip.samples.empty());
    return clip;
}

// ---- PPM (binary P6) --------------------------------------------------------

struct Image {
    bool                      ok = false;
    int                       width = 0;
    int                       height = 0;
    std::vector<std::uint8_t> bgr;  // width*height*3 bytes (treated as BGR-shaped)
};

// Reads a binary P6 PPM. Tolerates '#' comment lines in the header even though
// our generator emits none.
inline Image load_ppm(const std::string& name) {
    Image img;
    std::ifstream f(fixture_path(name), std::ios::binary);
    if (!f) return img;
    std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
    std::size_t pos = 0;
    auto skip_ws = [&] {
        while (pos < b.size()) {
            if (b[pos] == '#') { while (pos < b.size() && b[pos] != '\n') ++pos; }
            else if (std::isspace(b[pos])) ++pos;
            else break;
        }
    };
    auto read_int = [&](int& out) -> bool {
        skip_ws();
        if (pos >= b.size() || !std::isdigit(b[pos])) return false;
        int v = 0;
        while (pos < b.size() && std::isdigit(b[pos])) v = v * 10 + (b[pos++] - '0');
        out = v;
        return true;
    };
    if (b.size() < 2 || b[0] != 'P' || b[1] != '6') return img;
    pos = 2;
    int maxval = 0;
    if (!read_int(img.width) || !read_int(img.height) || !read_int(maxval)) return img;
    ++pos;  // single whitespace after maxval precedes the binary block
    const std::size_t need = static_cast<std::size_t>(img.width) * img.height * 3;
    if (img.width <= 0 || img.height <= 0 || pos + need > b.size()) return img;
    img.bgr.assign(b.begin() + pos, b.begin() + pos + need);
    img.ok = true;
    return img;
}

}  // namespace echo::test
