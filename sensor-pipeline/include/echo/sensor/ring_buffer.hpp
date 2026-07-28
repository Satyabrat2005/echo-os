// ECHO OS — single-producer / single-consumer lock-free ring buffer.
//
// The capture ISR (producer) and the perception thread (consumer) hand frames
// across this queue without a mutex, so a slow consumer can never block the
// real-time capture path — the defining requirement of the sensor stage's 5 ms
// budget. This is a bounded SPSC queue; on overflow the oldest slot is dropped
// (we favor fresh sensor data over completeness).
#pragma once

#include <atomic>
#include <array>
#include <cstddef>
#include <optional>

namespace echo::sensor {

template <typename T, std::size_t Capacity>
class SpscRingBuffer {
    static_assert(Capacity >= 2, "ring buffer needs at least two slots");

public:
    // Producer side. Returns false if the buffer is full (caller may drop).
    bool push(const T& item) noexcept {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto next = increment(head);
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;  // full
        }
        slots_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer side. Returns nullopt if empty.
    std::optional<T> pop() noexcept {
        const auto tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return std::nullopt;  // empty
        }
        T item = slots_[tail];
        tail_.store(increment(tail), std::memory_order_release);
        return item;
    }

    bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    static constexpr std::size_t capacity() noexcept { return Capacity - 1; }

private:
    static constexpr std::size_t increment(std::size_t i) noexcept {
        return (i + 1) % Capacity;
    }

    std::array<T, Capacity>  slots_{};
    std::atomic<std::size_t> head_{0};  // written by producer
    std::atomic<std::size_t> tail_{0};  // written by consumer
};

}  // namespace echo::sensor
