#include "echo/voice/voice_ui.hpp"
#include "echo/config.hpp"
#include "echo/latency.hpp"
#include "echo/log.hpp"

#include "tts_synth.hpp"

#include <cstdio>
#include <string>

#if defined(ECHO_WITH_PIPER)
#include <SDL.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#endif

namespace echo::voice {

Utterance to_utterance(const cognitive::Response& response) {
    Utterance u;
    u.text = response.text;
    u.tone = (response.kind == cognitive::ResponseKind::SafeMode)
                 ? Tone::Reassuring
                 : Tone::Neutral;
    return u;
}

namespace {

const char* to_string(Tone t) noexcept {
    switch (t) {
        case Tone::Neutral:    return "neutral";
        case Tone::Reassuring: return "reassuring";
        case Tone::Alert:      return "alert";
    }
    return "neutral";
}

#if defined(ECHO_WITH_PIPER)

// Real TTS: shell out to the local `piper` binary (fully on-device) to synthesize
// a WAV, then play it through SDL audio. Playback is non-blocking — speak()
// returns once the first samples are queued and the device is unpaused, keeping
// the core loop responsive (principle #3).
class PiperVoiceUi final : public IVoiceUi {
public:
    Status initialize() override {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            log_error("voice", "SDL audio init failed; TTS disabled");
            return Status::NotReady;
        }
        tmp_wav_ = (std::filesystem::temp_directory_path() / "echo_tts_out.wav").string();
        log_info("voice", "voice UI initialized (Piper TTS + SDL audio)");
        return Status::Ok;
    }

    Status speak(const Utterance& utterance) override {
        StageTimer timer(Stage::VoiceOutput);
        (void)timer;
        if (utterance.text.empty()) return Status::Ok;

        // 1+2) synthesize a WAV with the tone's pacing (headless, no audio device)
        Status s = detail::piper_synthesize(utterance.text, utterance.tone, tmp_wav_);
        if (s != Status::Ok) return s;

        // 3) play (non-blocking)
        return play_wav(tmp_wav_);
    }

    Status play_earcon(std::string_view name) override {
        // Earcons are short pre-rendered WAVs under models/earcons/<name>.wav.
        std::string path = (std::filesystem::path(config::models_dir()) /
                            "earcons" / (std::string(name) + ".wav")).string();
        if (std::filesystem::exists(path)) return play_wav(path);
        return Status::Ok;  // absent earcon is not an error
    }

    void barge_in() override {
        if (dev_) { SDL_ClearQueuedAudio(dev_); SDL_PauseAudioDevice(dev_, 1); }
    }

    void shutdown() override {
        close_device();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        log_info("voice", "voice UI shut down");
    }

private:
    Status play_wav(const std::string& path) {
        SDL_AudioSpec spec;
        Uint8* buf = nullptr;
        Uint32 len = 0;
        if (!SDL_LoadWAV(path.c_str(), &spec, &buf, &len)) {
            log_warn("voice", "could not load synthesized WAV");
            return Status::Unavailable;
        }
        barge_in();       // stop anything mid-flight (double-buffered swap)
        close_device();
        spec.callback = nullptr;  // use the queue API
        dev_ = SDL_OpenAudioDevice(nullptr, 0, &spec, nullptr, 0);
        if (dev_ == 0) { SDL_FreeWAV(buf); return Status::Unavailable; }
        SDL_QueueAudio(dev_, buf, len);
        SDL_FreeWAV(buf);
        SDL_PauseAudioDevice(dev_, 0);  // start playback; return immediately
        return Status::Ok;
    }
    void close_device() { if (dev_) { SDL_CloseAudioDevice(dev_); dev_ = 0; } }

    SDL_AudioDeviceID dev_ = 0;
    std::string tmp_wav_;
};

#endif  // ECHO_WITH_PIPER

// Stub TTS: logs what it would speak. Used by CI and by any build without Piper.
class StubVoiceUi final : public IVoiceUi {
public:
    Status initialize() override {
        log_info("voice", "voice UI initialized (stub TTS)");
        return Status::Ok;
    }

    Status speak(const Utterance& utterance) override {
        StageTimer timer(Stage::VoiceOutput);
        (void)timer;
        char buf[256];
        (void)std::snprintf(buf, sizeof(buf), "speak[%s]: %.*s",
                            to_string(utterance.tone),
                            static_cast<int>(utterance.text.size()), utterance.text.data());
        log_info("voice", buf);
        return Status::Ok;
    }

    Status play_earcon(std::string_view name) override {
        char buf[128];
        (void)std::snprintf(buf, sizeof(buf), "earcon: %.*s",
                            static_cast<int>(name.size()), name.data());
        log_info("voice", buf);
        return Status::Ok;
    }

    void barge_in() override { log_debug("voice", "barge-in: stopping playback"); }
    void shutdown() override { log_info("voice", "voice UI shut down"); }
};

}  // namespace

// --- Internal synthesis helpers (declared in tts_synth.hpp) ------------------
// Kept out of the anonymous namespace so the Phase 11 real-engine test can link
// them and exercise the real Piper path without the SDL playback step.
#if defined(ECHO_WITH_PIPER)
namespace detail {

double length_scale_for(Tone t) noexcept {
    switch (t) {
        case Tone::Reassuring: return 1.15;  // slower, calmer for safe-mode/distress
        case Tone::Alert:      return 0.95;  // a touch quicker to catch attention
        case Tone::Neutral:    return 1.0;
    }
    return 1.0;
}

Status piper_synthesize(const std::string& text, Tone tone, const std::string& out_wav) {
    if (text.empty()) return Status::Unavailable;  // nothing to synthesize

    // text -> temp file (avoids shell-quoting the whole utterance). This is the
    // file-producing half of speak(); no audio device is touched, so it is safe
    // on a headless CI runner.
    const std::string tin =
        (std::filesystem::temp_directory_path() / "echo_tts_synth_in.txt").string();
    { std::ofstream(tin) << text; }

    char cmd[1024];
    std::snprintf(cmd, sizeof(cmd),
                  "\"%s\" --model \"%s\" --length_scale %.2f --output_file \"%s\" < \"%s\"",
                  config::piper_binary().c_str(), config::piper_voice().c_str(),
                  length_scale_for(tone), out_wav.c_str(), tin.c_str());
#if defined(_WIN32)
    // std::system() runs the command through cmd.exe, which strips the first and
    // last double-quote of the WHOLE command line. With several quoted tokens here
    // (piper path, voice model, output, stdin file) — and a piper path that can
    // contain spaces (e.g. "C:\Users\First Last\...") — that stripping corrupts the
    // command and piper silently fails. Wrapping the entire command in one more pair
    // of quotes makes cmd.exe strip THOSE, passing the inner command through intact.
    // POSIX /bin/sh has no such rule, so this is guarded to Windows (where it was a
    // latent bug: Linux CI paths had no spaces, so it never surfaced until the real
    // Windows bring-up). See cmd.exe quoting rules (`cmd /?`).
    const std::string full = std::string("\"") + cmd + "\"";
#else
    const std::string full = cmd;
#endif
    if (std::system(full.c_str()) != 0) {
        log_warn("voice", "piper synthesis failed");
        return Status::Unavailable;
    }
    return Status::Ok;
}

}  // namespace detail
#endif  // ECHO_WITH_PIPER

std::unique_ptr<IVoiceUi> make_voice_ui() {
#if defined(ECHO_WITH_PIPER)
    return std::make_unique<PiperVoiceUi>();
#else
    return std::make_unique<StubVoiceUi>();
#endif
}

}  // namespace echo::voice
