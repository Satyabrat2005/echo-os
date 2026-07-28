#include "echo/apps/framework/permission.hpp"

namespace echo::apps {

const char* to_string(Capability c) noexcept {
    switch (c) {
        case Capability::Camera:     return "camera";
        case Capability::Microphone: return "microphone";
        case Capability::Contacts:   return "contacts";
        case Capability::Network:    return "network";
        case Capability::Location:   return "location";
        case Capability::Storage:    return "storage";
        case Capability::Count:      return "?";
    }
    return "?";
}

}  // namespace echo::apps
