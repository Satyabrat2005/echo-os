#include "asr.hpp"

#include "echo/config.hpp"
#include "echo/log.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#if defined(ECHO_WITH_WHISPER)
#include "whisper.h"
#endif

namespace echo::perception {
namespace {

#if defined(ECHO_WITH_WHISPER)

// Convert int16 PCM to the float32 [-1,1] mono @16 kHz whisper expects. Uses a
// cheap linear resample when the source rate differs — fine for speech ASR.
std::vector<float> to_whisper_pcm(const std::int16_t* pcm, std::size_t n, int sr) {
    std::vector<float> f32;
    if (!pcm || n == 0) return f32;
    constexpr int kTarget = 16000;
    if (sr == kTarget || sr <= 0) {
        f32.resize(n);
        for (std::size_t i = 0; i < n; ++i) f32[i] = pcm[i] / 32768.0f;
        return f32;
    }
    const double ratio = static_cast<double>(kTarget) / sr;
    const std::size_t out_n = static_cast<std::size_t>(n * ratio);
    f32.resize(out_n);
    for (std::size_t i = 0; i < out_n; ++i) {
        const double src = i / ratio;
        const std::size_t i0 = static_cast<std::size_t>(src);
        const std::size_t i1 = std::min(i0 + 1, n - 1);
        const float frac = static_cast<float>(src - i0);
        f32[i] = ((1.0f - frac) * pcm[i0] + frac * pcm[i1]) / 32768.0f;
    }
    return f32;
}

class WhisperAsr final : public IAsr {
public:
    Status initialize() override {
        const std::string model = config::whisper_model();
        whisper_context_params cparams = whisper_context_default_params();
        cparams.use_gpu = true;  // honored only if the build has a GPU backend
        ctx_ = whisper_init_from_file_with_params(model.c_str(), cparams);
        if (!ctx_) {
            log_error("perception", "whisper model failed to load; ASR disabled");
            return Status::NotReady;
        }
        char buf[160];
        std::snprintf(buf, sizeof(buf), "ASR ready (whisper.cpp, %s)", model.c_str());
        log_info("perception", buf);
        return Status::Ok;
    }

    AsrResult transcribe(const std::int16_t* pcm, std::size_t n, int sr) override {
        if (!ctx_) return {};
        std::vector<float> audio = to_whisper_pcm(pcm, n, sr);
        if (audio.size() < 1600) return {};  // < 100 ms: nothing to transcribe

        whisper_full_params wp = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        wp.language        = "en";
        wp.translate       = false;
        wp.no_timestamps   = true;
        wp.print_progress  = false;
        wp.print_realtime  = false;
        wp.print_special   = false;
        wp.single_segment  = false;
        wp.n_threads       = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));

        if (whisper_full(ctx_, wp, audio.data(), static_cast<int>(audio.size())) != 0)
            return {};

        std::string text;
        double prob_sum = 0.0;
        int    tok_count = 0;
        const int segments = whisper_full_n_segments(ctx_);
        for (int s = 0; s < segments; ++s) {
            text += whisper_full_get_segment_text(ctx_, s);
            const int ntok = whisper_full_n_tokens(ctx_, s);
            for (int t = 0; t < ntok; ++t) {
                prob_sum += whisper_full_get_token_p(ctx_, s, t);
                ++tok_count;
            }
        }
        // Average token probability is a defensible [0,1] confidence for the gate.
        const float conf = tok_count ? static_cast<float>(prob_sum / tok_count) : 0.0f;
        return AsrResult{trim(text), conf};
    }

    void shutdown() override {
        if (ctx_) { whisper_free(ctx_); ctx_ = nullptr; }
    }

private:
    static std::string trim(std::string s) {
        auto notspace = [](unsigned char c) { return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), notspace));
        s.erase(std::find_if(s.rbegin(), s.rend(), notspace).base(), s.end());
        return s;
    }
    whisper_context* ctx_ = nullptr;
};

#endif  // ECHO_WITH_WHISPER

class StubAsr final : public IAsr {
public:
    Status initialize() override {
        log_info("perception", "ASR initialized (stub: empty transcripts)");
        return Status::Ok;
    }
    AsrResult transcribe(const std::int16_t*, std::size_t, int) override { return {}; }
    void shutdown() override {}
};

}  // namespace

std::unique_ptr<IAsr> make_asr() {
#if defined(ECHO_WITH_WHISPER)
    return std::make_unique<WhisperAsr>();
#else
    return std::make_unique<StubAsr>();
#endif
}

}  // namespace echo::perception
