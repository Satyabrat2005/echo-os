#include "wake_word.hpp"

#include "echo/config.hpp"
#include "echo/log.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

#if defined(ECHO_WITH_PORCUPINE)
#include "pv_porcupine.h"
#include <fstream>
#endif

#if defined(ECHO_WITH_OPENWAKEWORD)
#include "onnxruntime_cxx_api.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>
#endif

namespace echo::perception {
namespace {

#if defined(ECHO_WITH_PORCUPINE)

// Read the one-time AccessKey. Porcupine validates it OFFLINE; obtaining it is a
// one-time setup step, not a runtime network call (see README "Phase 3").
std::string load_access_key() {
    std::string k = config::porcupine_access_key();
    // If it looks like a path, read the file; otherwise treat it as the key.
    std::ifstream f(k);
    if (f) {
        std::string line;
        std::getline(f, line);
        // trim trailing whitespace/newline
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (!line.empty()) return line;
    }
    return k;  // was the key itself, not a path
}

class PorcupineWakeWord final : public IWakeWord {
public:
    Status initialize() override {
        const std::string access_key   = load_access_key();
        const std::string params_path  = config::porcupine_params();
        const std::string keyword_path = config::porcupine_keyword();
        const char* keyword_paths[] = { keyword_path.c_str() };
        const float sensitivities[] = { 0.6f };  // balance false-accepts vs misses

        pv_status_t st = pv_porcupine_init(
            access_key.c_str(), params_path.c_str(),
            1, keyword_paths, sensitivities, &handle_);
        if (st != PV_STATUS_SUCCESS || !handle_) {
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                          "porcupine init failed (%s); wake-word disabled",
                          pv_status_to_string(st));
            log_error("perception", buf);
            handle_ = nullptr;
            return Status::NotReady;
        }
        log_info("perception", "wake-word ready (Porcupine, 'Hey ECHO')");
        return Status::Ok;
    }

    int sample_rate()  const noexcept override { return pv_sample_rate(); }
    int frame_length() const noexcept override { return pv_porcupine_frame_length(); }

    WakeResult process(const std::int16_t* pcm, std::size_t n) override {
        if (!handle_ || static_cast<int>(n) < pv_porcupine_frame_length()) return {};
        std::int32_t keyword_index = -1;
        pv_status_t st = pv_porcupine_process(handle_, pcm, &keyword_index);
        if (st != PV_STATUS_SUCCESS) return {};
        // Porcupine returns a binary decision; report a high fixed confidence on a
        // hit so the aggregate gate treats a spotted wake-word as strong evidence.
        return WakeResult{ keyword_index >= 0, keyword_index >= 0 ? 0.95f : 0.0f };
    }

    void shutdown() override {
        if (handle_) { pv_porcupine_delete(handle_); handle_ = nullptr; }
    }

private:
    pv_porcupine_t* handle_ = nullptr;
};

#endif  // ECHO_WITH_PORCUPINE

#if defined(ECHO_WITH_OPENWAKEWORD)

// --- openWakeWord (Phase 13): the account-free wake-word backend --------------
//
// openWakeWord runs a fixed three-model ONNX pipeline, all fetched free
// (Apache-2.0), so unlike Porcupine it needs no Picovoice account/key. The stages
// (see MANIFEST.md and tests/fixtures/README.md for the pinned models):
//
//   1. melspectrogram.onnx : raw 16 kHz int16 audio (as float, NOT normalized) ->
//      a (frames x 32) log-mel spectrogram. openWakeWord applies `x/10 + 2` to the
//      model output to match the scale its embedding model was trained against.
//   2. embedding_model.onnx : a sliding 76-frame x 32-mel window (step 8 frames) ->
//      a 96-dim Google speech-embedding per window.
//   3. <keyword>.onnx       : a sliding 16-embedding window -> one score in [0,1].
//
// A detection is the max classifier score over the recent audio crossing a
// threshold (openWakeWord's own examples use 0.5). We hold a bounded rolling
// audio buffer (~3 s — a wake phrase is well under that) and, each frame, recompute
// the pipeline over it and report the peak score. This is the straightforward,
// non-incremental realization: correct and easy to verify against fixtures, at the
// cost of recomputing the shared feature front-end each hop. Incrementally caching
// the mel/embedding buffers (as upstream does) is a pure-performance refinement and
// is noted as a follow-up in docs/STATE.md — it does not change the detection math.
//
// Tensor input/output NAMES are queried from each session at load time rather than
// hard-coded, so a re-exported model with renamed I/O still works.

constexpr int   kSampleRate      = 16000;
constexpr int   kFrameLength      = 1280;   // openWakeWord's 80 ms audio hop
constexpr int   kMelBins          = 32;
constexpr int   kEmbWindowFrames  = 76;     // mel frames per embedding window
constexpr int   kEmbStride        = 8;      // mel-frame step between windows
constexpr int   kEmbDim           = 96;     // speech-embedding dimensionality
constexpr int   kClsWindowEmbs    = 16;     // embeddings per classifier window
constexpr std::size_t kMaxSamples = 3 * kSampleRate;  // bounded rolling buffer
// Minimum audio (~2.5 s) the pipeline scores over. A wake phrase is well under a
// second, but forming even one 16-embedding classifier window needs ~2 s of mel
// frames, so a short utterance is LEFT-padded with zeros to this length. That
// mirrors openWakeWord's own streaming buffers, which start zero-filled — i.e. the
// wake word spoken after a stretch of silence — so it is faithful, not a fudge.
constexpr std::size_t kMinScoreSamples = 40000;

// ONNX Runtime's path argument is `char*` on POSIX but `wchar_t*` on Windows.
// This backend is exercised in Linux CI, but keep the widening so the TU also
// compiles on a Windows toolchain.
#if defined(_WIN32)
std::wstring ort_path(const std::string& s) { return std::wstring(s.begin(), s.end()); }
#else
std::string ort_path(const std::string& s) { return s; }
#endif

// A loaded ONNX model plus its queried I/O names. run() feeds one flat float
// tensor of the given shape and returns the flat output tensor.
class OrtModel {
public:
    bool load(Ort::Env& env, const std::string& path) {
        if (!std::filesystem::exists(path)) return false;
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(1);   // tiny models; avoid oversubscription
        opts.SetInterOpNumThreads(1);
        opts.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
        session_ = Ort::Session(env, ort_path(path).c_str(), opts);
        Ort::AllocatorWithDefaultOptions alloc;
        in_name_  = session_.GetInputNameAllocated(0, alloc).get();
        out_name_ = session_.GetOutputNameAllocated(0, alloc).get();
        return true;
    }

    // Run with a single float input tensor of `shape`; returns the flat output.
    std::vector<float> run(const std::vector<float>& data,
                           const std::vector<std::int64_t>& shape) {
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input = Ort::Value::CreateTensor<float>(
            mem, const_cast<float*>(data.data()), data.size(),
            shape.data(), shape.size());
        const char* in_names[]  = { in_name_.c_str() };
        const char* out_names[] = { out_name_.c_str() };
        auto outs = session_.Run(Ort::RunOptions{nullptr},
                                 in_names, &input, 1, out_names, 1);
        auto info = outs[0].GetTensorTypeAndShapeInfo();
        std::size_t n = info.GetElementCount();
        const float* p = outs[0].GetTensorMutableData<float>();
        return std::vector<float>(p, p + n);
    }

private:
    Ort::Session session_{nullptr};
    std::string  in_name_;
    std::string  out_name_;
};

float detection_threshold() {
    const char* t = std::getenv("ECHO_OWW_THRESHOLD");
    if (t && *t) {
        float v = std::strtof(t, nullptr);
        if (v > 0.0f && v <= 1.0f) return v;
    }
    return 0.5f;  // openWakeWord's documented example threshold
}

class OpenWakeWordDetector final : public IWakeWord {
public:
    Status initialize() override {
        try {
            env_ = Ort::Env(ORT_LOGGING_LEVEL_WARNING, "echo-oww");
            const std::string mel = config::openwakeword_melspec();
            const std::string emb = config::openwakeword_embedding();
            const std::string cls = config::openwakeword_model();
            if (!melspec_.load(env_, mel) ||
                !embed_.load(env_, emb) ||
                !classifier_.load(env_, cls)) {
                log_error("perception",
                          "openWakeWord model file(s) missing; wake-word disabled");
                ready_ = false;
                return Status::NotReady;
            }
        } catch (const Ort::Exception& e) {
            char buf[220];
            std::snprintf(buf, sizeof(buf),
                          "openWakeWord init failed (%s); wake-word disabled", e.what());
            log_error("perception", buf);
            ready_ = false;
            return Status::NotReady;
        }
        threshold_ = detection_threshold();
        ready_ = true;
        log_info("perception", "wake-word ready (openWakeWord, ONNX 3-stage pipeline)");
        return Status::Ok;
    }

    int sample_rate()  const noexcept override { return kSampleRate; }
    int frame_length() const noexcept override { return kFrameLength; }

    WakeResult process(const std::int16_t* pcm, std::size_t n) override {
        if (!ready_ || !pcm || n == 0) return {};
        buffer_.insert(buffer_.end(), pcm, pcm + n);
        if (buffer_.size() > kMaxSamples) {
            buffer_.erase(buffer_.begin(),
                          buffer_.begin() +
                              static_cast<std::ptrdiff_t>(buffer_.size() - kMaxSamples));
        }
        float score = score_buffer();
        return WakeResult{ score >= threshold_, score };
    }

    void shutdown() override { buffer_.clear(); ready_ = false; }

private:
    // Run the full pipeline over the current rolling buffer; return the peak
    // classifier score (0 if, even after zero-padding, no window can be formed).
    float score_buffer() {
        if (buffer_.empty()) return 0.0f;

        // Stage 1: melspectrogram over the buffer (int16 values as float), left-
        // padded with zeros to kMinScoreSamples so a short utterance still yields
        // enough mel frames to form classifier windows (see kMinScoreSamples).
        std::vector<float> audio;
        const std::size_t pad =
            buffer_.size() >= kMinScoreSamples ? 0 : kMinScoreSamples - buffer_.size();
        audio.reserve(pad + buffer_.size());
        audio.resize(pad, 0.0f);
        for (std::int16_t s : buffer_) audio.push_back(static_cast<float>(s));
        std::vector<std::int64_t> mel_shape = { 1, static_cast<std::int64_t>(audio.size()) };
        std::vector<float> mel = melspec_.run(audio, mel_shape);
        // Output flattens to (frames, 32) row-major regardless of leading 1-dims.
        const int frames = static_cast<int>(mel.size() / kMelBins);
        if (frames < kEmbWindowFrames) return 0.0f;
        for (float& v : mel) v = v / 10.0f + 2.0f;   // openWakeWord's scale transform

        // Stage 2: sliding 76-frame windows -> one 96-d embedding each.
        std::vector<float> embs;  // flat [n_emb * 96]
        const std::vector<std::int64_t> emb_in_shape = { 1, kEmbWindowFrames, kMelBins, 1 };
        std::vector<float> window(static_cast<std::size_t>(kEmbWindowFrames) * kMelBins);
        for (int f = 0; f + kEmbWindowFrames <= frames; f += kEmbStride) {
            std::copy_n(mel.begin() + static_cast<std::ptrdiff_t>(f) * kMelBins,
                        window.size(), window.begin());
            std::vector<float> e = embed_.run(window, emb_in_shape);
            if (static_cast<int>(e.size()) < kEmbDim) return 0.0f;
            embs.insert(embs.end(), e.begin(), e.begin() + kEmbDim);
        }
        const int n_emb = static_cast<int>(embs.size() / kEmbDim);
        if (n_emb < kClsWindowEmbs) return 0.0f;

        // Stage 3: sliding 16-embedding windows -> peak wake score.
        float peak = 0.0f;
        const std::vector<std::int64_t> cls_shape = { 1, kClsWindowEmbs, kEmbDim };
        std::vector<float> feat(static_cast<std::size_t>(kClsWindowEmbs) * kEmbDim);
        for (int j = 0; j + kClsWindowEmbs <= n_emb; ++j) {
            std::copy_n(embs.begin() + static_cast<std::ptrdiff_t>(j) * kEmbDim,
                        feat.size(), feat.begin());
            std::vector<float> out = classifier_.run(feat, cls_shape);
            if (!out.empty()) peak = std::max(peak, out[0]);
        }
        return peak;
    }

    Ort::Env env_{nullptr};
    OrtModel melspec_;
    OrtModel embed_;
    OrtModel classifier_;
    std::vector<std::int16_t> buffer_;
    float threshold_ = 0.5f;
    bool  ready_ = false;
};

#endif  // ECHO_WITH_OPENWAKEWORD

// Stub: never fires. The demo host provides a keyboard "wake" affordance so the
// pipeline is still exercisable without any wake-word model installed.
class StubWakeWord final : public IWakeWord {
public:
    Status initialize() override {
        log_info("perception", "wake-word initialized (stub: no detections)");
        return Status::Ok;
    }
    int sample_rate()  const noexcept override { return 16000; }
    int frame_length() const noexcept override { return 512; }
    WakeResult process(const std::int16_t*, std::size_t) override { return {}; }
    void shutdown() override {}
};

}  // namespace

// Backend selection. Both real backends can be compiled in at once; which one
// make_wake_word() returns is decided at build time by ECHO_WAKEWORD_BACKEND
// (default openwakeword — the one CI and anyone without a Picovoice account can
// actually run). ECHO_WAKEWORD_PREFER_PORCUPINE is defined by CMake when that
// flag selects porcupine. A runtime $ECHO_WAKEWORD_BACKEND override lets a build
// with both compiled switch without recompiling. With no real backend compiled
// (the default stub build) the stub is returned, unchanged since Phase 3.
std::unique_ptr<IWakeWord> make_wake_word() {
#if defined(ECHO_WITH_OPENWAKEWORD) && defined(ECHO_WITH_PORCUPINE)
    bool prefer_porcupine =
#if defined(ECHO_WAKEWORD_PREFER_PORCUPINE)
        true;
#else
        false;
#endif
    if (const char* b = std::getenv("ECHO_WAKEWORD_BACKEND")) {
        if (std::string(b) == "porcupine")   prefer_porcupine = true;
        if (std::string(b) == "openwakeword") prefer_porcupine = false;
    }
    if (prefer_porcupine) return std::make_unique<PorcupineWakeWord>();
    return std::make_unique<OpenWakeWordDetector>();
#elif defined(ECHO_WITH_OPENWAKEWORD)
    return std::make_unique<OpenWakeWordDetector>();
#elif defined(ECHO_WITH_PORCUPINE)
    return std::make_unique<PorcupineWakeWord>();
#else
    return std::make_unique<StubWakeWord>();
#endif
}

}  // namespace echo::perception
