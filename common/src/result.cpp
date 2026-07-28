#include "echo/result.hpp"

namespace echo {

const char* to_string(Status s) noexcept {
    switch (s) {
        case Status::Ok:            return "ok";
        case Status::LowConfidence: return "low-confidence";
        case Status::NotReady:      return "not-ready";
        case Status::Timeout:       return "timeout";
        case Status::HardwareError: return "hardware-error";
        case Status::Unavailable:   return "unavailable";
    }
    return "unknown";
}

}  // namespace echo
