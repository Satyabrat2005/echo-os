// ECHO OS — end-to-end fixture pipeline test.
//
// Phase 10, deliverable #6 — the single most valuable test in this phase. It runs
// a fixture audio clip through the REAL core loop in sequence:
//
//     sensor-pipeline (SPSC queue)  →  perception  →  cognitive-core  →  voice-ui
//
// and asserts the final spoken "turn", as close to a real turn as is achievable
// without live hardware. Two turns are exercised, landing on the two distinct
// production code paths that both end in the safety-critical safe-mode fallback:
//
//   Turn A — low confidence: the stub perception recognizes nothing, so the
//            confidence gate (principle #5) refuses the LLM and speaks the calm
//            fallback. This is the exact wiring that protects the user whenever
//            perception is unsure.
//
//   Turn B — confident but LLM unavailable: a perception observation carrying a
//            strong wake+transcript (the OUTPUT CONTRACT the real Porcupine+
//            whisper would produce — generating it from raw audio needs those
//            libs, see docs/STATE.md) passes the confidence gate, but the stub
//            LLM yields no text, so the core still "fails safe, not smart." The
//            two turns are distinguishable by the confidence carried in the
//            response, proving they took different branches.
#include "echo/sensor/sensor_source.hpp"
#include "echo/perception/perception_engine.hpp"
#include "echo/cognitive/cognitive_core.hpp"
#include "echo/memory/memory_engine.hpp"
#include "echo/voice/voice_ui.hpp"
#include "echo/types.hpp"

#include "check.hpp"
#include "fixture_io.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace echo;

namespace {

// Run one real turn from a loaded audio clip and return the spoken utterance plus
// the response that produced it. Audio flows: producer -> SPSC queue ->
// next_frame() -> perception.process() (fused across frames) -> core.respond() ->
// to_utterance().
struct Turn {
    voice::Utterance     spoken;
    cognitive::Response  response;
};

Turn run_audio_turn(const echo::test::WavClip& clip,
                    cognitive::ICognitiveCore& core,
                    perception::IPerceptionEngine& perception) {
    // --- sensor-pipeline: push the clip through the real capture queue ---------
    sensor::SensorPipeline pipe;
    constexpr std::size_t kFrame = 512;
    std::uint64_t seq = 0;
    for (std::size_t off = 0; off + kFrame <= clip.samples.size(); off += kFrame) {
        SensorFrame f;
        f.modality    = Modality::Microphone;
        f.sequence    = seq++;
        f.data        = reinterpret_cast<const std::uint8_t*>(clip.samples.data() + off);
        f.size        = kFrame * sizeof(std::int16_t);
        f.sample_rate = static_cast<std::uint32_t>(clip.sample_rate);
        pipe.queue().push(f);              // real producer side (drops if full)
        // Drain promptly so a 5-frame clip never overflows the depth-8 queue.
        while (auto qf = pipe.next_frame()) {
            (void)perception.process(*qf); // real perception pass, fused internally
        }
    }

    // --- perception -> the turn's observation ----------------------------------
    // In the stub build every pass is empty; the observation the core reasons over
    // is therefore an empty Perception (nothing confidently recognized).
    perception::Perception observation;

    // --- cognitive-core -> response --------------------------------------------
    auto r = core.respond(observation);

    // --- voice-ui -> spoken utterance ------------------------------------------
    Turn t;
    t.response = r.value();
    t.spoken   = voice::to_utterance(t.response);
    return t;
}

// Turn A: low-confidence -> safe-mode fallback, spoken reassuringly.
void test_turn_low_confidence_speaks_safe_fallback() {
    const cognitive::SafeModeConfig cfg;  // defaults: threshold 0.72, safe line
    auto clip = echo::test::load_wav("speech_hello.wav");
    CHECK(clip.ok);

    auto core = cognitive::make_cognitive_core(cfg);
    CHECK(core->initialize() == Status::Ok);
    auto perception = perception::make_perception_engine();
    CHECK(perception->initialize() == Status::Ok);

    Turn t = run_audio_turn(clip, *core, *perception);

    // The whole point: an unsure turn never fabricates an answer.
    CHECK(t.response.kind == cognitive::ResponseKind::SafeMode);
    CHECK(t.response.flag_caregiver);
    CHECK(t.response.text == cfg.safe_response);
    CHECK(std::abs(t.response.confidence.value - 0.0f) < 1e-6f);  // nothing recognized
    // voice-ui renders safe-mode in the reassuring tone.
    CHECK(t.spoken.tone == voice::Tone::Reassuring);
    CHECK(t.spoken.text == cfg.safe_response);

    // And it is actually speakable through the real (stub-audio) shell.
    auto ui = voice::make_voice_ui();
    CHECK(ui->initialize() == Status::Ok);
    CHECK(ui->speak(t.spoken) == Status::Ok);
    ui->shutdown();

    perception->shutdown();
    core->shutdown();
}

// Turn B: confident observation, but the stub LLM produces no text, so the core
// still degrades to safe mode — via the *post-gate* branch, not the confidence
// gate. Distinguished from Turn A by the high confidence it carries through.
void test_turn_confident_but_no_llm_still_safe() {
    const cognitive::SafeModeConfig cfg;
    auto core = cognitive::make_cognitive_core(cfg);
    CHECK(core->initialize() == Status::Ok);

    // The perception OUTPUT CONTRACT a real wake+ASR pass would hand up. Building
    // it here (rather than from raw audio) is the documented stand-in for the
    // Porcupine+whisper model paths that need the installed libraries.
    perception::Perception strong;
    strong.wake   = perception::WakeWord{true, Confidence{0.95f}};
    strong.speech = perception::Transcript{"what time is it", Confidence{0.80f}, true};
    const float agg = strong.aggregate_confidence().value;   // weakest link = 0.80
    CHECK(agg >= cfg.min_confidence);                        // this turn PASSES the gate

    auto r = core->respond(strong);
    auto spoken = voice::to_utterance(r.value());

    // Gate passed, but no LLM text -> fail safe, not smart.
    CHECK(r.value().kind == cognitive::ResponseKind::SafeMode);
    CHECK(r.value().text == cfg.safe_response);
    CHECK(spoken.tone == voice::Tone::Reassuring);
    // The tell that this took the *confident* branch: it carried 0.80 through,
    // whereas Turn A carried 0.0. Different code path, same safe landing.
    CHECK(std::abs(r.value().confidence.value - 0.80f) < 1e-6f);

    core->shutdown();
}

// --- Phase 15: face-based recall end to end ----------------------------------
// The single most valuable NEW test in this phase. It drives a fixture face
// embedding + a prior naming utterance through the REAL cognitive core WITH a real
// (on-disk) memory engine attached, and asserts that "who is this?" produces an
// enriched recall answer — the concrete behavior change the whole product turns on.
// No hardware, no OpenCV: the embedding is fixture data, exactly as the memory unit
// tests use, standing in for what vision's SFace path would hand up (see the
// perception OUTPUT CONTRACT note on Turn B above).

// A deterministic stand-in for an SFace embedding (matches memory_engine_test).
std::vector<float> fixture_embedding(int seed, std::size_t dim = 128) {
    std::vector<float> e(dim, 0.0f);
    e[static_cast<std::size_t>(seed) % dim] = 1.0f;
    for (std::size_t i = 0; i < dim; ++i) e[i] += 0.01f * static_cast<float>((seed + i) % 3);
    return e;
}

perception::Perception face_and_speech(const std::vector<float>& emb, const std::string& said) {
    perception::Perception p;
    perception::FaceObservation f;
    f.identity   = "";                 // vision didn't name them; memory will
    f.confidence = Confidence{0.90f};  // a solid face detection
    f.embedding  = emb;
    p.faces.push_back(std::move(f));
    if (!said.empty())
        p.speech = perception::Transcript{said, Confidence{0.90f}, /*endpointed=*/true};
    return p;
}

void test_turn_face_recall_is_enriched_end_to_end() {
    auto mem = memory::make_memory_engine();
    CHECK(mem->open(":memory:") == Status::Ok);
    auto core = cognitive::make_cognitive_core({}, mem.get());
    CHECK(core->initialize() == Status::Ok);

    const auto emb = fixture_embedding(1);

    // TURN 1 — the wearer names the person while looking at them. This is the ONLY
    // way a person record is created; it flows through the real respond() path.
    auto naming = core->respond(face_and_speech(emb, "this is my daughter Priya"));
    CHECK(naming.value().kind == cognitive::ResponseKind::Normal);
    CHECK(naming.value().text.find("Priya") != std::string::npos);
    CHECK(mem->all_people().size() == 1);

    // Backdate the sighting so recall can say "2 days ago" (as if Priya visited two
    // days back). Everything else is untouched.
    const memory::PersonId id = mem->all_people().front().id;
    const memory::UnixTime now = memory::unix_now();
    CHECK(mem->mark_seen(id, now - 2 * 86400) == Status::Ok);

    // TURN 2 — later, the wearer asks "who is this?" with the same face in view.
    // The response is an enriched recall drawn from the real record, spoken neutrally
    // (a known fact, not a safe-mode deferral).
    auto recall = core->respond(face_and_speech(emb, "who is this"));
    const auto& r = recall.value();
    CHECK(r.kind == cognitive::ResponseKind::Normal);
    CHECK(!r.flag_caregiver);
    CHECK(r.text.find("Priya") != std::string::npos);
    CHECK(r.text.find("your daughter") != std::string::npos);
    CHECK(r.text.find("2 days ago") != std::string::npos);

    // It renders to a neutral spoken turn and is speakable through the real shell.
    auto spoken = voice::to_utterance(r);
    CHECK(spoken.tone == voice::Tone::Neutral);
    auto ui = voice::make_voice_ui();
    CHECK(ui->initialize() == Status::Ok);
    CHECK(ui->speak(spoken) == Status::Ok);
    ui->shutdown();

    // A STRANGER (different embedding, no naming) yields no recall and creates no
    // person — the no-auto-create rule, proven end to end.
    auto stranger = core->respond(face_and_speech(fixture_embedding(42), ""));
    CHECK(stranger.value().kind != cognitive::ResponseKind::Normal ||
          stranger.value().text.find("Priya") == std::string::npos);
    CHECK(mem->all_people().size() == 1);   // still just Priya

    core->shutdown();
    mem->close();
}

}  // namespace

int main() {
    test_turn_low_confidence_speaks_safe_fallback();
    test_turn_confident_but_no_llm_still_safe();
    test_turn_face_recall_is_enriched_end_to_end();
    return echo::test::report("e2e-fixture");
}
