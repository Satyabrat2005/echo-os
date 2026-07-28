// ECHO OS apps — media (audio playback + Spotify).
//
// Voice-first music. "play some jazz", "pause", "skip". There is no player UI to
// scrub or a library to browse by hand — the HUD shows only now-playing subtitle
// text and a play/pause glyph (constraint #1).
//
// Mock mode plays from a small built-in track list and returns fake now-playing
// metadata, so the whole voice flow works today with no Spotify credentials.
// Real mode will wrap the Spotify Web API behind this exact interface — swapping
// the backend must not change a single caller (constraint #4).
#pragma once

#include "echo/apps/framework/app.hpp"

#include <memory>

namespace echo::apps::media {

// mock == true  -> built-in test tracks + fake metadata (default).
// mock == false -> real Spotify backend (not wired yet; see note in the .cpp).
std::unique_ptr<IApp> make_media_app(bool mock = true);

}  // namespace echo::apps::media
