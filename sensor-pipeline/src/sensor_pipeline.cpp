#include "echo/sensor/sensor_source.hpp"
#include "echo/log.hpp"

namespace echo::sensor {

SensorPipeline::SensorPipeline()  = default;
SensorPipeline::~SensorPipeline() { stop_all(); }

void SensorPipeline::add_source(std::unique_ptr<ISensorSource> source) {
    if (source) sources_.push_back(std::move(source));
}

Status SensorPipeline::start_all() {
    for (auto& src : sources_) {
        const Status s = src->start(queue_);
        if (s != Status::Ok) {
            log_error("sensor", "failed to start a source; aborting pipeline start");
            stop_all();
            return s;
        }
    }
    log_info("sensor", "all sensor frontends started");
    return Status::Ok;
}

void SensorPipeline::stop_all() {
    for (auto& src : sources_) {
        if (src && src->running()) src->stop();
    }
}

std::optional<SensorFrame> SensorPipeline::next_frame() noexcept {
    return queue_.pop();
}

}  // namespace echo::sensor
