// ECHO OS — the companion transport seam (Phase 22).
//
// The ADR-9 pattern, applied to the radio: one narrow interface, a deterministic
// in-process fake for CI, an honest stand-in that exercises the whole protocol,
// and a real backend that is a documented stub because it needs hardware.
//
// WHAT EACH BACKEND IS, PLAINLY:
//
//   make_fake_transport()      A scriptable queue. Bytes go in, bytes come out,
//                              and a test can inject exactly the frame it wants —
//                              including corrupt ones. Proves the LINK LOGIC.
//
//   make_loopback_transport()  A paired in-process endpoint that runs the REAL
//                              framing, the REAL AES-256-CTR, and the REAL
//                              AES-256-CMAC end to end. Proves the PROTOCOL AND
//                              CRYPTO layer.
//
//                              It is in-process on purpose. The phase spec suggests
//                              "loopback / local socket", but the stub build must
//                              have no runtime network in CI — a bound port is a
//                              thing that fails in a sandbox, on a locked-down
//                              runner, or when two jobs race for it, and a flaky
//                              privacy test is worse than no test. Bytes cross a
//                              queue instead of a socket. Everything above the
//                              socket is identical; nothing about the radio is
//                              proven by it, and this comment is the honest label.
//
//   make_ble_transport()       A STUB. open() returns Unavailable. Real BLE GATT
//                              means a stack, a pairing UI, a peer, and hardware to
//                              run them on — none of which exist here. It is not
//                              faked, because a fake radio that "works" in CI is a
//                              claim this project has no business making. See the
//                              gap table in docs/STATE.md.
//
// The interface is byte-oriented and knows nothing about digests, consent, or
// commands. Confidentiality and integrity live one layer up, in SecureChannel, so
// that a transport can never be the thing that decides whether a frame was
// authentic.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "echo/result.hpp"

namespace echo::companion {

enum class TransportKind : std::uint8_t { Fake, Loopback, Ble };

const char* to_string(TransportKind kind) noexcept;

// A frame is an opaque, already-sealed byte string. The transport moves it; it does
// not inspect it, and it certainly does not authenticate it.
using Frame = std::vector<std::uint8_t>;

class ICompanionTransport {
public:
    virtual ~ICompanionTransport() = default;

    // Bring the link up. Unavailable is a legitimate, expected answer (no radio, no
    // peer, no hardware) and every caller must treat it as one.
    virtual Status open() = 0;
    virtual void   close() = 0;
    virtual bool   is_open() const noexcept = 0;

    virtual TransportKind kind() const noexcept = 0;

    // Send one sealed frame. Returns Unavailable when the link is down.
    virtual Status send(const Frame& frame) = 0;

    // Drain everything that has arrived since the last call. Never blocks: this is
    // called from the runtime tick, and a tick that can block on a radio is a tick
    // that can stall the wearer's reminders.
    virtual std::vector<Frame> receive() = 0;

    // Frames this endpoint has SENT, for tests that need to inspect the wire. The
    // real backends may return an empty vector; only the fake is required to record.
    virtual std::vector<Frame> sent() const { return {}; }
};

// A deterministic, scriptable transport. `inject()` makes a frame appear as though
// it arrived from the peer; `sent()` returns everything written to the wire.
class IFakeTransport : public ICompanionTransport {
public:
    virtual void inject(const Frame& frame) = 0;
    virtual void fail_next_send(bool fail) = 0;
};

std::unique_ptr<IFakeTransport> make_fake_transport();

// Two endpoints wired to each other in-process: what A sends, B receives. Returns
// the pair; either half can be handed to a companion-sync instance.
struct LoopbackPair {
    std::unique_ptr<ICompanionTransport> device;    // the glasses' end
    std::unique_ptr<ICompanionTransport> peer;      // the test double for the phone
};
LoopbackPair make_loopback_transport();

// The real radio. A documented stub: open() returns Unavailable, always.
std::unique_ptr<ICompanionTransport> make_ble_transport();

}  // namespace echo::companion
