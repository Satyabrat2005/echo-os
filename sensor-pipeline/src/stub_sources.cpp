// Stub sensor frontends for the scaffold.
//
// On real hardware each of these owns a DMA channel and a pre-allocated frame
// pool, and pushes zero-copy views into the capture queue from an ISR. Here they
// simply report "ready" without producing frames, so the rest of the runtime can
// be exercised end to end.
#include "echo/sensor/sensor_source.hpp"
#include "echo/log.hpp"

namespace echo::sensor {
namespace {

class StubSource final : public ISensorSource {
public:
    explicit StubSource(Modality m, const char* name) : modality_(m), name_(name) {}

    Modality modality() const noexcept override { return modality_; }

    Status start(FrameQueue& /*out*/) override {
        // TODO(sensor): configure DMA, allocate frame pool, arm the capture ISR.
        running_ = true;
        log_info("sensor", name_);
        return Status::Ok;
    }

    void stop() override { running_ = false; }
    bool running() const noexcept override { return running_; }

private:
    Modality    modality_;
    const char* name_;
    bool        running_ = false;
};

}  // namespace

std::unique_ptr<ISensorSource> make_camera_source() {
    return std::make_unique<StubSource>(Modality::Camera, "camera frontend ready (stub)");
}

std::unique_ptr<ISensorSource> make_microphone_source() {
    return std::make_unique<StubSource>(Modality::Microphone, "microphone frontend ready (stub)");
}

std::unique_ptr<ISensorSource> make_eeg_source() {
    return std::make_unique<StubSource>(Modality::Eeg, "eeg frontend ready (stub)");
}

}  // namespace echo::sensor
