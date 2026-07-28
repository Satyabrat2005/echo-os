#include "wake_word.hpp"

#include "echo/config.hpp"
#include "echo/log.hpp"

#include <cstdio>
#include <string>

#if defined(ECHO_WITH_PORCUPINE)
#include "pv_porcupine.h"
#include <fstream>
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

// Stub: never fires. The demo host provides a keyboard "wake" affordance so the
// pipeline is still exercisable without the Porcupine model installed.
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

std::unique_ptr<IWakeWord> make_wake_word() {
#if defined(ECHO_WITH_PORCUPINE)
    return std::make_unique<PorcupineWakeWord>();
#else
    return std::make_unique<StubWakeWord>();
#endif
}

}  // namespace echo::perception
