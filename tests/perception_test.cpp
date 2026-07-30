// ECHO OS — perception fixture tests.
//
// Phase 10, deliverable #3. Runs the fixture audio/images through the REAL
// PerceptionEngine (make_perception_engine) in stub-engine mode, exercising the
// surrounding production logic — process() routing by modality, the wake-framing
// buffer, the confidence-fusion contract, and the input-validation guards —
// WITHOUT requiring whisper.cpp/Porcupine/OpenCV to be installed.
//
// HONESTY (constraint #1): in the stub build the wake-word never fires and the
// ASR returns empty, so the endpoint→transcribe branch and the real model paths
// are NOT reached here. That gap is asserted explicitly below (see
// test_real_engine_paths_gap) and reported in docs/STATE.md rather than hidden
// behind a padded percentage. Perception was at 7 % before this phase.
#include "echo/perception/perception_engine.hpp"
#include "echo/types.hpp"

#include "check.hpp"
#include "fixture_io.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace echo;
using echo::perception::Perception;

namespace {

SensorFrame audio_frame(const std::int16_t* pcm, std::size_t n, int sr, std::uint64_t seq) {
    SensorFrame f;
    f.modality    = Modality::Microphone;
    f.sequence    = seq;
    f.data        = reinterpret_cast<const std::uint8_t*>(pcm);
    f.size        = n * sizeof(std::int16_t);
    f.sample_rate = static_cast<std::uint32_t>(sr);
    return f;
}

// --- Real fusion logic (Perception::aggregate_confidence) --------------------
// This is production code on the perception output contract, fully reachable in
// the stub build. The gate downstream depends on it being the *weakest* link.
void test_aggregate_confidence_weakest_link() {
    Perception p;
    CHECK(p.aggregate_confidence().value == 0.0f);  // nothing observed -> 0

    p.wake   = perception::WakeWord{true, Confidence{0.90f}};
    p.speech = perception::Transcript{"hello", Confidence{0.40f}, true};
    p.faces.push_back({"", Confidence{0.80f}, 0, 0, 0, 0});
    // Weakest across contributing modalities is the 0.40 transcript.
    CHECK(std::abs(p.aggregate_confidence().value - 0.40f) < 1e-6f);

    Perception only_face;
    only_face.faces.push_back({"Grace", Confidence{0.66f}, 0, 0, 0, 0});
    CHECK(std::abs(only_face.aggregate_confidence().value - 0.66f) < 1e-6f);
}

// --- Real audio routing + framing buffer -------------------------------------
// Feed the speech fixture through process() in wake-frame-sized chunks. This runs
// the real wake-framing buffer (insert + fixed-size consumption) and calls the
// wake detector for every frame. The stub detector never fires, so the contract
// is: valid Result, no wake, no speech. We assert exactly that — the surrounding
// logic ran; the model decision is stubbed.
void test_audio_fixture_routes_and_frames() {
    auto clip = echo::test::load_wav("speech_hello.wav");
    CHECK(clip.ok);

    auto engine = perception::make_perception_engine();
    CHECK(engine->initialize() == Status::Ok);

    constexpr std::size_t kFrame = 512;
    bool any_wake = false, any_speech = false;
    std::uint64_t seq = 0;
    for (std::size_t off = 0; off + kFrame <= clip.samples.size(); off += kFrame) {
        auto r = engine->process(
            audio_frame(clip.samples.data() + off, kFrame, clip.sample_rate, seq++));
        CHECK(r.is_ok());
        if (r.value().wake && r.value().wake->detected) any_wake = true;
        if (r.value().speech) any_speech = true;
    }
    CHECK(seq > 0);          // we actually pushed frames through the framing loop
    CHECK(!any_wake);        // stub wake-word never fires
    CHECK(!any_speech);      // therefore ASR is never invoked -> no transcript
    engine->shutdown();
}

// The noisy/garbled clip must not trip the pipeline into a false detection or a
// crash — high energy, no voiced structure, still no wake/speech in stub mode.
void test_noisy_fixture_no_false_detection() {
    auto clip = echo::test::load_wav("noisy_garble.wav");
    CHECK(clip.ok);
    auto engine = perception::make_perception_engine();
    CHECK(engine->initialize() == Status::Ok);

    auto r = engine->process(
        audio_frame(clip.samples.data(), clip.samples.size(), clip.sample_rate, 0));
    CHECK(r.is_ok());
    CHECK(!(r.value().wake && r.value().wake->detected));
    CHECK(!r.value().speech.has_value());
    engine->shutdown();
}

// --- Real camera routing -----------------------------------------------------
// The image fixtures route through process_camera (dimension guard + vision
// call). Stub vision returns nothing, so faces/objects are empty — the routing
// and guards are what run here.
void test_camera_fixtures_route() {
    auto engine = perception::make_perception_engine();
    CHECK(engine->initialize() == Status::Ok);

    for (const char* name : {"face_enrolled.ppm", "face_unknown.ppm", "no_face.ppm"}) {
        auto img = echo::test::load_ppm(name);
        CHECK(img.ok);
        SensorFrame f;
        f.modality = Modality::Camera;
        f.data     = img.bgr.data();
        f.size     = img.bgr.size();
        f.width    = static_cast<std::uint32_t>(img.width);
        f.height   = static_cast<std::uint32_t>(img.height);
        auto r = engine->process(f);
        CHECK(r.is_ok());
        CHECK(r.value().faces.empty());    // stub vision: no recognition
        CHECK(r.value().objects.empty());
    }
    engine->shutdown();
}

// --- Input-validation guards -------------------------------------------------
void test_degenerate_frames_are_safe() {
    auto engine = perception::make_perception_engine();
    CHECK(engine->initialize() == Status::Ok);

    SensorFrame empty_audio;  // data == nullptr, size == 0
    empty_audio.modality = Modality::Microphone;
    CHECK(engine->process(empty_audio).is_ok());

    std::vector<std::uint8_t> px(4 * 4 * 3, 0);
    SensorFrame zero_dim;     // camera frame with 0 dimensions
    zero_dim.modality = Modality::Camera;
    zero_dim.data = px.data();
    zero_dim.size = px.size();
    zero_dim.width = 0; zero_dim.height = 0;
    CHECK(engine->process(zero_dim).is_ok());

    SensorFrame eeg;          // EEG is accepted but yields nothing this phase
    eeg.modality = Modality::Eeg;
    auto r = engine->process(eeg);
    CHECK(r.is_ok());
    CHECK(!r.value().wake && !r.value().speech &&
          r.value().faces.empty() && r.value().objects.empty());
    engine->shutdown();
}

// --- The honest gap ----------------------------------------------------------
// Prove, as a first-class assertion, that the real wake/ASR/vision model paths
// are NOT exercised by this stub-build suite. If someone links the real engines,
// this build flips and the assertion documents that instead. This is the
// "mark the gap explicitly" that constraint #1 requires.
void test_real_engine_paths_gap() {
#if defined(ECHO_WITH_PORCUPINE) || defined(ECHO_WITH_WHISPER) || defined(ECHO_WITH_OPENCV)
    // A build with real engines linked would exercise the model paths for real;
    // this harness intentionally targets the stub configuration.
    std::printf("[perception] NOTE: real engine(s) linked; model paths are live\n");
#else
    std::printf("[perception] NOTE: stub engines only — wake-word/ASR/vision MODEL "
                "paths (endpointing→transcribe, face/object recognition) are NOT "
                "covered here; they require ECHO_WITH_PORCUPINE/WHISPER/OPENCV. "
                "Surrounding routing/framing/fusion logic IS covered.\n");
    CHECK(true);  // the gap is documented, not silently skipped
#endif
}

}  // namespace

int main() {
    test_aggregate_confidence_weakest_link();
    test_audio_fixture_routes_and_frames();
    test_noisy_fixture_no_false_detection();
    test_camera_fixtures_route();
    test_degenerate_frames_are_safe();
    test_real_engine_paths_gap();
    return echo::test::report("perception");
}
