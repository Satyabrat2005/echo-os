#include "echo/consent.hpp"

namespace echo {

const char* to_string(ConsentScope scope) noexcept {
    switch (scope) {
        case ConsentScope::None:              return "none";
        case ConsentScope::Digest:            return "digest";
        case ConsentScope::DigestAndCommands: return "digest+commands";
    }
    return "unknown";
}

const char* to_string(ConsentGrantor grantor) noexcept {
    switch (grantor) {
        case ConsentGrantor::Wearer:              return "wearer";
        case ConsentGrantor::CaregiverSupervised: return "caregiver-supervised";
    }
    return "unknown";
}

}  // namespace echo
