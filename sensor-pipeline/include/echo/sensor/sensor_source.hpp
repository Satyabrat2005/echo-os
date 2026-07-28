// ECHO OS — sensor frontend interfaces.
//
// Each physical sensor (OV2640 camera, mic, EEG frontend) is wrapped in an
// ISensorSource. Sources push SensorFrames into a shared lock-free buffer; the
// perception stage drains it. Sources never allocate on the hot path — they hand
// out views into a pre-allocated frame pool.
#pragma once

#include "echo/types.hpp"
#include "echo/result.hpp"
#include "echo/sensor/ring_buffer.hpp"

#include <memory>
#include <vector>

namespace echo::sensor {

// Depth of the capture queue. Sized so a momentary perception hiccup doesn't
// drop frames, but small enough to keep end-to-end latency bounded.
inline constexpr std::size_t kCaptureQueueDepth = 8;
using FrameQueue = SpscRingBuffer<SensorFrame, kCaptureQueueDepth + 1>;

// A single sensor frontend.
class ISensorSource {
public:
    virtual ~ISensorSource() = default;

    virtual Modality modality() const noexcept = 0;

    // Power on the frontend and begin latching frames into `out`. Non-blocking:
    // capture runs on its own real-time context (ISR / dedicated thread on device).
    virtual Status start(FrameQueue& out) = 0;

    // Stop capture and power down the frontend.
    virtual void stop() = 0;

    virtual bool running() const noexcept = 0;
};

// Owns the set of active sources and the shared capture queue. The runtime holds
// one of these; perception drains it each loop iteration.
class SensorPipeline {
public:
    SensorPipeline();
    ~SensorPipeline();

    // Register a frontend. Must be called before start().
    void add_source(std::unique_ptr<ISensorSource> source);

    Status start_all();
    void   stop_all();

    // Consumer entry point: pull the next available frame across all sources,
    // or nullopt if none are queued.
    std::optional<SensorFrame> next_frame() noexcept;

    FrameQueue& queue() noexcept { return queue_; }

private:
    std::vector<std::unique_ptr<ISensorSource>> sources_;
    FrameQueue                                  queue_;
};

// Factories for the concrete frontends (stubbed for the scaffold).
std::unique_ptr<ISensorSource> make_camera_source();      // OV2640 DVP
std::unique_ptr<ISensorSource> make_microphone_source();  // I2S/PDM mic
std::unique_ptr<ISensorSource> make_eeg_source();         // frontal EEG

}  // namespace echo::sensor
