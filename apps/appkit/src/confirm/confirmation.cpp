#include "echo/apps/confirm/confirmation.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace echo::apps::confirm {

namespace {

std::string lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Whole-word search: true if `word` appears in `hay` bounded by non-alphanumerics,
// so "no" does not match inside "now" and "send" does not match "sender".
bool has_word(const std::string& hay, const std::string& word) {
    std::size_t from = 0;
    while (true) {
        std::size_t at = hay.find(word, from);
        if (at == std::string::npos) return false;
        bool left_ok  = (at == 0) || !std::isalnum(static_cast<unsigned char>(hay[at - 1]));
        std::size_t end = at + word.size();
        bool right_ok = (end == hay.size()) ||
                        !std::isalnum(static_cast<unsigned char>(hay[end]));
        if (left_ok && right_ok) return true;
        from = at + 1;
    }
}

}  // namespace

bool ConfirmationGate::is_affirmative(const std::string& utterance) {
    const std::string u = lower(utterance);
    static const std::array<const char*, 8> kYes = {
        "send", "yes", "yeah", "yep", "confirm", "confirmed", "go ahead", "do it"};
    for (const char* w : kYes)
        if (has_word(u, w)) return true;
    return false;
}

bool ConfirmationGate::is_negative(const std::string& utterance) {
    const std::string u = lower(utterance);
    static const std::array<const char*, 8> kNo = {
        "no", "nope", "cancel", "stop", "don't", "dont", "nevermind", "wait"};
    for (const char* w : kNo)
        if (has_word(u, w)) return true;
    return false;
}

void ConfirmationGate::arm(PendingAction action) {
    pending_ = std::move(action);
    armed_   = true;
}

Decision ConfirmationGate::decide(const std::string& utterance) {
    if (!armed_) return Decision::NoPending;

    // One-shot: whatever happens, the gate is cleared so a later stray "yes" to an
    // unrelated command can never resurrect this action.
    const bool yes = is_affirmative(utterance);
    const bool no  = is_negative(utterance);
    clear();

    // Safety-first tie-break: an utterance that trips BOTH lists ("no, don't send")
    // is not a confirmation. Only a clean affirmative commits.
    if (yes && !no) return Decision::Confirmed;
    if (no)         return Decision::Declined;
    return Decision::Unrecognized;
}

void ConfirmationGate::clear() noexcept {
    armed_ = false;
    pending_ = PendingAction{};
}

}  // namespace echo::apps::confirm
