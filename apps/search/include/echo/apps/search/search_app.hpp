// ECHO OS apps — search (Google Search wrapper, voice-first).
//
// "search for the nearest pharmacy." Same voice-command pattern as browser: a
// query in, a spoken top result out, with the HUD showing only the result title
// and a search glyph. No results page to scroll (constraint #1).
//
// Mock mode returns canned results; real mode wraps the Google Custom Search /
// Programmable Search API behind this identical interface (constraint #4).
#pragma once

#include "echo/apps/framework/app.hpp"
#include <memory>

namespace echo::apps::search {
std::unique_ptr<IApp> make_search_app(bool mock = true);
}  // namespace echo::apps::search
