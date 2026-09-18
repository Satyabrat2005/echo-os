// ECHO OS — the caregiver digest (Phase 22).
//
// This is the ONLY periodic, structured thing that may leave the device about the
// wearer's day, and it is deliberately made of nothing but counts and states.
//
// Read the guarantee precisely, because it is narrower than "nothing about memory
// may ever leave" and the difference matters. What Phases 10/15 proved — and what
// this phase must not weaken by a millimetre — is that RAW memory CONTENT has no
// path off-device: no SensorFrame, no Embedding, no PersonRecord, no EventRecord,
// no event summary, no note, no name. A caregiver who needs to know whether the 9am
// medication reminder was acknowledged does not need any of those. They need a
// number. So they get a number.
//
// WHY THE DIGEST HAS NO STRING, WHEN Alert::note DOES.
// The existing proof deliberately allows Alert::note to be a std::string, described
// there as a "short human-readable summary". That is defensible for an ALERT: it is
// one-off, it fires on an exceptional event, and a caregiver receiving "safe mode
// engaged" with no context can't act on it. A DIGEST is a different exposure
// entirely — it arrives on a schedule, forever, and a free-text field on a periodic
// feed is where identifying detail accumulates one well-meaning commit at a time.
// ("Just the reminder text." "Just the person's first name." "Just for debugging.")
// So the digest gets no such affordance, and the type system is what enforces that,
// not a reviewer's memory.
//
// HOW THE PROOF IS MADE EXHAUSTIVE.
// The obvious ways to assert "every field is a scalar or an enum" don't actually
// say that. is_trivially_copyable_v rules out std::string and std::vector but
// happily admits `char name[32]` or a `const char*` — precisely the fields someone
// would reach for. has_unique_object_representations_v and sizeof() checks are
// padding-fragile and go red for reasons unrelated to privacy. So the field list
// below is written ONCE, as an X-macro, and BOTH the struct and the per-field
// assertion are generated from it. A new field cannot be added without also being
// asserted — the proof cannot fall out of date, because there is no second list to
// forget to update. X-macros are otherwise unusual in this codebase; this is the
// one place the guarantee is worth the idiom.
#pragma once

#include <cstdint>
#include <type_traits>

#include "echo/consent.hpp"
#include "echo/types.hpp"

namespace echo::companion {

// The digest's fields, in one place. X(type, name, doc-comment).
//
// EVERY entry must be an arithmetic type or an enum. Adding a std::string, a
// std::vector, a pointer, or a char[] here fails the build immediately at the
// static_assert below — which is the point. If a caregiver's question cannot be
// answered by something on this list, the answer is to add a COUNT, not a string.
#define ECHO_CAREGIVER_DIGEST_FIELDS(X)                                                     \
    /* The window these counts cover, ending at generated_at. */                            \
    X(std::uint32_t, window_hours)                                                          \
    /* Reminders whose due time fell inside the window. */                                  \
    X(std::uint32_t, reminders_due)                                                         \
    /* ...of those, how many the device actually spoke. */                                  \
    X(std::uint32_t, reminders_delivered)                                                   \
    /* ...of those, how many the wearer acknowledged. The number a caregiver                \
       actually rings up about, and it is a number. */                                      \
    X(std::uint32_t, reminders_acknowledged)                                                \
    /* Times the system deferred to safe mode in the window (Phase 14/21). */               \
    X(std::uint32_t, safe_mode_engagements)                                                 \
    /* Times the system answered "I'm not sure" rather than guessing (Phase 21). */         \
    X(std::uint32_t, unverified_answers)                                                    \
    /* HOW MANY people are enrolled. Never WHO. Phase 15's rule is not negotiable:          \
       a name belongs to the wearer, on-device, in the moment. */                           \
    X(std::uint32_t, people_known)                                                          \
    /* Wandering/distress conditions CONFIRMED in the window (Phase 23). Same bucket as    \
       safe_mode_engagements/unverified_answers: how the wearer's day went, not device      \
       health — a device fault stays on the EngineDegraded alert channel, off the digest. */\
    X(std::uint32_t, wandering_flags)                                                       \
    X(std::uint32_t, distress_flags)                                                        \
    /* Unix seconds. Scalars, not TimePoint: a digest is serialized onto a wire and         \
       a clock type would drag std::chrono into the frame format for no benefit. */         \
    X(std::int64_t, generated_at)                                                           \
    X(std::int64_t, last_sync_at)                                                           \
    /* Coarse device state — is it awake, throttled, in safe mode. */                       \
    X(RuntimeState, state)                                                                  \
    /* The consent under which this digest was built, echoed back so the receiving          \
       side can never mistake a Digest-scope feed for permission to send commands. */       \
    X(ConsentScope, scope)

struct CaregiverDigest {
#define ECHO_DIGEST_DECLARE_FIELD(type, name) type name{};
    ECHO_CAREGIVER_DIGEST_FIELDS(ECHO_DIGEST_DECLARE_FIELD)
#undef ECHO_DIGEST_DECLARE_FIELD
};

// --- The per-field proof, generated from the same list as the fields ----------
// "Every digest field is a scalar or an enum, never a string or a buffer."
#define ECHO_DIGEST_ASSERT_FIELD(type, name)                                          \
    static_assert(std::is_arithmetic_v<type> || std::is_enum_v<type>,                 \
                  "PRIVACY: CaregiverDigest::" #name " must be a scalar or an enum. " \
                  "A digest carries counts and states — never a name, a note, a "     \
                  "summary, or any buffer that could hold one.");
ECHO_CAREGIVER_DIGEST_FIELDS(ECHO_DIGEST_ASSERT_FIELD)
#undef ECHO_DIGEST_ASSERT_FIELD

// Belt and braces on top of the field-by-field proof: no heap-owning member can
// hide in here, and the whole thing is memcpy-able onto a wire.
static_assert(std::is_trivially_copyable_v<CaregiverDigest>,
              "PRIVACY: CaregiverDigest must be trivially copyable — no owning members.");
static_assert(std::is_standard_layout_v<CaregiverDigest>,
              "CaregiverDigest is serialized field-by-field; keep it standard layout.");

}  // namespace echo::companion
