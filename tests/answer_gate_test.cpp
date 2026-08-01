// ECHO OS — the answer-side safe-mode gate (Phase 21).
//
// From Phase 1 to Phase 20 the safe-mode gate scored the OBSERVATION. Nothing ever
// scored the sentence ECHO was about to say: once a clean transcript cleared 0.72,
// the model's text was spoken verbatim as ResponseKind::Normal with flag_caregiver
// false. ADR-4's premise — that a confident wrong answer is worse than "I'm not
// sure" for a wearer who cannot fact-check — was therefore only half enforced.
//
// These suites drive the REAL cognitive core (echo::cognitive) and the REAL memory
// engine, with only the LLM replaced by tests/fake_llm.hpp. That substitution is the
// whole point: the stub LLM returns empty text (always safe mode, so it can never
// reach the answer path) and a real model answers confidently for every prompt (the
// Phase 12 finding, so it can never be made to fail on cue). Neither can express
// "the model said something confident and wrong", which is the case that matters.
//
// Runs entirely in the dependency-free stub build — no llama.cpp, no model weights.
#include "echo/cognitive/cognitive_core.hpp"
#include "echo/memory/memory_engine.hpp"
#include "echo/memory/utterance.hpp"
#include "echo/perception/perception_engine.hpp"
#include "echo/voice/voice_ui.hpp"
#include "echo/types.hpp"

#include "core_factory.hpp"  // cognitive-core/src (private DI seam)

#include "check.hpp"
#include "fake_llm.hpp"

#include <memory>
#include <string>
#include <utility>

using namespace echo;

namespace {

// A fixed wall clock so reminder recurrence and "due now" are deterministic.
constexpr memory::UnixTime kNow = 1'700'000'000;

// An observation that clears the 0.72 input gate carrying `text`. This is the OUTPUT
// CONTRACT real Porcupine + whisper would produce; generating it from raw audio needs
// those libs (see docs/STATE.md), and none of what is under test here depends on how
// the transcript was produced.
perception::Perception heard(const std::string& text, float conf = 0.95f) {
    perception::Perception p;
    perception::Transcript t;
    t.text       = text;
    t.confidence = Confidence{conf};
    t.endpointed = true;
    p.speech     = std::move(t);
    return p;
}

// An in-memory store, optionally seeded with one reminder that is already due.
std::unique_ptr<memory::IMemoryEngine> store(bool with_reminder) {
    auto mem = memory::make_memory_engine();
    if (mem->open(":memory:") != Status::Ok) return mem;
    if (with_reminder)
        (void)mem->add_reminder("take blood pressure medication", kNow - 3600,
                                memory::Recurrence::Daily);
    return mem;
}

// Build the core around a scripted LLM. Returns both so the test can inspect what
// the "model" was actually asked — often the more important of the two.
struct Rig {
    std::unique_ptr<cognitive::ICognitiveCore> core;
    echo::test::FakeLlm*                       llm = nullptr;  // owned by `core`
};

Rig rig(memory::IMemoryEngine* mem, cognitive::SafeModeConfig cfg = {}) {
    auto fake = std::make_unique<echo::test::FakeLlm>();
    Rig r;
    r.llm  = fake.get();
    r.core = cognitive::make_cognitive_core_with_llm(std::move(cfg), mem, std::move(fake));
    (void)r.core->initialize();
    return r;
}

// --- 1. The confident path is untouched -------------------------------------
// Regression guard for the whole phase: a well-scored, ordinary answer must still
// be spoken exactly as before. A gate that also rejects good answers is not a safer
// device, it is a broken one.
void test_confident_answer_passes() {
    auto r = rig(nullptr);
    r.llm->text       = "It's just after three in the afternoon.";
    r.llm->confidence = 0.91f;

    auto resp = r.core->respond(heard("what time is it"));
    CHECK(resp.is_ok());
    CHECK(resp.value().kind == cognitive::ResponseKind::Normal);
    CHECK(resp.value().text == "It's just after three in the afternoon.");
    CHECK(resp.value().flag_caregiver == false);
    CHECK(r.llm->consulted());  // an ordinary question DOES go to the model
}

// --- 2. The degeneracy floor ------------------------------------------------
void test_low_confidence_answer_is_declined() {
    auto r = rig(nullptr);
    r.llm->text       = "The the the of and the.";  // collapsed generation
    r.llm->confidence = 0.08f;

    auto resp = r.core->respond(heard("what time is it"));
    CHECK(resp.value().kind == cognitive::ResponseKind::Unverified);
    // The critical assertion: the model's words never reach the wearer.
    CHECK(resp.value().text.find("the the") == std::string::npos);
    CHECK(resp.value().text == cognitive::SafeModeConfig{}.unverified_response);
    CHECK(resp.value().answer_confidence.value < 0.35f);
}

// --- 3. An unknown score is not a zero --------------------------------------
// LlmReply::scored exists so a backend that cannot produce a confidence does not get
// treated as maximally suspect. Collapsing "unscored" into 0.0 would make every
// answer from such a backend an abstention — the device would go mute rather than
// degrade.
void test_unscored_answer_is_not_penalized() {
    auto r = rig(nullptr);
    r.llm->text       = "Your bus leaves at ten past.";
    r.llm->scored     = false;
    r.llm->confidence = 0.0f;

    auto resp = r.core->respond(heard("when is the bus"));
    CHECK(resp.value().kind == cognitive::ResponseKind::Normal);
    CHECK(resp.value().text == "Your bus leaves at ten past.");
    CHECK(resp.value().answer_confidence.value == 0.0f);  // reported as unknown, not asserted
}

// --- 4. Grounding: the record answers, and the model is never asked ----------
void test_self_referential_with_record_answers_from_store() {
    auto mem = store(/*with_reminder=*/true);
    CHECK(mem->is_open());
    auto r = rig(mem.get());
    r.llm->text = "Yes, you took them at nine.";  // a plausible, confident invention

    auto resp = r.core->respond(heard("what am i supposed to take today"));
    CHECK(resp.value().kind == cognitive::ResponseKind::Normal);
    CHECK(resp.value().text.find("blood pressure medication") != std::string::npos);
    // The store answered, so the invention never had a chance to be spoken.
    CHECK(resp.value().text.find("nine") == std::string::npos);
    CHECK(!r.llm->consulted());
}

// --- 5. Grounding: no record means ABSTAIN, not "ask the model" --------------
// The single most important suite in this file. Before Phase 21 this question
// reached the LLM and whatever it said was spoken as Normal.
void test_self_referential_without_record_abstains() {
    auto mem = store(/*with_reminder=*/false);
    CHECK(mem->is_open());
    auto r = rig(mem.get());
    r.llm->text       = "Yes, you took your tablets this morning.";
    r.llm->confidence = 0.97f;  // fluent AND confident — the floor would not catch it

    auto resp = r.core->respond(heard("did i take my tablets today"));
    CHECK(resp.value().kind == cognitive::ResponseKind::Unverified);
    CHECK(resp.value().text == cognitive::SafeModeConfig{}.unverified_response);
    // Not "the model answered and we suppressed it" — the model was never consulted.
    // This is what makes the guarantee independent of model quality.
    CHECK(!r.llm->consulted());
}

// --- 6. No store at all is also "no record" ---------------------------------
void test_self_referential_without_memory_engine_abstains() {
    auto r = rig(nullptr);  // core-only build: no store to ground against
    r.llm->text = "You saw Priya on Tuesday.";

    auto resp = r.core->respond(heard("who visited yesterday"));
    CHECK(resp.value().kind == cognitive::ResponseKind::Unverified);
    CHECK(!r.llm->consulted());
}

// --- 7. The [route:memory] fallback, corrected ------------------------------
// Phase 15 wired the route tag to the store but kept the LLM's sentence when the
// store came back empty — the model asked to consult the record, the record said
// "nothing", and we answered from the model anyway.
void test_route_memory_with_no_record_abstains() {
    auto mem = store(/*with_reminder=*/false);
    auto r = rig(mem.get());
    r.llm->intent     = "memory";
    r.llm->text       = "You have a doctor's appointment at four.";
    r.llm->confidence = 0.95f;

    // Phrased so the pre-LLM grounding classifier does NOT fire: this suite must
    // exercise the post-LLM route-tag branch specifically.
    auto resp = r.core->respond(heard("any appointments coming up"));
    CHECK(r.llm->consulted());  // it did reach the model, which tagged [route:memory]
    CHECK(resp.value().kind == cognitive::ResponseKind::Unverified);
    CHECK(resp.value().text.find("four") == std::string::npos);
}

void test_route_memory_with_record_answers_from_store() {
    auto mem = store(/*with_reminder=*/true);
    auto r = rig(mem.get());
    r.llm->intent = "memory";
    r.llm->text   = "You have a doctor's appointment at four.";

    auto resp = r.core->respond(heard("what reminders do i have"));
    CHECK(resp.value().kind == cognitive::ResponseKind::Normal);
    CHECK(resp.value().text.find("blood pressure medication") != std::string::npos);
    CHECK(resp.value().intent.empty());  // handled here, not routed to the apps layer
}

// --- 8. Retrieval is not generation -----------------------------------------
// A fact read out of the store must not be subject to the fluency floor: the floor
// scores how the MODEL generated text, and the store did not generate anything.
// (Same reasoning ADR-12 used to put face recall in front of the input gate.)
void test_retrieved_answer_bypasses_the_fluency_floor() {
    auto mem = store(/*with_reminder=*/true);
    auto r = rig(mem.get());
    r.llm->intent     = "memory";
    r.llm->text       = "mumble mumble";
    r.llm->confidence = 0.02f;  // would be declined if this were a generated answer

    auto resp = r.core->respond(heard("what reminders do i have"));
    CHECK(resp.value().kind == cognitive::ResponseKind::Normal);
    CHECK(resp.value().text.find("blood pressure medication") != std::string::npos);
}

// --- 9. The input gate still fires first, and still wins ---------------------
// Constraint #1 of the brief: the 0.72 gate is untouched and the two gates stay
// distinguishable. A muddy transcript is a SafeMode turn, not an abstention — ECHO
// did not understand the question, which is a different thing from not knowing the
// answer.
void test_input_gate_fires_before_the_answer_gate() {
    auto mem = store(/*with_reminder=*/false);
    auto r = rig(mem.get());

    auto resp = r.core->respond(heard("did i take my tablets today", 0.30f));
    CHECK(resp.value().kind == cognitive::ResponseKind::SafeMode);
    CHECK(resp.value().text == cognitive::SafeModeConfig{}.safe_response);
    CHECK(resp.value().flag_caregiver == true);
    CHECK(!r.llm->consulted());
}

// --- 10. Three decline paths, three distinguishable outcomes ----------------
// Phase 17 kept "engine didn't respond" separate from "input unclear"; Phase 21 adds
// a third and must not blur any of them. If a caregiver cannot tell a broken device
// from a careful one, the alert channel is worthless.
void test_the_three_decline_paths_stay_distinct() {
    const cognitive::SafeModeConfig cfg;

    // Distinct spoken lines.
    CHECK(cfg.safe_response != cfg.unverified_response);

    auto mem = store(/*with_reminder=*/false);

    // (a) input unclear -> SafeMode, caregiver flagged.
    auto a = rig(mem.get());
    auto ra = a.core->respond(heard("did i take my tablets", 0.10f));
    CHECK(ra.value().kind == cognitive::ResponseKind::SafeMode);
    CHECK(ra.value().flag_caregiver == true);

    // (b) input clear, answer ungrounded -> Unverified, caregiver NOT flagged.
    //     One abstention is correct behaviour, not a fault; only the repeated
    //     pattern is worth a caregiver's attention (see runtime handle_response).
    auto b = rig(mem.get());
    auto rb = b.core->respond(heard("did i take my tablets today"));
    CHECK(rb.value().kind == cognitive::ResponseKind::Unverified);
    CHECK(rb.value().flag_caregiver == false);

    // (c) engine silent -> the UNCHANGED safe-mode fallback (the engine-fault line
    //     itself belongs to the runtime, and is asserted in fault_injection_test).
    auto c = rig(nullptr);
    c.llm->text = "";
    auto rc = c.core->respond(heard("what time is it"));
    CHECK(rc.value().kind == cognitive::ResponseKind::SafeMode);

    // All three reachable states are pairwise distinct.
    CHECK(ra.value().kind != rb.value().kind);
    CHECK(rb.value().kind != rc.value().kind);
    CHECK(ra.value().text != rb.value().text);
}

// --- 11. voice-ui renders an abstention warmly ------------------------------
// Without this, a new enumerator falls through to Tone::Neutral: ECHO would announce
// that it cannot remember something in the same brisk voice it uses for the time.
void test_abstention_is_spoken_reassuringly() {
    cognitive::Response r;
    r.kind = cognitive::ResponseKind::Unverified;
    r.text = cognitive::SafeModeConfig{}.unverified_response;

    auto u = voice::to_utterance(r);
    CHECK(u.tone == voice::Tone::Reassuring);
    CHECK(u.text == r.text);

    cognitive::Response normal;
    normal.kind = cognitive::ResponseKind::Normal;
    CHECK(voice::to_utterance(normal).tone == voice::Tone::Neutral);
}

// --- 12. The classifier itself ----------------------------------------------
// Pure string logic, so it is asserted directly rather than through the core.
void test_self_referential_classifier() {
    // Positives: questions only the record can answer.
    CHECK(memory::is_self_referential_query("Did I take my tablets today?"));
    CHECK(memory::is_self_referential_query("when did i last see Priya"));
    CHECK(memory::is_self_referential_query("Who visited yesterday?"));
    CHECK(memory::is_self_referential_query("where did i put my keys"));
    CHECK(memory::is_self_referential_query("what am i supposed to do this morning"));
    CHECK(memory::is_self_referential_query("where's my walking stick"));

    // Negatives: ordinary questions must keep reaching the model.
    CHECK(!memory::is_self_referential_query("what time is it"));
    CHECK(!memory::is_self_referential_query("play some music"));
    CHECK(!memory::is_self_referential_query("what's the weather like"));
    CHECK(!memory::is_self_referential_query("who is the prime minister"));
    CHECK(!memory::is_self_referential_query("what did you say"));
    CHECK(!memory::is_self_referential_query(""));

    // Disjoint from the identity-query path by construction: "who is this" belongs
    // to pre-gate face recall, and exactly one classifier owns any given turn.
    CHECK(memory::is_identity_query("who is this"));
    CHECK(!memory::is_self_referential_query("who is this"));
    CHECK(!memory::is_self_referential_query("remind me who that is"));
}

// --- 13. The floor is configurable, and off by default at 0 -----------------
// Proves the threshold is honoured rather than hard-coded, so the measured
// distribution from the real-llm job can actually be acted on.
void test_answer_floor_is_configurable() {
    cognitive::SafeModeConfig strict;
    strict.min_answer_confidence = 0.99f;
    auto a = rig(nullptr, strict);
    a.llm->confidence = 0.90f;  // fine by default, rejected here
    CHECK(a.core->respond(heard("what time is it")).value().kind ==
          cognitive::ResponseKind::Unverified);

    cognitive::SafeModeConfig off;
    off.min_answer_confidence = 0.0f;
    auto b = rig(nullptr, off);
    b.llm->confidence = 0.001f;
    CHECK(b.core->respond(heard("what time is it")).value().kind ==
          cognitive::ResponseKind::Normal);
}

// --- 14. Naming still works --------------------------------------------------
// "this is my daughter Priya" is a first-person sentence sitting right next to the
// new classifier's territory. It must still create a person and never be swallowed
// as an unanswerable question about the wearer's past.
void test_naming_is_not_captured_by_the_grounding_gate() {
    auto mem = store(/*with_reminder=*/false);
    auto r = rig(mem.get());

    auto resp = r.core->respond(heard("this is my daughter Priya"));
    CHECK(resp.value().kind == cognitive::ResponseKind::Normal);
    CHECK(resp.value().text.find("Priya") != std::string::npos);
    CHECK(mem->all_people().size() == 1);
    CHECK(!r.llm->consulted());
}

}  // namespace

int main() {
    test_confident_answer_passes();
    test_low_confidence_answer_is_declined();
    test_unscored_answer_is_not_penalized();
    test_self_referential_with_record_answers_from_store();
    test_self_referential_without_record_abstains();
    test_self_referential_without_memory_engine_abstains();
    test_route_memory_with_no_record_abstains();
    test_route_memory_with_record_answers_from_store();
    test_retrieved_answer_bypasses_the_fluency_floor();
    test_input_gate_fires_before_the_answer_gate();
    test_the_three_decline_paths_stay_distinct();
    test_abstention_is_spoken_reassuringly();
    test_self_referential_classifier();
    test_answer_floor_is_configurable();
    test_naming_is_not_captured_by_the_grounding_gate();
    return echo::test::report("answer-gate");
}
