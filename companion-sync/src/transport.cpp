// ECHO OS — companion transport backends. See transport.hpp for what each one is
// and, more importantly, what each one does not prove.
#include "echo/companion/transport.hpp"

#include "echo/log.hpp"

#include <deque>
#include <memory>
#include <mutex>

namespace echo::companion {

const char* to_string(TransportKind kind) noexcept {
    switch (kind) {
        case TransportKind::Fake:     return "fake";
        case TransportKind::Loopback: return "loopback";
        case TransportKind::Ble:      return "ble";
    }
    return "unknown";
}

namespace {

// --- Fake: scriptable, deterministic, no framing of its own -------------------
class FakeTransport final : public IFakeTransport {
public:
    Status open() override { open_ = true; return Status::Ok; }
    void   close() override { open_ = false; }
    bool   is_open() const noexcept override { return open_; }
    TransportKind kind() const noexcept override { return TransportKind::Fake; }

    Status send(const Frame& frame) override {
        if (!open_) return Status::Unavailable;
        if (fail_next_) { fail_next_ = false; return Status::HardwareError; }
        sent_.push_back(frame);
        return Status::Ok;
    }

    std::vector<Frame> receive() override {
        std::vector<Frame> out;
        out.swap(inbox_);
        return out;
    }

    std::vector<Frame> sent() const override { return sent_; }

    void inject(const Frame& frame) override { inbox_.push_back(frame); }
    void fail_next_send(bool fail) override { fail_next_ = fail; }

private:
    bool               open_ = false;
    bool               fail_next_ = false;
    std::vector<Frame> sent_;
    std::vector<Frame> inbox_;
};

// --- Loopback: two endpoints wired to each other, in-process ------------------
// The queues are shared between the halves, so what one sends the other receives.
// A mutex because the runtime tick and a test's peer half can plausibly live on
// different threads; the queues are small and never held across a call out.
struct LoopbackWires {
    std::mutex        mu;
    std::deque<Frame> to_device;
    std::deque<Frame> to_peer;
};

class LoopbackEndpoint final : public ICompanionTransport {
public:
    LoopbackEndpoint(std::shared_ptr<LoopbackWires> wires, bool is_device)
        : wires_(std::move(wires)), is_device_(is_device) {}

    Status open() override { open_ = true; return Status::Ok; }
    void   close() override { open_ = false; }
    bool   is_open() const noexcept override { return open_; }
    TransportKind kind() const noexcept override { return TransportKind::Loopback; }

    Status send(const Frame& frame) override {
        if (!open_) return Status::Unavailable;
        std::lock_guard<std::mutex> lock(wires_->mu);
        // A device write lands in the peer's inbox, and vice versa.
        (is_device_ ? wires_->to_peer : wires_->to_device).push_back(frame);
        return Status::Ok;
    }

    std::vector<Frame> receive() override {
        std::vector<Frame> out;
        std::lock_guard<std::mutex> lock(wires_->mu);
        auto& q = is_device_ ? wires_->to_device : wires_->to_peer;
        while (!q.empty()) {
            out.push_back(std::move(q.front()));
            q.pop_front();
        }
        return out;
    }

private:
    std::shared_ptr<LoopbackWires> wires_;
    bool                           is_device_;
    bool                           open_ = false;
};

// --- BLE: a stub, and labelled as one ----------------------------------------
class BleTransport final : public ICompanionTransport {
public:
    Status open() override {
        // Deliberately NOT faked. Real BLE GATT needs a stack, a service/
        // characteristic table, a pairing UI, a peer device, and hardware to run
        // them on. None of that exists here, and a radio that "works" in CI would
        // be a claim this project has not earned. Unavailable is the truth.
        log_warn("companion", "BLE transport is a documented stub; no radio on this build");
        return Status::Unavailable;
    }
    void   close() override {}
    bool   is_open() const noexcept override { return false; }
    TransportKind kind() const noexcept override { return TransportKind::Ble; }
    Status send(const Frame&) override { return Status::Unavailable; }
    std::vector<Frame> receive() override { return {}; }
};

}  // namespace

std::unique_ptr<IFakeTransport> make_fake_transport() {
    return std::make_unique<FakeTransport>();
}

LoopbackPair make_loopback_transport() {
    auto wires = std::make_shared<LoopbackWires>();
    LoopbackPair pair;
    pair.device = std::make_unique<LoopbackEndpoint>(wires, /*is_device=*/true);
    pair.peer   = std::make_unique<LoopbackEndpoint>(wires, /*is_device=*/false);
    return pair;
}

std::unique_ptr<ICompanionTransport> make_ble_transport() {
    return std::make_unique<BleTransport>();
}

}  // namespace echo::companion
