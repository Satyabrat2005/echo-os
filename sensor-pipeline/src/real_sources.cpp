// Real laptop sensor frontends for the Phase-3 demo.
//
// The webcam substitutes for the glasses' OV2640 (OpenCV VideoCapture) and the
// system mic for the I2S mic (SDL audio capture at 16 kHz mono int16 — the rate
// the wake-word/ASR stages want). Each source pushes into whatever FrameQueue it
// is started with, and the demo host gives each its OWN queue so exactly one
// producer thread ever writes a given SpscRingBuffer.
//
// When neither dep is compiled in, the factories fall back to the stub sources so
// the demo binary still links and runs (it just won't capture real media).
#include "echo/sensor/sensor_source.hpp"
#include "echo/audio_dsp.hpp"
#include "echo/log.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <thread>
#include <vector>

#if defined(ECHO_WITH_OPENCV)
#include <opencv2/videoio.hpp>
#include <opencv2/core.hpp>
#endif

#if defined(ECHO_WITH_SDL)
#include <SDL.h>
#endif

namespace echo::sensor {
namespace {

#if defined(ECHO_WITH_OPENCV)

// Webcam capture on a dedicated thread. Frames live in a small pool so the view
// pushed into the queue stays valid until the consumer drains it.
class WebcamSource final : public ISensorSource {
public:
    explicit WebcamSource(int device_index) : device_(device_index) {}
    ~WebcamSource() override { stop(); }

    Modality modality() const noexcept override { return Modality::Camera; }

    Status start(FrameQueue& out) override {
        if (!cap_.open(device_)) {
            log_error("sensor", "webcam open failed");
            return Status::HardwareError;
        }
        cap_.set(cv::CAP_PROP_FRAME_WIDTH, 640);
        cap_.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
        running_ = true;
        worker_ = std::thread([this, &out] { loop(out); });
        log_info("sensor", "webcam frontend started (OpenCV)");
        return Status::Ok;
    }

    void stop() override {
        running_ = false;
        if (worker_.joinable()) worker_.join();
        if (cap_.isOpened()) cap_.release();
    }
    bool running() const noexcept override { return running_; }

private:
    void loop(FrameQueue& out) {
        std::uint64_t seq = 0;
        while (running_) {
            cv::Mat& slot = pool_[seq % pool_.size()];
            if (!cap_.read(slot) || slot.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            if (!slot.isContinuous()) slot = slot.clone();
            SensorFrame f;
            f.modality    = Modality::Camera;
            f.sequence    = seq++;
            f.captured_at = now();
            f.data        = slot.data;
            f.size        = slot.total() * slot.elemSize();
            f.width       = static_cast<std::uint32_t>(slot.cols);
            f.height      = static_cast<std::uint32_t>(slot.rows);
            out.push(f);  // drop-on-full is fine: we favor fresh frames
            std::this_thread::sleep_for(std::chrono::milliseconds(33));  // ~30 fps
        }
    }

    int                       device_;
    cv::VideoCapture          cap_;
    std::array<cv::Mat, 4>    pool_;
    std::thread               worker_;
    std::atomic<bool>         running_{false};
};

#endif  // ECHO_WITH_OPENCV

#if defined(ECHO_WITH_SDL)

// System-mic capture via SDL. The SDL audio thread is the single producer for
// this source's queue.
class SdlMicSource final : public ISensorSource {
public:
    ~SdlMicSource() override { stop(); }

    Modality modality() const noexcept override { return Modality::Microphone; }

    Status start(FrameQueue& out) override {
        out_ = &out;
        // Phase 19: a single-mic pre-processing pass (high-pass + gentle gate + AGC)
        // sits right here in the capture path, before wake-word/ASR ever see the
        // audio. Single-mic by design — this is one mono SDL device, so there is no
        // array to beamform (see audio_dsp.hpp). Opt-out via ECHO_MIC_PREPROCESS=0 so
        // its effect can be A/B'd on real hardware; the offline evaluation in
        // tests/audio_robustness_test.cpp measures whether it actually helps.
        if (const char* v = std::getenv("ECHO_MIC_PREPROCESS"))
            preprocess_ = !(v[0] == '0' && v[1] == '\0');
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            log_error("sensor", "SDL audio init failed");
            return Status::HardwareError;
        }
        SDL_AudioSpec want{}, have{};
        want.freq     = 16000;
        want.format   = AUDIO_S16SYS;
        want.channels = 1;
        want.samples  = 512;                 // matches Porcupine's frame length
        want.callback = &SdlMicSource::on_audio;
        want.userdata = this;
        dev_ = SDL_OpenAudioDevice(nullptr, /*iscapture=*/1, &want, &have, 0);
        if (dev_ == 0) {
            log_error("sensor", "SDL mic open failed");
            return Status::HardwareError;
        }
        sample_rate_ = have.freq;
        running_ = true;
        SDL_PauseAudioDevice(dev_, 0);  // start capturing
        log_info("sensor", "microphone frontend started (SDL, 16 kHz mono)");
        return Status::Ok;
    }

    void stop() override {
        running_ = false;
        if (dev_) { SDL_CloseAudioDevice(dev_); dev_ = 0; }
    }
    bool running() const noexcept override { return running_; }

private:
    static void on_audio(void* userdata, Uint8* stream, int len) {
        auto* self = static_cast<SdlMicSource*>(userdata);
        if (!self->running_ || !self->out_) return;
        auto& slot = self->pool_[self->idx_ % self->pool_.size()];
        if (self->preprocess_ && (len % 2) == 0) {
            // Run the single-mic pre-processor over this int16 buffer before it is
            // queued. Note (honest): this allocates on the SDL audio callback — fine
            // for the scaffold's small buffers, a persistent scratch buffer is the
            // production refinement.
            const auto* pcm = reinterpret_cast<const std::int16_t*>(stream);
            const std::size_t nn = static_cast<std::size_t>(len) / sizeof(std::int16_t);
            auto pp = self->preproc_.process(pcm, nn, self->sample_rate_);
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(pp.samples.data());
            slot.assign(bytes, bytes + pp.samples.size() * sizeof(std::int16_t));
        } else {
            slot.assign(stream, stream + len);
        }
        SensorFrame f;
        f.modality    = Modality::Microphone;
        f.sequence    = self->idx_++;
        f.captured_at = now();
        f.data        = slot.data();
        f.size        = slot.size();
        f.sample_rate = static_cast<std::uint32_t>(self->sample_rate_);
        self->out_->push(f);
    }

    FrameQueue*                             out_ = nullptr;
    SDL_AudioDeviceID                       dev_ = 0;
    int                                     sample_rate_ = 16000;
    std::array<std::vector<std::uint8_t>, 8> pool_;
    std::uint64_t                           idx_ = 0;
    std::atomic<bool>                       running_{false};
    bool                                    preprocess_ = true;
    echo::audio::AudioPreprocessor          preproc_{};
};

#endif  // ECHO_WITH_SDL

}  // namespace

std::unique_ptr<ISensorSource> make_real_camera_source(int device_index) {
#if defined(ECHO_WITH_OPENCV)
    return std::make_unique<WebcamSource>(device_index);
#else
    (void)device_index;
    log_warn("sensor", "OpenCV not built in; using stub camera");
    return make_camera_source();
#endif
}

std::unique_ptr<ISensorSource> make_real_microphone_source() {
#if defined(ECHO_WITH_SDL)
    return std::make_unique<SdlMicSource>();
#else
    log_warn("sensor", "SDL not built in; using stub microphone");
    return make_microphone_source();
#endif
}

}  // namespace echo::sensor
