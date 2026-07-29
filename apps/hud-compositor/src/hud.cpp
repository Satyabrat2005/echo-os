#include "echo/apps/hud/hud.hpp"
#include "echo/log.hpp"

#include <cstdio>
#include <string>

#if defined(ECHO_WITH_SDL)
#include <SDL.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#if defined(ECHO_WITH_SDL_TTF)
#include <SDL_ttf.h>
#include <cstdlib>
#endif
#endif

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
        log_info("hud", describe(app_id, frame));
    }
    void clear(std::string_view app_id) override {
        char buf[96];
        (void)std::snprintf(buf, sizeof(buf), "clear [%.*s]",
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
        (void)std::fflush(stdout);
    }
    void clear(std::string_view /*app_id*/) override {}
    void shutdown() override { log_info("hud", "compositor shut down"); }
};

#if defined(ECHO_WITH_SDL)

// Real laptop HUD: a borderless, always-on-top overlay band near the bottom of
// the screen — the visual stand-in for the glasses display. It renders the SAME
// three primitives (subtitle text, one icon, a status dot) and nothing else, so
// the visual language stays identical to the on-device compositor.
//
// It owns its own render thread (SDL windows must be serviced from one thread and
// the event queue pumped). present() only copies the latest frame under a mutex,
// so it never blocks the caller and an app can never stall the core loop
// (constraint #4). Subtitles auto-expire after their TTL, keeping the HUD calm.
class SdlHudCompositor final : public IHudCompositor {
public:
    Status initialize() override {
        running_ = true;
        thread_  = std::thread([this] { render_loop(); });
        log_info("hud", "compositor initialized (SDL overlay window)");
        return Status::Ok;
    }

    void present(std::string_view app_id, const HudFrame& frame) override {
        std::lock_guard<std::mutex> lk(mu_);
        app_id_    = std::string(app_id);
        frame_     = frame;
        shown_at_  = std::chrono::steady_clock::now();
        have_frame_ = true;
    }

    void clear(std::string_view /*app_id*/) override {
        std::lock_guard<std::mutex> lk(mu_);
        have_frame_ = false;
        frame_ = HudFrame{};
    }

    void shutdown() override {
        running_ = false;
        if (thread_.joinable()) thread_.join();
        log_info("hud", "compositor shut down");
    }

private:
    void render_loop() {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
            log_error("hud", "SDL video init failed; HUD window unavailable");
            return;
        }
        SDL_Rect bounds{0, 0, 1280, 720};
        SDL_GetDisplayUsableBounds(0, &bounds);
        const int w = 720, h = 120;
        const int x = bounds.x + (bounds.w - w) / 2;
        const int y = bounds.y + bounds.h - h - 60;  // hover near the bottom
        Uint32 flags = SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP |
                       SDL_WINDOW_SKIP_TASKBAR;
        window_ = SDL_CreateWindow("ECHO HUD", x, y, w, h, flags);
        if (!window_) { log_error("hud", "HUD window create failed"); return; }
        SDL_SetWindowOpacity(window_, 0.92f);
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
        if (!renderer_) { SDL_DestroyWindow(window_); return; }
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

#if defined(ECHO_WITH_SDL_TTF)
        if (TTF_Init() == 0) {
            const char* fp = std::getenv("ECHO_HUD_FONT");
            std::string path = fp ? fp : "C:/Windows/Fonts/segoeui.ttf";
            font_ = TTF_OpenFont(path.c_str(), 26);
            if (!font_) log_warn("hud", "HUD font not found; subtitles will be omitted");
        }
#endif

        while (running_) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) { /* keep the window responsive */ }
            draw_once(w, h);
            SDL_Delay(33);  // ~30 fps
        }

#if defined(ECHO_WITH_SDL_TTF)
        if (font_) TTF_CloseFont(font_);
        TTF_Quit();
#endif
        if (renderer_) SDL_DestroyRenderer(renderer_);
        if (window_)   SDL_DestroyWindow(window_);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    void draw_once(int w, int h) {
        HudFrame f;
        bool have;
        {
            std::lock_guard<std::mutex> lk(mu_);
            // Expire the subtitle once its TTL passes — the HUD never accumulates.
            if (have_frame_ && frame_.has_subtitle) {
                auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - shown_at_).count();
                if (age > frame_.subtitle.ttl_ms) frame_.has_subtitle = false;
            }
            f = frame_; have = have_frame_;
        }

        // Calm dark band.
        SDL_SetRenderDrawColor(renderer_, 12, 14, 18, 235);
        SDL_RenderClear(renderer_);

        if (have) {
            if (f.has_status) draw_status_dot(f.status.kind, w);
            if (f.has_icon)   draw_icon(f.icon.glyph);
            if (f.has_subtitle) draw_subtitle(f.subtitle.text, w, h);
        }
        SDL_RenderPresent(renderer_);
    }

    void fill(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a = 255) {
        SDL_SetRenderDrawColor(renderer_, r, g, b, a);
        SDL_Rect rect{x, y, w, h};
        SDL_RenderFillRect(renderer_, &rect);
    }

    void draw_status_dot(StatusKind k, int w) {
        Uint8 r = 120, g = 120, b = 130;  // idle grey
        switch (k) {
            case StatusKind::Idle:    break;
            case StatusKind::Working: r = 70;  g = 130; b = 235; break;  // blue
            case StatusKind::Success: r = 60;  g = 200; b = 110; break;  // green
            case StatusKind::Error:   r = 225; g = 80;  b = 80;  break;  // red
        }
        fill(w - 34, 22, 14, 14, r, g, b);  // top-right dot
    }

    // Each icon is one simple mark — never a row of controls (that would be a GUI).
    void draw_icon(Glyph gph) {
        const int x = 26, y = 24, s = 40;
        const Uint8 r = 220, g = 224, b = 230;
        switch (gph) {
            case Glyph::Play:
                for (int i = 0; i < s; ++i)  // filled triangle
                    fill(x, y + i * (s / 2) / s, (s * (s - i)) / s / 2 + 1, 1, r, g, b);
                break;
            case Glyph::Pause:
                fill(x, y, s / 3, s, r, g, b);
                fill(x + 2 * s / 3, y, s / 3, s, r, g, b);
                break;
            case Glyph::Stop:
                fill(x, y, s, s, r, g, b);
                break;
            case Glyph::None:
                break;
            default:  // remaining glyphs: a calm filled diamond placeholder mark
                for (int i = 0; i < s; ++i) {
                    int half = (i < s / 2) ? i : (s - i);
                    fill(x + s / 2 - half, y + i, 2 * half + 1, 1, r, g, b);
                }
                break;
        }
    }

    void draw_subtitle(const std::string& text, int w, int h) {
#if defined(ECHO_WITH_SDL_TTF)
        if (!font_ || text.empty()) return;
        SDL_Color col{235, 238, 242, 255};
        SDL_Surface* surf = TTF_RenderUTF8_Blended_Wrapped(font_, text.c_str(), col, w - 100);
        if (!surf) return;
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
        SDL_Rect dst{80, (h - surf->h) / 2, surf->w, surf->h};
        SDL_RenderCopy(renderer_, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
        SDL_FreeSurface(surf);
#else
        // Without SDL_ttf we can't rasterize text; show a thin "caption present"
        // underline so the band still reflects that a subtitle was emitted.
        (void)text;
        fill(80, h - 30, w - 160, 3, 120, 140, 180);
#endif
    }

    SDL_Window*   window_   = nullptr;
    SDL_Renderer* renderer_ = nullptr;
#if defined(ECHO_WITH_SDL_TTF)
    TTF_Font*     font_     = nullptr;
#endif
    std::thread   thread_;
    std::atomic<bool> running_{false};

    std::mutex  mu_;
    HudFrame    frame_;
    std::string app_id_;
    std::chrono::steady_clock::time_point shown_at_{};
    bool        have_frame_ = false;
};

#endif  // ECHO_WITH_SDL

}  // namespace

std::unique_ptr<IHudCompositor> make_hud_compositor(HudMode mode) {
    switch (mode) {
        case HudMode::Headless:     return std::make_unique<HeadlessCompositor>();
        case HudMode::TerminalBand: return std::make_unique<SimulatedWindowCompositor>();
        case HudMode::Window:
#if defined(ECHO_WITH_SDL)
            return std::make_unique<SdlHudCompositor>();
#else
            log_warn("hud", "SDL not built in; HUD window falls back to terminal band");
            return std::make_unique<SimulatedWindowCompositor>();
#endif
    }
    return std::make_unique<HeadlessCompositor>();
}

std::unique_ptr<IHudCompositor> make_hud_compositor(bool simulated_window) {
    return make_hud_compositor(simulated_window ? HudMode::TerminalBand : HudMode::Headless);
}

}  // namespace echo::apps::hud
