// ECHO OS — the caregiver link. See caregiver_link.hpp for why this coordinator
// lives in boot/ rather than in companion-sync.
#include "echo/boot/caregiver_link.hpp"

#include "echo/log.hpp"

namespace echo::boot {

namespace {

// Map the transport-side repeat enum to the store's. companion-sync deliberately
// does not name memory::Recurrence — that translation happens here, at the seam,
// which is the only place that legitimately knows both vocabularies.
memory::Recurrence to_recurrence(companion::CommandRepeat repeat) noexcept {
    switch (repeat) {
        case companion::CommandRepeat::Daily:  return memory::Recurrence::Daily;
        case companion::CommandRepeat::Weekly: return memory::Recurrence::Weekly;
        case companion::CommandRepeat::Once:   break;
    }
    return memory::Recurrence::Once;
}

std::uint32_t clamp_count(int n) noexcept {
    return n < 0 ? 0u : static_cast<std::uint32_t>(n);
}

}  // namespace

Status CaregiverLink::grant(ConsentScope scope, ConsentGrantor grantor, memory::UnixTime now) {
    if (!memory_) return Status::NotReady;
    const Status st = memory_->grant_consent(scope, grantor, now);
    if (st != Status::Ok) return st;

    // Push it through immediately as well as persisting it, so a grant takes effect
    // in this tick rather than the next one. (Revocation is the direction where
    // waiting a tick would matter, and it is handled the same way.)
    if (companion_) companion_->set_consent(scope);

    // The wearer is told. A caregiver link being switched on is exactly the kind of
    // change that must never happen silently on someone's own device.
    announce("A caregiver link is now active.");
    log_info("caregiver", std::string("consent granted: ") + to_string(scope));
    return Status::Ok;
}

Status CaregiverLink::revoke(memory::UnixTime now) {
    if (!memory_) return Status::NotReady;
    const Status st = memory_->revoke_consent(now);

    // Clear the enforcement side even if the write failed. If the store cannot
    // record a revocation, the safe outcome is still that the link stops working —
    // failing to persist "off" must never leave the device sending.
    if (companion_) companion_->set_consent(ConsentScope::None);

    announce("The caregiver link is now off.");
    log_info("caregiver", "consent revoked");
    return st;
}

void CaregiverLink::sync_consent() {
    if (!companion_) return;
    if (!memory_) {
        companion_->set_consent(ConsentScope::None);
        return;
    }
    // Re-read from the store every tick rather than caching. A cache here is
    // precisely what would let a revoked link keep working for "just one more tick",
    // and that tick is the one a caregiver could see something in.
    const memory::ConsentRecord rec = memory_->consent();
    companion_->set_consent(rec.active() ? rec.scope : ConsentScope::None);
}

Result<companion::CaregiverDigest> CaregiverLink::build_digest(memory::UnixTime now) {
    using R = Result<companion::CaregiverDigest>;
    if (!memory_) return R::fail(Status::NotReady);

    const memory::ConsentRecord rec = memory_->consent();
    if (!rec.active() || !permits_digest(rec.scope)) {
        // Not an empty digest. See the header: "nothing happened today" and "you are
        // not permitted to see this" are opposite claims, and only one of them is
        // true here.
        return R::fail(Status::Unavailable);
    }

    const memory::UnixTime window = static_cast<memory::UnixTime>(kDigestWindowHours) * 3600;
    const memory::UnixTime since  = now - window;

    companion::CaregiverDigest d;
    d.window_hours = kDigestWindowHours;

    // Every one of these is a COUNT, computed inside the memory engine. No row, no
    // summary, no name, and no free text crosses into this function — which is what
    // makes the digest minimal by construction rather than by careful assembly.
    d.reminders_due          = clamp_count(memory_->count_reminders_due(since, now));
    d.reminders_delivered    = clamp_count(memory_->count_events(memory::EventKind::ReminderFired, since, now));
    d.reminders_acknowledged = clamp_count(memory_->count_reminders_acknowledged(since, now));
    d.safe_mode_engagements  = clamp_count(memory_->count_events(memory::EventKind::SafeModeEngaged, since, now));
    d.unverified_answers     = clamp_count(memory_->count_events(memory::EventKind::UnverifiedAnswer, since, now));
    // How many people are enrolled. Never who. Phase 15's rule stands.
    d.people_known           = clamp_count(memory_->count_people());
    // Phase 23: how the wearer's day went, same bucket as safe_mode_engagements/
    // unverified_answers above — counted on the alert edge (see Runtime::apply_safety_
    // decision), not once per tick a condition holds.
    d.wandering_flags = clamp_count(memory_->count_events(memory::EventKind::WanderingFlagged, since, now));
    d.distress_flags  = clamp_count(memory_->count_events(memory::EventKind::DistressFlagged, since, now));

    d.generated_at = now;
    d.last_sync_at = last_sync_at_;
    d.scope        = rec.scope;
    d.state        = RuntimeState::Ready;

    return R::ok(d);
}

Status CaregiverLink::push_digest(memory::UnixTime now) {
    if (!companion_) return Status::NotReady;

    const auto built = build_digest(now);
    if (!built.is_ok()) return built.status();

    const Status st = companion_->send_digest(built.value());
    if (st == Status::Ok) last_sync_at_ = now;
    return st;
}

int CaregiverLink::drain_inbound(memory::UnixTime now) {
    if (!companion_ || !memory_) return 0;

    int applied = 0;
    for (const companion::InboundCommand& cmd : companion_->poll_inbound(now)) {
        switch (cmd.kind) {
            case companion::CommandKind::AddReminder: {
                const auto id = memory_->add_reminder(cmd.text, cmd.due, to_recurrence(cmd.repeat));
                if (!id.is_ok()) {
                    ++refused_commands_;
                    break;
                }
                // Announced, never silently applied. A caregiver-set reminder is a
                // WRITE to the wearer's device, and ADR-10's confirm-before-send
                // discipline applies just as much to something arriving as to
                // something leaving. The wearer may not be able to consent
                // meaningfully in the moment — that is the condition they have — but
                // "changed while you weren't looking" is not a thing this device does
                // to the person wearing it.
                announce("A caregiver added a reminder: " + cmd.text + ".");
                memory::EventRecord ev;
                ev.kind = memory::EventKind::CaregiverCommand;
                ev.at = now;
                ev.summary = "caregiver added a reminder";
                ev.reminder_id = id.value();
                memory_->log_event(ev);
                ++applied;
                ++applied_commands_;
                break;
            }

            case companion::CommandKind::EnrolName: {
                // THE NAMING RULE, IN CODE. A pre-enrolled person gets a name and an
                // EMPTY embedding. Nothing is bound to a face here, and there is no
                // field on an InboundCommand that could carry one — a payload that
                // even mentions biometric data was refused back in the decoder.
                //
                // Phase 15's rule is that a face is bound to a name only by the
                // wearer, on-device, in the moment. Pre-enrolment prepares the name;
                // it does not perform the binding, and it must never be allowed to.
                const auto id = memory_->remember_person(cmd.name, cmd.relation,
                                                         memory::Embedding{}, now);
                if (!id.is_ok()) {
                    ++refused_commands_;
                    break;
                }
                announce("A caregiver added " + cmd.name + " to your people.");
                memory::EventRecord ev;
                ev.kind = memory::EventKind::CaregiverCommand;
                ev.at = now;
                ev.summary = "caregiver pre-enrolled a name";
                ev.person_id = id.value();
                memory_->log_event(ev);
                ++applied;
                ++applied_commands_;
                break;
            }

            case companion::CommandKind::RequestDigest: {
                // Read-only, so it is not announced: telling the wearer out loud every
                // time a caregiver refreshes a screen would be noise, not consent, and
                // the wearer already agreed to the digest when they granted the scope.
                push_digest(now);
                ++applied;
                ++applied_commands_;
                break;
            }

            case companion::CommandKind::Unknown:
            default:
                // Unreachable: poll_inbound() only returns commands on the allowlist.
                ++refused_commands_;
                break;
        }
    }
    return applied;
}

void CaregiverLink::announce(const std::string& text) {
    if (!voice_) return;
    // Deliberately unguarded by an EngineGuard here: the runtime wraps CaregiverLink
    // calls in its own guard (see Runtime::tick_impl), so wrapping again would be a
    // second timeout budget for the same operation.
    voice_->speak(voice::Utterance{text, voice::Tone::Neutral});
}

}  // namespace echo::boot
