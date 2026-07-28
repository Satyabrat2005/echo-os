// ECHO OS appkit — the confirm-before-action gate (constraint #1).
//
// No app may take a real-world, state-changing action (send an email, start
// playback that costs data, place a call) on the strength of a single utterance.
// This device serves a vulnerable user, and an ASR mishear must never become an
// irreversible act. Every such action goes through this gate:
//
//   1. arm(action)         -> stages what WOULD happen and speaks a summary asking
//                             for confirmation. Nothing has happened yet.
//   2. decide(next_utter)  -> only an explicit affirmative ("send", "yes",
//                             "confirm") commits. Anything else — an explicit "no",
//                             an unrelated command, silence turned into noise —
//                             does NOT act and clears the pending action.
//
// The bias is deliberate and asymmetric: the cost of a wrongly-cancelled action is
// the user repeating themselves; the cost of a wrongly-taken one is an email the
// user never meant to send. So ambiguity always resolves to "do not act."
//
// This gate is dependency-free and single-threaded (the host serializes commands),
// which is exactly why it is the one piece of Phase 5 that is fully covered in CI:
// the real network call can't run there, but the gate that guards it can.
#pragma once

#include <map>
#include <string>

namespace echo::apps::confirm {

// A described-but-not-yet-taken action awaiting confirmation.
struct PendingAction {
    std::string                        kind;     // "send_email", "start_playback", ...
    std::string                        summary;  // spoken back to the user verbatim
    std::map<std::string, std::string> params;   // opaque payload for the caller
};

enum class Decision {
    NoPending,     // decide() called with nothing armed
    Confirmed,     // explicit affirmative — caller may now act
    Declined,      // explicit negative ("no", "cancel", "stop")
    Unrecognized,  // neither — treated as "do not act" (fail-safe)
};

class ConfirmationGate {
public:
    // Stage an action. Supersedes any previously-armed one (a fresh request means
    // the old prompt is stale). After this, armed() is true.
    void arm(PendingAction action);

    bool                  armed() const noexcept { return armed_; }
    const PendingAction&  pending() const noexcept { return pending_; }

    // Interpret an utterance as the response to the armed action. In every outcome
    // except a still-pending... there is no still-pending: this ALWAYS clears the
    // gate (one-shot). Confirmed is returned only for an explicit affirmative.
    Decision decide(const std::string& utterance);

    void clear() noexcept;

    // Shared vocabulary so the gate, the apps, and the tests agree on exactly what
    // a yes and a no are. Case-insensitive; tolerant of a few surrounding words
    // ("yes send it", "no don't"). An utterance that is both (mis-parsed) is NOT
    // treated as affirmative — see the .cpp.
    static bool is_affirmative(const std::string& utterance);
    static bool is_negative(const std::string& utterance);

private:
    bool          armed_ = false;
    PendingAction pending_;
};

}  // namespace echo::apps::confirm
