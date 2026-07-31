// ECHO OS — single-microphone audio DSP (Phase 19).
//
// Dependency-free, header-only signal-processing primitives shared by the capture
// path and the perception stage:
//
//   * rms()               — level of a mono int16 window.
//   * estimate_snr_db()   — a coarse, honest speech-vs-noise SNR estimate.
//   * noise_confidence()  — maps that SNR to a [0,1] confidence so noisy audio
//                           routes through the EXISTING safe-mode / "ask again"
//                           gate rather than a new mechanism (Phase 17 vocabulary).
//   * AudioPreprocessor   — high-pass + noise-floor gate + AGC, a basic real
//                           pre-processing pass evaluated against noisy fixtures.
//
// SINGLE-MIC BY DESIGN. The real capture path is one mono I2S/SDL microphone
// (sensor-pipeline/src/real_sources.cpp: SDL 16 kHz mono int16 — no array), so
// there is deliberately NO beamforming / multi-mic algorithm here. If the glasses
// ever ship a mic array, this is where a beamformer would be added; today the
// hardware is single-mic, so building for an assumed array would be dishonest.
//
// Nothing here allocates on a real hot path beyond the output buffer, uses only the
// standard library, and is fully deterministic (no RNG), so it is exercised
// directly in CI. See tests/audio_robustness_test.cpp for the measured behaviour.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace echo::audio {

// Root-mean-square level of a mono int16 window. 0 for an empty window.
inline double rms(const std::int16_t* pcm, std::size_t n) {
    if (!pcm || n == 0) return 0.0;
    double acc = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double s = static_cast<double>(pcm[i]);
        acc += s * s;
    }
    return std::sqrt(acc / static_cast<double>(n));
}

// A clean-audio SNR estimate that reads as "much higher than any real recording":
// used as the sentinel when the noise floor is effectively silent.
inline constexpr float kCleanSnrDb = 60.0f;

// Estimate the speech-to-noise ratio (dB) of a mono utterance with no model and no
// tuning knobs beyond the analysis window: split the clip into ~20 ms windows,
// take a low percentile of window energy as the noise floor and a high percentile
// as speech+noise, and report 10*log10(speech_power / noise_power). This is a
// coarse *stationary-noise* estimate — it assumes the quietest stretches are noise
// alone, which holds for a wearer speaking over steady background sound. It is
// deliberately simple and honest about that: it is a gate signal, not a calibrated
// measurement. Returns kCleanSnrDb when the floor is essentially silent.
inline float estimate_snr_db(const std::int16_t* pcm, std::size_t n, int sample_rate) {
    if (!pcm || n == 0) return 0.0f;
    const int sr = sample_rate > 0 ? sample_rate : 16000;
    std::size_t win = static_cast<std::size_t>(sr) / 50;  // ~20 ms
    if (win < 64) win = 64;
    if (n < win * 2) {
        // Too short to separate speech from noise; report the whole-clip level as
        // "clean enough" rather than guess a floor. A sub-40 ms window never reaches
        // ASR anyway (the endpointer needs >= 300 ms).
        return kCleanSnrDb;
    }

    std::vector<double> powers;
    powers.reserve(n / win + 1);
    for (std::size_t off = 0; off + win <= n; off += win) {
        double acc = 0.0;
        for (std::size_t i = 0; i < win; ++i) {
            const double s = static_cast<double>(pcm[off + i]);
            acc += s * s;
        }
        powers.push_back(acc / static_cast<double>(win));  // mean power per window
    }
    if (powers.size() < 2) return kCleanSnrDb;

    std::sort(powers.begin(), powers.end());
    // 10th-percentile window = noise floor; 90th-percentile window = speech+noise.
    const std::size_t lo_idx = powers.size() / 10;
    const std::size_t hi_idx = (powers.size() * 9) / 10;
    const double noise_power  = powers[lo_idx];
    const double signal_power = powers[std::min(hi_idx, powers.size() - 1)];

    // A near-silent floor means we cannot see any noise: treat as clean.
    if (noise_power <= 1.0) return kCleanSnrDb;

    const double speech_power = signal_power - noise_power;  // remove the noise that rides in the loud windows too
    if (speech_power <= noise_power * 1e-3) return -kCleanSnrDb;  // no speech above the floor
    const double snr = 10.0 * std::log10(speech_power / noise_power);
    return static_cast<float>(std::clamp(snr, static_cast<double>(-kCleanSnrDb),
                                         static_cast<double>(kCleanSnrDb)));
}

// SNR band over which confidence ramps from "unusable" to "clean". Deliberately
// CONSERVATIVE for a device worn by someone with memory loss: below ~3 dB the
// audio is treated as unusable (confidence -> 0, which the 0.72 safe-mode gate
// turns into a calm "ask again"); it only reaches full confidence by ~18 dB. Being
// too eager to trust noisy audio and confidently mis-hearing is exactly the failure
// this product must avoid (principle #5, "fail safe, not smart").
inline constexpr float kSnrFloorDb   = 3.0f;
inline constexpr float kSnrCleanDb   = 18.0f;

// Map an SNR estimate to a [0,1] confidence. This is NOT a new alerting path: it is
// folded into the transcript confidence so the pre-existing safe-mode gate and the
// Phase-17 "ASR degraded -> ask again calmly" fallback handle noisy audio with no
// parallel mechanism (Phase 19 constraint #3).
inline float noise_confidence(float snr_db) {
    if (snr_db >= kSnrCleanDb) return 1.0f;
    if (snr_db <= kSnrFloorDb) return 0.0f;
    return (snr_db - kSnrFloorDb) / (kSnrCleanDb - kSnrFloorDb);
}

// Tunables for the pre-processing pass. Defaults are a reasonable starting point
// for 16 kHz speech; the evaluation in tests measures whether they actually help.
struct PreprocessConfig {
    float target_rms = 3000.0f;  // AGC target level (int16 RMS)
    float max_gain   = 8.0f;     // cap so pure noise is never boosted to full scale
    float min_gain   = 0.25f;    // floor so a loud clip is attenuated, not silenced
    float hp_alpha   = 0.97f;    // first-order high-pass coefficient (removes DC/hum)
    bool  high_pass  = true;
    bool  agc        = true;
    bool  gate       = true;     // gentle downward expansion below the noise floor
    float gate_ratio = 0.5f;     // attenuation applied to sub-floor windows (0..1)
};

// A basic single-mic pre-processing stage: DC/low-frequency removal, a mild
// noise-floor gate, and automatic gain control. Real DSP, not a stub — but
// deliberately basic (constraint: no beamforming on a single-mic pipeline). It
// exposes both the processed audio and the before/after SNR so the evaluation can
// report whether it measurably helps rather than assuming it does.
class AudioPreprocessor {
public:
    AudioPreprocessor() = default;
    explicit AudioPreprocessor(const PreprocessConfig& cfg) : cfg_(cfg) {}

    struct Result {
        std::vector<std::int16_t> samples;
        float in_snr_db  = 0.0f;
        float out_snr_db = 0.0f;
    };

    // Offline (whole-clip) form used by the capture path per buffer and by the
    // evaluation. Stateless across calls so a test gets identical bytes every run.
    Result process(const std::int16_t* pcm, std::size_t n, int sample_rate) const {
        Result r;
        r.in_snr_db = estimate_snr_db(pcm, n, sample_rate);
        if (!pcm || n == 0) return r;

        std::vector<float> x(n);
        for (std::size_t i = 0; i < n; ++i) x[i] = static_cast<float>(pcm[i]);

        // 1) First-order high-pass: y[i] = a*(y[i-1] + x[i] - x[i-1]). Removes DC and
        //    low-frequency rumble (a steady hum bed lives mostly down here).
        if (cfg_.high_pass) {
            float prev_x = 0.0f, prev_y = 0.0f;
            for (std::size_t i = 0; i < n; ++i) {
                const float xi = x[i];
                const float yi = cfg_.hp_alpha * (prev_y + xi - prev_x);
                x[i] = yi;
                prev_x = xi;
                prev_y = yi;
            }
        }

        // 2) Noise gate (gentle downward expansion): windows whose energy sits at
        //    the noise floor are attenuated, so between-word gaps get quieter. Kept
        //    mild (does not fully mute) so it never chops the front of a word.
        if (cfg_.gate) apply_gate(x, sample_rate);

        // 3) AGC: scale the whole clip toward the target RMS, bounded so pure noise
        //    is not amplified to full scale and a loud clip is only trimmed.
        if (cfg_.agc) {
            double cur = 0.0;
            for (float v : x) cur += static_cast<double>(v) * v;
            cur = std::sqrt(cur / static_cast<double>(n));
            if (cur > 1.0) {
                float gain = static_cast<float>(cfg_.target_rms / cur);
                gain = std::clamp(gain, cfg_.min_gain, cfg_.max_gain);
                for (float& v : x) v *= gain;
            }
        }

        r.samples.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            const float v = std::round(x[i]);
            r.samples[i] = static_cast<std::int16_t>(
                std::clamp(v, -32768.0f, 32767.0f));
        }
        r.out_snr_db = estimate_snr_db(r.samples.data(), r.samples.size(), sample_rate);
        return r;
    }

    const PreprocessConfig& config() const noexcept { return cfg_; }

private:
    void apply_gate(std::vector<float>& x, int sample_rate) const {
        const int sr = sample_rate > 0 ? sample_rate : 16000;
        std::size_t win = static_cast<std::size_t>(sr) / 50;  // ~20 ms
        if (win < 64) win = 64;
        const std::size_t n = x.size();
        if (n < win * 2) return;

        // Estimate the noise floor as the 10th-percentile window RMS.
        std::vector<double> levels;
        levels.reserve(n / win + 1);
        for (std::size_t off = 0; off + win <= n; off += win) {
            double acc = 0.0;
            for (std::size_t i = 0; i < win; ++i) acc += static_cast<double>(x[off + i]) * x[off + i];
            levels.push_back(std::sqrt(acc / static_cast<double>(win)));
        }
        std::vector<double> sorted = levels;
        std::sort(sorted.begin(), sorted.end());
        const double floor = sorted[sorted.size() / 10];
        const double thresh = floor * 1.5;  // windows at/near the floor are "noise only"

        std::size_t wi = 0;
        for (std::size_t off = 0; off + win <= n; off += win, ++wi) {
            if (levels[wi] <= thresh) {
                for (std::size_t i = 0; i < win; ++i) x[off + i] *= cfg_.gate_ratio;
            }
        }
    }

    PreprocessConfig cfg_{};
};

}  // namespace echo::audio
