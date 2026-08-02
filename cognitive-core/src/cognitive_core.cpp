#include "echo/cognitive/cognitive_core.hpp"
#include "echo/latency.hpp"
#include "echo/log.hpp"

#include "echo/memory/memory_engine.hpp"
#include "echo/memory/utterance.hpp"

#include "core_factory.hpp"
#include "llm.hpp"

#include <memory>
#include <optional>
#include <utility>

namespace echo::cognitive {
namespace {

// The cognitive core: the confidence-threshold safe-mode gate (principle #5) with
// the real quantized LLM wired in behind it, and — new in Phase 15 — the memory &
// recall engine wired in *around* it:
//
//   * Face recall runs BEFORE the gate. A confidently-matched known person is a
//     stored FACT, not an LLM guess, so recalling "that's Priya, your daughter"
//     is not something the safe-mode gate should suppress.
//   * Naming ("this is my daughter Priya") and the "[route:memory]" query both run
//     only on the CONFIDENT path — we never write a person's name, or answer a
//     memory question, off a shaky transcript.
//
// When no memory engine is attached (memory_ == nullptr) every memory branch is
// skipped and behavior is exactly the pre-Phase-15 core.
//
// Phase 21 adds the ANSWER-side gate. Everything above scores the OBSERVATION; from
// Phase 1 to Phase 20 nothing scored the sentence ECHO was about to say, so once a
// clean transcript cleared 0.72 the model's text was spoken verbatim as Normal with
// flag_caregiver = false. Two new checks close that, in order of how much they
// actually protect the wearer:
//
//   1. GROUNDING (the one that matters). A question about the wearer's own life is
//      answered from the store or not at all. No probability estimate is involved,
//      so nothing about model quality can weaken it.
//   2. A DEGENERACY FLOOR on the model's own mean token probability. This catches
//      collapsed generation. It does NOT catch a fluent falsehood, and is not
//      claimed to — see SafeModeConfig::min_answer_confidence.
class CognitiveCore final : public ICognitiveCore {
public:
    CognitiveCore(SafeModeConfig config, memory::IMemoryEngine* mem, std::unique_ptr<ILlm> llm)
        : config_(std::move(config)),
          llm_(llm ? std::move(llm) : make_llm()),
          memory_(mem) {}

    Status initialize() override {
        if (llm_->initialize() != Status::Ok)
            log_warn("cognitive", "LLM unavailable; core will use safe mode only");
        log_info("cognitive", "core initialized");
        return Status::Ok;
    }

    Result<Response> respond(const perception::Perception& observation) override {
        StageTimer timer(Stage::Cognitive);
        (void)timer;

        const memory::UnixTime now = memory::unix_now();

        // --- Face recall (pre-gate: retrieval of a fact, not generation) --------
        if (memory_ && memory_->is_open()) {
            if (auto recalled = try_face_recall(observation, now)) return Result<Response>::ok(std::move(*recalled));
        }

        const Confidence agg = observation.aggregate_confidence();

        // --- The safe-mode gate (UNCHANGED) ------------------------------------
        if (agg.below(config_.min_confidence)) {
            log_warn("cognitive", "confidence below threshold -> safe mode");
            return Result<Response>::ok(safe_mode(agg));
        }

        // Above threshold there must be a transcript to reason over.
        if (!observation.speech || observation.speech->text.empty())
            return Result<Response>::ok(safe_mode(agg));

        const std::string& utterance = observation.speech->text;

        // --- Naming: the ONLY path that creates a person (confident only) -------
        if (memory_ && memory_->is_open()) {
            if (auto named = try_naming(observation, utterance, now, agg))
                return Result<Response>::ok(std::move(*named));
        }

        // --- ANSWER GATE 1: grounding (Phase 21) -------------------------------
        // A question about the wearer's OWN life ("did I take my tablets?", "who
        // visited yesterday?") has exactly one acceptable source: the record. The
        // LLM is not consulted at all — not even as a fallback — because the whole
        // failure mode here is that it will answer anyway, fluently, and the one
        // person in the room cannot check it. If the store has nothing, ECHO says so.
        if (memory::is_self_referential_query(utterance)) {
            std::string answer;
            if (memory_ && memory_->is_open()) answer = memory_->answer_query(utterance, now);
            if (answer.empty()) {
                log_info("cognitive", "self-referential question with no record -> abstaining");
                return Result<Response>::ok(unverified(agg));
            }
            return Result<Response>::ok(grounded(std::move(answer), agg));
        }

        // --- Confident path: run the real LLM ----------------------------------
        LlmReply reply = llm_->generate(utterance);
        if (reply.text.empty()) {
            log_warn("cognitive", "LLM produced no text -> safe mode");
            return Result<Response>::ok(safe_mode(agg));
        }

        // --- Memory-query route-tag: answer from the REAL record ---------------
        // The LLM asked the memory engine (reusing Phase 6's route-tag parsing). We
        // replace the LLM's text with the store's real answer — a genuine retrieval
        // step, not a hallucination.
        //
        // CHANGED IN PHASE 21: when the store has nothing concrete we now ABSTAIN.
        // Until now this branch kept the LLM's own sentence as a fallback, which had
        // the logic exactly backwards — the model asked to consult the record, the
        // record came back empty, and we answered from the model anyway. The one
        // case where the store positively knows it has nothing is the one case where
        // a guess is least defensible.
        if (reply.intent == "memory") {
            std::string answer;
            if (memory_ && memory_->is_open()) answer = memory_->answer_query(utterance, now);
            if (answer.empty()) {
                log_info("cognitive", "[route:memory] with no record -> abstaining");
                return Result<Response>::ok(unverified(agg));
            }
            // Retrieved, not generated: the fluency floor below does not apply to a
            // fact read out of the store (the same reasoning ADR-12 used to put face
            // recall in front of the input gate).
            return Result<Response>::ok(grounded(std::move(answer), agg));
        }

        // --- ANSWER GATE 2: the degeneracy floor (Phase 21) --------------------
        // Only meaningful when the backend actually scored the generation. An
        // unscored reply is not treated as a zero — see LlmReply::scored.
        if (reply.scored && reply.confidence.below(config_.min_answer_confidence)) {
            log_warn("cognitive", "answer confidence below floor -> unverified");
            Response r = unverified(agg);
            r.answer_confidence = reply.confidence;
            return Result<Response>::ok(std::move(r));
        }

        Response r;
        r.kind              = ResponseKind::Normal;
        r.text              = std::move(reply.text);
        r.intent            = std::move(reply.intent);
        r.confidence        = agg;
        r.answer_confidence = reply.scored ? reply.confidence : Confidence{};
        r.flag_caregiver    = false;
        return Result<Response>::ok(std::move(r));
    }

    void shutdown() override {
        llm_->shutdown();
        log_info("cognitive", "core shut down");
    }

private:
    // Enriched recall for a recognized known person. Returns a response only when a
    // stored person matches a face embedding AND the wearer either said nothing or
    // asked "who is this" — so an unrelated question (e.g. "what time is it") with a
    // familiar face in view still goes to the LLM, not hijacked into a recall.
    std::optional<Response> try_face_recall(const perception::Perception& obs,
                                            memory::UnixTime now) {
        const bool has_speech = obs.speech && !obs.speech->text.empty();
        if (has_speech && !memory::is_identity_query(obs.speech->text)) return std::nullopt;

        for (const auto& face : obs.faces) {
            if (face.embedding.empty()) continue;
            memory::PersonMatch m = memory_->recognize(face.embedding);
            if (!m.matched) continue;

            // Build the phrase from the pre-update last_seen, THEN record the sighting.
            std::string text = memory::recall_sentence(m.person, now);
            memory_->mark_seen(m.person.id, now);

            Response r;
            r.kind           = ResponseKind::Normal;
            r.text           = std::move(text);
            r.confidence     = Confidence{m.similarity};
            r.flag_caregiver = false;
            return r;
        }
        return std::nullopt;
    }

    // Create a person when the wearer names one. Binds the name to a face embedding
    // present in the same observation (so it can be recalled later); if no face is
    // in view we still remember the name, just unanchored.
    std::optional<Response> try_naming(const perception::Perception& obs,
                                       const std::string& utterance,
                                       memory::UnixTime now, Confidence agg) {
        auto naming = memory::parse_naming(utterance);
        if (!naming) return std::nullopt;

        memory::Embedding emb;
        for (const auto& face : obs.faces)
            if (!face.embedding.empty()) { emb = face.embedding; break; }

        auto id = memory_->remember_person(naming->name, naming->relation, emb, now);
        if (!id) return std::nullopt;

        Response r;
        r.kind        = ResponseKind::Normal;
        r.confidence  = agg;
        r.text        = "Okay, I'll remember " + naming->name +
                        (naming->relation.empty() ? "" : ", " + naming->relation) + ".";
        if (emb.empty())
            r.text += " I couldn't see their face just now, so I may not recognize them yet.";
        r.flag_caregiver = false;
        return r;
    }

    Response safe_mode(Confidence agg) const {
        Response r;
        r.kind           = ResponseKind::SafeMode;
        r.text           = config_.safe_response;
        r.confidence     = agg;
        r.flag_caregiver = true;
        return r;
    }

    // The third decline path (Phase 21): we heard you, we just won't assert this.
    //
    // flag_caregiver is deliberately FALSE. A single "I don't have a record of that"
    // is ECHO working correctly, not a fault, and raising the caregiver every time
    // would bury the signal that matters under a stream of normal behaviour. It is
    // the *repeated* pattern that is worth a caregiver's attention, and that is a
    // trend the runtime observes across turns (AlertKind::LowConfidenceTrend), not
    // something a single turn can know. Keeping this false also stops an abstention
    // from dragging the whole runtime into RuntimeState::SafeMode.
    Response unverified(Confidence agg) const {
        Response r;
        r.kind           = ResponseKind::Unverified;
        r.text           = config_.unverified_response;
        r.confidence     = agg;
        r.flag_caregiver = false;
        return r;
    }

    // An answer read out of the store. Normal, because a retrieved fact is not a
    // guess — the same distinction that puts face recall in front of the input gate.
    Response grounded(std::string answer, Confidence agg) const {
        Response r;
        r.kind           = ResponseKind::Normal;
        r.text           = std::move(answer);
        r.confidence     = agg;
        r.flag_caregiver = false;
        return r;
    }

    SafeModeConfig          config_;
    std::unique_ptr<ILlm>   llm_;
    memory::IMemoryEngine*  memory_ = nullptr;  // non-owning; owned by the runtime
};

}  // namespace

std::unique_ptr<ICognitiveCore> make_cognitive_core(SafeModeConfig config,
                                                    memory::IMemoryEngine* memory) {
    return std::make_unique<CognitiveCore>(std::move(config), memory, nullptr);
}

std::unique_ptr<ICognitiveCore> make_cognitive_core_with_llm(SafeModeConfig config,
                                                             memory::IMemoryEngine* memory,
                                                             std::unique_ptr<ILlm> llm) {
    return std::make_unique<CognitiveCore>(std::move(config), memory, std::move(llm));
}

}  // namespace echo::cognitive
