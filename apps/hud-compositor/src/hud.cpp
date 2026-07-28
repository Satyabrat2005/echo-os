#include "echo/apps/hud/hud.hpp"
#include "echo/log.hpp"

#include <cstdio>
#include <string>

namespace echo::apps::hud {

const char* to_string(Glyph g) noexcept {
    switch (g) {
        case Glyph::None:     return "-";
        case Glyph::Play:     return "play";
        case Glyph::Pause:    return "pause";
        case Glyph::Stop:     return "stop";
        case Glyph::Phone:    return "phone";
        case Glyph::PhoneEnd: return "phone-end";
        case Glyph::Mail:     return "mail";
        case Glyph::Search:   return "search";
        case Glyph::Globe:    return "globe";
        case Glyph::Video:    return "video";
        case Glyph::Camera:   return "camera";
        case Glyph::Photo:    return "photo";
        case Glyph::Check:    return "check";
        case Glyph::Warning:  return "warning";
    }
    return "-";
}

const char* to_string(StatusKind s) noexcept {
    switch (s) {
        case StatusKind::Idle:    return "idle";
        case StatusKind::Working: return "working";
        case StatusKind::Success: return "success";
        case StatusKind::Error:   return "error";
    }
    return "idle";
}

namespace {

// Render a HudFrame to a compact one-line description. Shared by both renderers
// so their content is identical — only the presentation differs.
std::string describe(std::string_view app_id, const HudFrame& f) {
    std::string line = "[";
    line += std::string(app_id);
    line += "]";
    if (f.has_status) { line += " ("; line += to_string(f.status.kind); line += ")"; }
    if (f.has_icon)   { line += " <"; line += to_string(f.icon.glyph);  line += ">"; }
    if (f.has_subtitle) { line += " \""; line += f.subtitle.text; line += "\""; }
    if (f.empty())    { line += " (blank)"; }
    return line;
}

// Headless renderer: writes each presented frame to the log. This is what CI and
// the smoke tests observe — no terminal drawing, no escape codes.
class HeadlessCompositor final : public IHudCompositor {
public:
    Status initialize() override {
        log_info("hud", "compositor initialized (headless)");
        return Status::Ok;
    }
    void present(std::string_view app_id, const HudFrame& frame) override {
        log_info("hud", describe(app_id, frame).c_str());
    }
    void clear(std::string_view app_id) override {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "clear [%.*s]",
                      static_cast<int>(app_id.size()), app_id.data());
        log_debug("hud", buf);
    }
    void shutdown() override { log_info("hud", "compositor shut down"); }
};

// Simulated-window renderer: draws a subtitle band to the terminal to stand in
// for the glasses overlay while developing on a laptop. Still only the three
// primitives — it just paints them in a fixed band instead of logging them.
class SimulatedWindowCompositor final : public IHudCompositor {
public:
    Status initialize() override {
        log_info("hud", "compositor initialized (simulated HUD window)");
        return Status::Ok;
    }
    void present(std::string_view app_id, const HudFrame& frame) override {
        // A minimal "overlay band" — one boxed line, never a scrolling UI.
        const char* status = frame.has_status ? to_string(frame.status.kind) : "idle";
        const char* icon   = frame.has_icon   ? to_string(frame.icon.glyph)  : "-";
        std::string text   = frame.has_subtitle ? frame.subtitle.text : std::string{};
        std::printf("\n  \xE2\x94\x8C\xE2\x94\x80 HUD \xE2\x94\x80 %-9.*s \xE2\x94\x80 %-8s \xE2\x94\x80 %-7s\n",
                    static_cast<int>(app_id.size()), app_id.data(), icon, status);
        std::printf("  \xE2\x94\x82 %s\n", text.c_str());
        std::printf("  \xE2\x94\x94\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\n");
        std::fflush(stdout);
    }
    void clear(std::string_view /*app_id*/) override {}
    void shutdown() override { log_info("hud", "compositor shut down"); }
};

}  // namespace

std::unique_ptr<IHudCompositor> make_hud_compositor(bool simulated_window) {
    if (simulated_window) return std::make_unique<SimulatedWindowCompositor>();
    return std::make_unique<HeadlessCompositor>();
}

}  // namespace echo::apps::hud
