// ECHO OS — the caregiver link (Phase 22).
//
// The coordinator that sits between three things that must not know about each
// other: the memory engine (which holds the day), companion-sync (which talks off
// the device), and voice-ui (which tells the wearer what just happened).
//
// WHY IT LIVES IN boot/ AND NOT IN companion-sync.
// Building a digest means counting reminders and events — which means naming
// memory types. companion-sync must remain structurally unable to do that; the
// Phase 10/15 compile-time proof says no send path accepts a PersonRecord or an
// EventRecord, and that proof is only worth anything if the module genuinely cannot
// see those types at all. So companion-sync gets counts pushed in as plain integers
// and a scope pushed in as a plain enum, and the module that holds both halves is
// echo::runtime, which already links memory, companion, and voice and already owns
// the tick and the EngineGuards.
//
// WHAT CROSSES, IN EACH DIRECTION:
//   memory -> here:  counts (ints) and a ConsentRecord. Never rows.
//   here -> companion: a CaregiverDigest of scalars, and a ConsentScope enum.
//   companion -> here: validated InboundCommands, already length-capped,
//                      schema-checked, replay-checked and rate-limited.
//   here -> voice:    an announcement, every single time something is applied.
//
// DEFENCE IN DEPTH ON CONSENT. This class refuses to BUILD a digest without
// consent, and companion-sync separately refuses to SEND one. Either check alone
// would be sufficient; having both means a future refactor has to break two
// independent things, in two modules, to leak a digest — and each is tested on its
// own in tests/caregiver_link_test.cpp.
#pragma once

#include <cstdint>
#include <string>

#include "echo/companion/companion_sync.hpp"
#include "echo/memory/memory_engine.hpp"
#include "echo/result.hpp"
#include "echo/voice/voice_ui.hpp"

namespace echo::boot {

// How far back a digest looks. A day, because that is the unit a caregiver actually
// asks about ("did she take the morning pills?"), and because a longer window would
// make the counts a behavioural profile rather than a status check.
inline constexpr std::uint32_t kDigestWindowHours = 24;

class CaregiverLink {
public:
    // Non-owning. The runtime owns the engines; this borrows them for the tick.
    // Any of them may be null, and every method degrades rather than dereferences.
    CaregiverLink(memory::IMemoryEngine* memory, companion::ICompanionSync* companion,
                  voice::IVoiceUi* voice) noexcept
        : memory_(memory), companion_(companion), voice_(voice) {}

    // Record consent. Persisted in the memory engine like any other record, so it
    // survives a reboot; pushed into companion-sync so enforcement is immediate.
    Status grant(ConsentScope scope, ConsentGrantor grantor, memory::UnixTime now);

    // Withdraw it. Immediate: the store is updated, the enum pushed into
    // companion-sync is cleared in the same call, and the next sync() reasserts
    // None from the store. There is no window in which a revoked link still works.
    Status revoke(memory::UnixTime now);

    // Re-read consent from the store and push it into companion-sync. Called every
    // tick. This is the mechanism that makes revocation "go cold on the next tick"
    // a property of the DESIGN rather than a thing someone has to remember to do.
    void sync_consent();

    // Build a digest for the window ending at `now`.
    //
    // Fails with Unavailable when there is no consent — it does not return an empty
    // digest, for the same reason send_digest() doesn't: an all-zero digest is a
    // factual claim ("nothing happened today"), and this device must not make it
    // when what is true is "you are not permitted to see this".
    Result<companion::CaregiverDigest> build_digest(memory::UnixTime now);

    // Build and send. Unavailable when consent is absent or the link is down.
    Status push_digest(memory::UnixTime now);

    // Drain, apply, and announce inbound caregiver commands. Returns how many were
    // applied. Every applied command is spoken to the wearer first — ADR-10's
    // confirm-before-send discipline, applied to a write coming the other way.
    int drain_inbound(memory::UnixTime now);

    memory::UnixTime last_sync_at() const noexcept { return last_sync_at_; }
    int applied_commands() const noexcept { return applied_commands_; }
    int refused_commands() const noexcept { return refused_commands_; }

private:
    void announce(const std::string& text);

    memory::IMemoryEngine*     memory_ = nullptr;
    companion::ICompanionSync* companion_ = nullptr;
    voice::IVoiceUi*           voice_ = nullptr;

    memory::UnixTime last_sync_at_ = 0;
    int applied_commands_ = 0;
    int refused_commands_ = 0;
};

}  // namespace echo::boot
