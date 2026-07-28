// ECHO OS apps — video (YouTube, audio-first).
//
// At this stage there is no rendered video surface: playback is audio-first and
// the HUD shows only the title and a progress line (constraint #1). "watch the
// news", "play that lecture" — the app resolves a video and streams its audio.
//
// Mock mode returns a fake video with title/duration; real mode wraps the YouTube
// Data + playback APIs behind this identical interface (constraint #4).
#pragma once

#include "echo/apps/framework/app.hpp"
#include <memory>

namespace echo::apps::video {
std::unique_ptr<IApp> make_video_app(bool mock = true);
}  // namespace echo::apps::video
