// ECHO OS apps — gallery (browse captured photos/videos by voice).
//
// "show me yesterday's photos." With no touchscreen there is no grid to swipe —
// the gallery answers by voice, summarizing what it found and putting a small set
// of HUD thumbnails (as status/glyph + a count subtitle) on the overlay
// (constraint #1). Navigation is "next" / "previous", spoken.
//
// Mock mode returns a fake captured set; real mode reads the on-device media
// store (Storage capability) behind this identical interface.
#pragma once

#include "echo/apps/framework/app.hpp"
#include <memory>

namespace echo::apps::gallery {
std::unique_ptr<IApp> make_gallery_app(bool mock = true);
}  // namespace echo::apps::gallery
