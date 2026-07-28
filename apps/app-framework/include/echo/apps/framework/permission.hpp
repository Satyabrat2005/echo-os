// ECHO OS apps — capability-based permission model.
//
// Privacy by default (constraint #5): an app reaches a sensor or the network
// only if it has been granted the matching capability. The grants live here, in
// the host, NOT in the app — an app cannot widen its own permissions. The host
// consults this model before dispatching a command, so a mail app can never open
// the camera and the gallery can never touch the network.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace echo::apps {

// The sensitive resources an app might need. Kept coarse on purpose — fewer,
// clearer prompts for the wearer (constraint #1: calm, legible interaction).
enum class Capability : std::uint8_t {
    Camera = 0,   // user-initiated capture (NOT perception's real-time feed)
    Microphone,   // raw mic beyond the shared wake-word/ASR path
    Contacts,     // names/numbers on the paired phone
    Network,      // any outbound request (search, mail, streaming)
    Location,     // coarse/fine position
    Storage,      // captured media on device
    Count,
};

const char* to_string(Capability c) noexcept;

// A compact set of capabilities (bitset over the small enum).
class PermissionSet {
public:
    PermissionSet() = default;

    PermissionSet& add(Capability c) {
        bits_ |= mask(c);
        return *this;
    }
    bool has(Capability c) const noexcept { return (bits_ & mask(c)) != 0; }
    bool empty() const noexcept { return bits_ == 0; }

    // Every capability in `needed` is present here.
    bool covers(const PermissionSet& needed) const noexcept {
        return (bits_ & needed.bits_) == needed.bits_;
    }

    static PermissionSet of(std::initializer_list<Capability> caps) {
        PermissionSet s;
        for (auto c : caps) s.add(c);
        return s;
    }

private:
    static std::uint32_t mask(Capability c) noexcept {
        return 1u << static_cast<std::uint8_t>(c);
    }
    std::uint32_t bits_ = 0;
};

// Maps app id -> the capabilities that app has been granted. The host builds
// this at startup from a policy (later: a wearer/caregiver-approved manifest).
class PermissionModel {
public:
    void grant(std::string_view app_id, PermissionSet caps) {
        grants_[std::string(app_id)] = caps;
    }

    PermissionSet granted(std::string_view app_id) const {
        auto it = grants_.find(std::string(app_id));
        return it == grants_.end() ? PermissionSet{} : it->second;
    }

    bool is_granted(std::string_view app_id, Capability c) const {
        return granted(app_id).has(c);
    }

    // True iff `app_id` holds every capability the app declared it requires.
    bool satisfies(std::string_view app_id, const PermissionSet& required) const {
        return granted(app_id).covers(required);
    }

private:
    std::unordered_map<std::string, PermissionSet> grants_;
};

}  // namespace echo::apps
