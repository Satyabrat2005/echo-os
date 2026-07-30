// ECHO OS — sensor-pipeline fixture tests.
//
// Phase 10, deliverable #2. Feeds real-shaped fixture audio/image BYTES through
// the real capture/queue path — the actual SpscRingBuffer and SensorPipeline —
// rather than the no-op stub factories (make_microphone_source et al. never
// push). A small in-test ISensorSource stands in for a real frontend: it latches
// fixture-derived SensorFrames into the pipeline's shared queue on start(), so
// SensorPipeline::start_all()/next_frame()/stop_all() run for real. Before this
// phase sensor-pipeline sat at 0 % coverage.
#include "echo/sensor/sensor_source.hpp"
#include "echo/sensor/ring_buffer.hpp"
#include "echo/types.hpp"

#include "check.hpp"
#include "fixture_io.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

using namespace echo;

namespace {

// A real ISensorSource: unlike the scaffold stubs, it actually produces frames.
// It slices a pre-loaded fixture buffer into fixed-size SensorFrames and pushes
// them into the pipeline's shared queue when start() is called — exactly the
// producer role the ISR/DMA frontend plays on real hardware. The backing bytes
// are owned by the caller (frames are non-owning views), matching the zero-copy
// contract in types.hpp.
class FixtureSource final : public sensor::ISensorSource {
public:
    FixtureSource(Modality m, std::vector<SensorFrame> frames)
        : modality_(m), frames_(std::move(frames)) {}

    Modality modality() const noexcept override { return modality_; }

    Status start(sensor::FrameQueue& out) override {
        running_ = true;
        for (const auto& f : frames_)
            if (!out.push(f)) ++dropped_;  // queue full: real frontends drop, never block
        return Status::Ok;
    }
    void stop() override { running_ = false; }
    bool running() const noexcept override { return running_; }

    int dropped() const noexcept { return dropped_; }

private:
    Modality                 modality_;
    std::vector<SensorFrame> frames_;
    bool                     running_ = false;
    int                      dropped_ = 0;
};

// Slice a contiguous int16 PCM buffer into `count` audio SensorFrames of
// `samples_per_frame` each, all viewing into the caller-owned `pcm`.
std::vector<SensorFrame> audio_frames(const std::vector<std::int16_t>& pcm,
                                      int sample_rate, std::size_t samples_per_frame,
                                      std::size_t count) {
    std::vector<SensorFrame> out;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t off = i * samples_per_frame;
        if (off >= pcm.size()) break;
        const std::size_t n = std::min(samples_per_frame, pcm.size() - off);
        SensorFrame f;
        f.modality    = Modality::Microphone;
        f.sequence    = i;
        f.data        = reinterpret_cast<const std::uint8_t*>(pcm.data() + off);
        f.size        = n * sizeof(std::int16_t);
        f.sample_rate = static_cast<std::uint32_t>(sample_rate);
        out.push_back(f);
    }
    return out;
}

// Deliverable #2: real fixture audio bytes traverse a real ISensorSource, the
// real SensorPipeline, and the real SPSC queue, and come out intact and in order.
void test_audio_fixture_through_pipeline() {
    auto clip = echo::test::load_wav("speech_hello.wav");
    CHECK(clip.ok);
    CHECK(clip.sample_rate == 16000);

    constexpr std::size_t kFrame = 512;   // wake-word frame length
    auto frames = audio_frames(clip.samples, clip.sample_rate, kFrame, 5);
    CHECK(frames.size() == 5);

    sensor::SensorPipeline pipe;
    auto src = std::make_unique<FixtureSource>(Modality::Microphone, frames);
    pipe.add_source(std::move(src));
    CHECK(pipe.start_all() == Status::Ok);

    // Drain the queue and verify FIFO order, modality, rate, and byte integrity.
    std::size_t got = 0;
    while (auto f = pipe.next_frame()) {
        CHECK(f->modality == Modality::Microphone);
        CHECK(f->sequence == got);                       // FIFO: order preserved
        CHECK(f->sample_rate == 16000);
        CHECK(f->size == kFrame * sizeof(std::int16_t)); // real-shaped, not synthetic
        // The frame is a VIEW: its bytes are the exact fixture bytes at that slice.
        const auto* p = reinterpret_cast<const std::int16_t*>(f->data);
        CHECK(p[0] == clip.samples[got * kFrame]);
        CHECK(p[1] == clip.samples[got * kFrame + 1]);
        ++got;
    }
    CHECK(got == 5);
    CHECK(!pipe.next_frame().has_value());  // drained -> empty, no block
}

// The capture queue is bounded (kCaptureQueueDepth) and never blocks the
// producer: once full, further pushes are refused so the real-time path drops
// rather than stalls. Verify with real frame-shaped data, not ints.
void test_capture_queue_is_bounded_and_nonblocking() {
    auto clip = echo::test::load_wav("noisy_garble.wav");
    CHECK(clip.ok);

    // Ask the source to push more frames than the queue can hold.
    const std::size_t over = sensor::kCaptureQueueDepth + 4;
    auto frames = audio_frames(clip.samples, clip.sample_rate, 256, over);
    CHECK(frames.size() == over);

    auto src = std::make_unique<FixtureSource>(Modality::Microphone, frames);
    FixtureSource* raw = src.get();
    sensor::SensorPipeline pipe;
    pipe.add_source(std::move(src));
    CHECK(pipe.start_all() == Status::Ok);

    // Exactly capacity frames are retrievable; the surplus was dropped, not queued.
    std::size_t drained = 0;
    while (pipe.next_frame()) ++drained;
    CHECK(drained == sensor::kCaptureQueueDepth);
    CHECK(raw->dropped() == static_cast<int>(over - sensor::kCaptureQueueDepth));
}

// Image bytes survive the queue with dimensions intact (camera lane).
void test_image_fixture_through_pipeline() {
    auto img = echo::test::load_ppm("face_enrolled.ppm");
    CHECK(img.ok);
    CHECK(img.width == 32 && img.height == 32);

    SensorFrame frame;
    frame.modality = Modality::Camera;
    frame.sequence = 0;
    frame.data     = img.bgr.data();
    frame.size     = img.bgr.size();
    frame.width    = static_cast<std::uint32_t>(img.width);
    frame.height   = static_cast<std::uint32_t>(img.height);

    sensor::SensorPipeline pipe;
    pipe.add_source(std::make_unique<FixtureSource>(
        Modality::Camera, std::vector<SensorFrame>{frame}));
    CHECK(pipe.start_all() == Status::Ok);

    auto out = pipe.next_frame();
    CHECK(out.has_value());
    CHECK(out->modality == Modality::Camera);
    CHECK(out->width == 32 && out->height == 32);
    CHECK(out->size == static_cast<std::size_t>(32 * 32 * 3));
    CHECK(out->valid());
    CHECK(out->data == img.bgr.data());              // still a view, no copy
    CHECK(out->data[0] == img.bgr[0]);
}

// Lifecycle: start_all() actually starts each registered source; stop_all()
// stops them. (The stub factories never let this run — they push nothing.)
void test_start_stop_lifecycle() {
    auto frames = std::vector<SensorFrame>{};  // a source that starts cleanly
    auto src = std::make_unique<FixtureSource>(Modality::Eeg, frames);
    FixtureSource* raw = src.get();

    sensor::SensorPipeline pipe;
    pipe.add_source(std::move(src));
    CHECK(!raw->running());
    CHECK(pipe.start_all() == Status::Ok);
    CHECK(raw->running());
    pipe.stop_all();
    CHECK(!raw->running());
}

}  // namespace

int main() {
    test_audio_fixture_through_pipeline();
    test_capture_queue_is_bounded_and_nonblocking();
    test_image_fixture_through_pipeline();
    test_start_stop_lifecycle();
    return echo::test::report("sensor-pipeline");
}
