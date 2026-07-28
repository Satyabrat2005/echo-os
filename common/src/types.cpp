#include "echo/types.hpp"

namespace echo {

const char* to_string(Modality m) noexcept {
    switch (m) {
        case Modality::Camera:     return "camera";
        case Modality::Microphone: return "microphone";
        case Modality::Eeg:        return "eeg";
    }
    return "unknown";
}

const char* to_string(RuntimeState s) noexcept {
    switch (s) {
        case RuntimeState::Booting:  return "booting";
        case RuntimeState::Ready:    return "ready";
        case RuntimeState::Active:   return "active";
        case RuntimeState::SafeMode: return "safe-mode";
        case RuntimeState::LowPower: return "low-power";
        case RuntimeState::Shutdown: return "shutdown";
    }
    return "unknown";
}

}  // namespace echo
