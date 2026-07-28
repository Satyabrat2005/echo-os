// ECHO OS apps — camera (user-initiated photo/video capture).
//
// This owns "take a photo" / "record a video" — deliberate, user-initiated
// capture. It is SEPARATE from the perception module's camera feed: perception
// owns the real-time recognition stream on the safety path; this app owns
// discrete captures the wearer asks for, and must never contend with or disturb
// that feed (constraint #2).
//
// Mock mode simulates a capture and returns a fake asset id; real mode grabs a
// frame/clip from the laptop webcam (dev) or the glasses camera (device) behind
// this identical interface. The HUD shows only a camera glyph and a confirmation.
#pragma once

#include "echo/apps/framework/app.hpp"
#include <memory>

namespace echo::apps::camera {
std::unique_ptr<IApp> make_camera_app(bool mock = true);
}  // namespace echo::apps::camera
