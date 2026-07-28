// ECHO OS apps — HUD compositor: the ONLY visual surface an app may draw to.
//
// The glasses have no touchscreen and no window manager (constraint #1). An app
// never gets a framebuffer, a canvas, or a "window" — it gets this narrow set of
// overlay primitives and nothing else. The whole point of routing every app
// through this header is that the visual language of the device stays uniform
// and calm: subtitle-style text, a single glyph, a status dot. If it can't be
// said with these three primitives, it doesn't belong on the HUD.
//
// This mirrors how voice-ui is the single audio surface for the core: apps
// compose a HudFrame from primitives and hand it over; they cannot reach past
// the compositor to paint anything bespoke.
#pragma once

#include "echo/types.hpp"
#include "echo/result.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace echo::apps::hud {

// The small, fixed set of icons the HUD can show. Deliberately closed: adding a
// glyph is a design decision, not something an app does at runtime.
enum class Glyph : std::uint8_t {
    None = 0,
    Play,
    Pause,
    Stop,
    Phone,
    PhoneEnd,
    Mail,
    Search,
    Globe,      // browser / web page
    Video,
    Camera,
    Photo,
    Check,
    Warning,
};

const char* to_string(Glyph g) noexcept;

// A single status dot. The calm-by-default rule (constraint #5) means most
// frames sit at Idle or Working; Success/Error are brief punctuation.
enum class StatusKind : std::uint8_t {
    Idle = 0,
    Working,
    Success,
    Error,
};

const char* to_string(StatusKind s) noexcept;

// --- The three (and only three) overlay primitives --------------------------

// Subtitle-style caption. Short, readable, auto-expiring so the HUD never
// accumulates clutter.
struct SubtitleText {
    std::string   text;
    std::uint16_t ttl_ms = 4000;  // how long it lingers before fading
};

// A single icon. One glyph, never a row of controls (that would be a GUI).
struct Icon {
    Glyph glyph = Glyph::None;
};

// A status dot conveying app state at a glance.
struct StatusGlyph {
    StatusKind kind = StatusKind::Idle;
};

// A composed overlay: at most one of each primitive. This bound is the design —
// there is no way to express a scrolling list, a grid, or a button here, because
// the device has no way to interact with one. Build one with the with_* helpers.
struct HudFrame {
    SubtitleText subtitle;
    Icon         icon;
    StatusGlyph  status;

    bool has_subtitle = false;
    bool has_icon     = false;
    bool has_status   = false;

    HudFrame& with_subtitle(std::string text, std::uint16_t ttl_ms = 4000) {
        subtitle      = {std::move(text), ttl_ms};
        has_subtitle  = true;
        return *this;
    }
    HudFrame& with_icon(Glyph g) {
        icon     = {g};
        has_icon = true;
        return *this;
    }
    HudFrame& with_status(StatusKind s) {
        status     = {s};
        has_status = true;
        return *this;
    }

    bool empty() const noexcept { return !has_subtitle && !has_icon && !has_status; }
};

// The compositor owns the single overlay surface and draws frames submitted by
// apps. Apps hold no reference to a renderer — they hand a HudFrame to the host,
// which presents it here, tagged by app id so the compositor can arbitrate.
class IHudCompositor {
public:
    virtual ~IHudCompositor() = default;

    virtual Status initialize() = 0;

    // Draw a frame on behalf of an app. The compositor is free to coalesce or
    // drop frames to keep the overlay calm; presentation must never block the
    // caller (it feeds a display buffer, it doesn't do synchronous I/O).
    virtual void present(std::string_view app_id, const HudFrame& frame) = 0;

    // Remove whatever the given app last drew.
    virtual void clear(std::string_view app_id) = 0;

    virtual void shutdown() = 0;
};

// Two renderers, same primitives:
//   simulated_window == false -> headless: logs each frame (used by tests/CI).
//   simulated_window == true  -> draws a subtitle band to the terminal, standing
//                                in for the glasses' overlay on a laptop.
std::unique_ptr<IHudCompositor> make_hud_compositor(bool simulated_window = false);

}  // namespace echo::apps::hud
