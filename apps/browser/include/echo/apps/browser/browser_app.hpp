// ECHO OS apps — browser (voice-navigable web, no rendered window).
//
// Not a browser window with a viewport to pan — there is no screen for that
// (constraint #1). "open the top result", "read this page": the app fetches a
// page headlessly, extracts its text, and speaks it, showing only a title
// subtitle and a globe glyph on the HUD.
//
// Mock mode returns canned pages so the read-aloud flow works offline. Real mode
// wraps a headless fetch + text-extraction pipeline behind this same interface —
// never a full browser engine.
#pragma once

#include "echo/apps/framework/app.hpp"
#include <memory>

namespace echo::apps::browser {
std::unique_ptr<IApp> make_browser_app(bool mock = true);
}  // namespace echo::apps::browser
