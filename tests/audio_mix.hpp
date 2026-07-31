// ECHO OS tests — deterministic noise mixing at a defined SNR (Phase 19).
//
// Mixes a clean speech/wake clip with a noise bed at a target signal-to-noise
// ratio, so the real-engine robustness test can measure ASR/wake-word behaviour at
// several defined noise levels. The mix is a pure, deterministic function of its
// inputs: same clean clip + same bed + same SNR -> same bytes, every run and every
// platform (no RNG here — the randomness lives only in the committed/generated
// beds, which are checksummed; see MANIFEST.md).
//
// SNR definition (matches the honest convention used in docs/STATE.md): the noise
// bed is scaled so that 20*log10(rms_speech / rms_noise) == target_snr_db, i.e. a
// larger SNR means quieter noise, "clean" means no noise added at all. The bed is
// tiled/truncated to the clip length so any bed length works.
#pragma once

#include "echo/audio_dsp.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace echo::test {

// Mix `noise` into `clean` at `snr_db`. The bed is tiled to the clip length. The
// noise is scaled to hit the requested SNR relative to the clean clip's own RMS;
// the result is clamped to int16. `snr_db` = +infinity (or a very large value) mixes
// nothing.
inline std::vector<std::int16_t> mix_at_snr(const std::vector<std::int16_t>& clean,
                                            const std::vector<std::int16_t>& noise,
                                            float snr_db) {
    std::vector<std::int16_t> out = clean;
    if (clean.empty() || noise.empty()) return out;

    const double sig_rms = echo::audio::rms(clean.data(), clean.size());
    double noi_rms = echo::audio::rms(noise.data(), noise.size());
    if (sig_rms <= 0.0 || noi_rms <= 0.0) return out;

    // Desired noise RMS so that 20*log10(sig/noise) == snr_db.
    const double desired_noise_rms = sig_rms / std::pow(10.0, snr_db / 20.0);
    const double scale = desired_noise_rms / noi_rms;

    for (std::size_t i = 0; i < clean.size(); ++i) {
        const double bed = static_cast<double>(noise[i % noise.size()]) * scale;
        const double mixed = static_cast<double>(clean[i]) + bed;
        out[i] = static_cast<std::int16_t>(
            std::clamp(std::round(mixed), -32768.0, 32767.0));
    }
    return out;
}

}  // namespace echo::test
