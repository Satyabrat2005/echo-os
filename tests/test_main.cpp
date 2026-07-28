// ECHO OS — dependency-free smoke tests.
//
// No external test framework: a tiny CHECK macro plus assertions, run under
// CTest. These guard the invariants that matter most in the scaffold — the
// latency budget arithmetic, the lock-free queue, and the safe-mode gate.
#include "echo/latency.hpp"
#include "echo/sensor/ring_buffer.hpp"
#include "echo/cognitive/cognitive_core.hpp"
#include "echo/cognitive/route_tag.hpp"
#include "echo/perception/perception_engine.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

// The per-stage budgets must sum to the advertised end-to-end target.
void test_latency_budget_sums() {
    double total = 0.0;
    for (double ms : echo::kBudgetMs) total += ms;
    CHECK(total == echo::kEndToEndBudgetMs);
}

// SPSC ring buffer: FIFO order, bounded, reports full without blocking.
void test_ring_buffer() {
    echo::sensor::SpscRingBuffer<int, 4> q;  // capacity() == 3
    CHECK(q.empty());
    CHECK(q.push(1));
    CHECK(q.push(2));
    CHECK(q.push(3));
    CHECK(!q.push(4));            // full: dropped, never blocks
    CHECK(q.pop().value() == 1);  // FIFO
    CHECK(q.pop().value() == 2);
    CHECK(q.push(5));             // room again
    CHECK(q.pop().value() == 3);
    CHECK(q.pop().value() == 5);
    CHECK(!q.pop().has_value());  // empty
}

// The safe-mode gate: a low-confidence observation must NOT yield a normal
// answer, must flag the caregiver, and must speak the fallback line.
void test_safe_mode_gate() {
    echo::cognitive::SafeModeConfig cfg;  // min_confidence = 0.72
    auto core = echo::cognitive::make_cognitive_core(cfg);
    CHECK(core->initialize() == echo::Status::Ok);

    // Build an observation that carries a weak face confidence.
    echo::perception::Perception weak;
    weak.faces.push_back({/*identity*/"", echo::Confidence{0.30f}, 0, 0, 0, 0});

    auto r = core->respond(weak);
    CHECK(r.is_ok());
    CHECK(r.value().kind == echo::cognitive::ResponseKind::SafeMode);
    CHECK(r.value().flag_caregiver);
    CHECK(r.value().text == cfg.safe_response);
}

// Route-tag parsing: the LLM's "[route:<app>]" prefix must be extracted (lower-
// cased, whitespace-tolerant) and stripped from the spoken reply; malformed or
// absent tags leave the text untouched and yield no intent. This guards the
// piece most likely to need adjustment against a real model (README Phase 4,
// Step 3) — and runs in the stub build, no llama.cpp required.
void test_route_tag_parsing() {
    using echo::cognitive::parse_route_tag;

    // The three exact utterances from the manual test flow.
    std::string music = "[route:media] Now playing Kind of Blue.";
    CHECK(parse_route_tag(music) == "media");
    CHECK(music == "Now playing Kind of Blue.");

    std::string who = "[route:camera] Taking a photo.";
    CHECK(parse_route_tag(who) == "camera");
    CHECK(who == "Taking a photo.");

    // Out-of-scope / conversational answer: no tag => no intent, text unchanged.
    std::string safe = "I don't know that, but you're safe here.";
    CHECK(parse_route_tag(safe).empty());
    CHECK(safe == "I don't know that, but you're safe here.");

    // Real models are untidy: inner spaces + mixed case still normalize.
    std::string messy = "[route:  Mail ] You have two messages.";
    CHECK(parse_route_tag(messy) == "mail");
    CHECK(messy == "You have two messages.");

    // Tag-only reply => empty spoken text (caller then routes silently).
    std::string tagOnly = "[route:telephony]";
    CHECK(parse_route_tag(tagOnly) == "telephony");
    CHECK(tagOnly.empty());

    // Malformed (no closing bracket): treated as plain text, nothing stripped.
    std::string malformed = "[route:search no bracket";
    CHECK(parse_route_tag(malformed).empty());
    CHECK(malformed == "[route:search no bracket");
}

}  // namespace

int main() {
    test_latency_budget_sums();
    test_ring_buffer();
    test_safe_mode_gate();
    test_route_tag_parsing();

    if (g_failures == 0) {
        std::printf("all tests passed\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return EXIT_FAILURE;
}
