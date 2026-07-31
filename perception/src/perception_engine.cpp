#include "echo/perception/perception_engine.hpp"
#include "echo/audio_dsp.hpp"
#include "echo/latency.hpp"
#include "echo/log.hpp"

#include "wake_word.hpp"
#include "asr.hpp"
#include "vision.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace echo::perception {

Confidence Perception::aggregate_confidence() const noexcept {
    // Gate on the weakest contributing modality: a confident face with a shaky
    // transcript should still defer to safe mode.
    float weakest = 1.0f;
    bool  any = false;
    auto consider = [&](Confidence c) { weakest = std::min(weakest, c.value); any = true; };

    if (wake)   consider(wake->confidence);
    if (speech) consider(speech->confidence);
    for (const auto& f : faces)   consider(f.confidence);
    for (const auto& o : objects) consider(o.confidence);

    return any ? Confidence{weakest} : Confidence{0.0f};
}

namespace {

// Endpointer tuning for batch-on-silence capture (principle: keep it simple this
// phase — stream partials later if the budget allows).
constexpr float  kSilenceRms      = 550.0f;   // int16 RMS below this = "silence"
constexpr int    kEndpointSilenceMs = 700;    // trailing silence that ends a turn
constexpr int    kMinUtteranceMs  = 300;      // ignore sub-300ms blips
constexpr int    kMaxUtteranceMs  = 12000;    // hard cap so we never hang

// The real perception engine: continuous wake-word spotting on the mic, then
// batch ASR on the captured utterance, plus OpenCV face/object recognition on
// camera frames. Each sub-model is real when its ECHO_WITH_* flag is set and a
// stub otherwise; this class is identical either way.
class PerceptionEngine final : public IPerceptionEngine {
public:
    PerceptionEngine()
        : wake_(make_wake_word()), asr_(make_asr()), vision_(make_vision()) {}

    Status initialize() override {
        wake_->initialize();
        asr_->initialize();
        vision_->initialize();
        frame_len_ = wake_->frame_length();
        sample_rate_ = wake_->sample_rate();
        log_info("perception", "engine initialized (wake + ASR + vision)");
        return Status::Ok;
    }

    Result<Perception> process(const SensorFrame& frame) override {
        StageTimer timer(Stage::Perception);
        (void)timer;

        switch (frame.modality) {
            case Modality::Microphone: return process_audio(frame);
            case Modality::Camera:     return process_camera(frame);
            case Modality::Eeg:        return Result<Perception>::ok(Perception{});
        }
        return Result<Perception>::ok(Perception{});
    }

    void shutdown() override {
        wake_->shutdown();
        asr_->shutdown();
        vision_->shutdown();
        log_info("perception", "engine shut down");
    }

private:
    Result<Perception> process_audio(const SensorFrame& frame) {
        Perception p;
        const auto* pcm = reinterpret_cast<const std::int16_t*>(frame.data);
        const std::size_t n = frame.size / sizeof(std::int16_t);
        const int sr = frame.sample_rate > 0 ? static_cast<int>(frame.sample_rate) : sample_rate_;
        if (!pcm || n == 0) return Result<Perception>::ok(std::move(p));

        // Append to the wake-framing buffer and consume in fixed-size frames.
        wake_buf_.insert(wake_buf_.end(), pcm, pcm + n);
        while (static_cast<int>(wake_buf_.size()) >= frame_len_) {
            if (!listening_) {
                WakeResult wr = wake_->process(wake_buf_.data(), frame_len_);
                if (wr.detected) {
                    p.wake = WakeWord{true, Confidence{wr.confidence}};
                    start_listening();
                }
            } else {
                feed_utterance(wake_buf_.data(), frame_len_, sr);
            }
            wake_buf_.erase(wake_buf_.begin(), wake_buf_.begin() + frame_len_);
        }

        // If capture just endpointed, transcribe the collected utterance.
        if (listening_ && endpointed_) {
            AsrResult ar = asr_->transcribe(utterance_.data(), utterance_.size(), sr);
            if (!ar.text.empty()) {
                // Phase 19: fold a real SNR estimate of the captured utterance into the
                // transcript confidence (weakest-link). Heavily-degraded audio drops the
                // confidence below the cognitive core's safe-mode threshold, so noisy
                // speech routes through the SAME "ask again calmly" fallback the Phase-17
                // ASR-degraded path already speaks — no new notification mechanism
                // (Phase 19 constraint #3). Clean audio yields a high SNR and no penalty.
                const float snr   = audio::estimate_snr_db(utterance_.data(), utterance_.size(), sr);
                const float nconf = audio::noise_confidence(snr);
                const float conf  = std::min(ar.confidence, nconf);
                p.speech = Transcript{ar.text, Confidence{conf}, /*endpointed=*/true, snr};
            }
            stop_listening();
        }
        return Result<Perception>::ok(std::move(p));
    }

    Result<Perception> process_camera(const SensorFrame& frame) {
        Perception p;
        if (!frame.valid() || frame.width == 0 || frame.height == 0)
            return Result<Perception>::ok(std::move(p));

        auto faces = vision_->detect_faces(frame.data,
                                           static_cast<int>(frame.width),
                                           static_cast<int>(frame.height));
        for (auto& f : faces) {
            FaceObservation fo{f.identity, Confidence{f.confidence}, f.x, f.y, f.w, f.h, {}};
            fo.embedding = std::move(f.embedding);
            p.faces.push_back(std::move(fo));
        }

        auto objects = vision_->classify_objects(frame.data,
                                                 static_cast<int>(frame.width),
                                                 static_cast<int>(frame.height));
        for (auto& o : objects)
            p.objects.push_back(ObjectObservation{o.label, Confidence{o.confidence}});

        return Result<Perception>::ok(std::move(p));
    }

    void start_listening() {
        listening_ = true;
        endpointed_ = false;
        utterance_.clear();
        silence_ms_ = 0;
        log_debug("perception", "wake-word: listening for command");
    }
    void stop_listening() {
        listening_ = false;
        endpointed_ = false;
        utterance_.clear();
    }

    void feed_utterance(const std::int16_t* pcm, int n, int sr) {
        utterance_.insert(utterance_.end(), pcm, pcm + n);
        const int chunk_ms = (n * 1000) / std::max(1, sr);
        if (rms(pcm, n) < kSilenceRms) silence_ms_ += chunk_ms;
        else                           silence_ms_ = 0;

        const int have_ms = static_cast<int>(utterance_.size()) * 1000 / std::max(1, sr);
        if ((silence_ms_ >= kEndpointSilenceMs && have_ms >= kMinUtteranceMs) ||
            have_ms >= kMaxUtteranceMs) {
            endpointed_ = true;
        }
    }

    static float rms(const std::int16_t* pcm, int n) {
        if (n <= 0) return 0.f;
        double acc = 0;
        for (int i = 0; i < n; ++i) acc += static_cast<double>(pcm[i]) * pcm[i];
        return static_cast<float>(std::sqrt(acc / n));
    }

    std::unique_ptr<IWakeWord> wake_;
    std::unique_ptr<IAsr>      asr_;
    std::unique_ptr<IVision>   vision_;

    int frame_len_   = 512;
    int sample_rate_ = 16000;

    std::vector<std::int16_t> wake_buf_;    // pending samples for wake framing
    std::vector<std::int16_t> utterance_;   // captured command audio
    bool  listening_  = false;
    bool  endpointed_ = false;
    int   silence_ms_ = 0;
};

}  // namespace

std::unique_ptr<IPerceptionEngine> make_perception_engine() {
    return std::make_unique<PerceptionEngine>();
}

}  // namespace echo::perception
