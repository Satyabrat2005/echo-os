// ECHO OS — a tiny Result<T> for fallible pipeline stages.
//
// The core loop must never throw across a stage boundary: an exception on the
// audio thread is jank. Stages return Result<T> and the orchestrator decides
// whether to proceed, retry, or fall to safe mode.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <optional>

namespace echo {

enum class Status : std::uint8_t {
    Ok = 0,
    LowConfidence,   // ran, but below threshold — caller should consider safe mode
    NotReady,        // stage not initialized / warming up
    Timeout,         // exceeded its latency budget
    HardwareError,   // sensor / accelerator fault
    Unavailable,     // feature disabled (e.g. throttled by power-mgmt)
};

const char* to_string(Status s) noexcept;

template <typename T>
class Result {
public:
    // Success.
    static Result ok(T value) { return Result(Status::Ok, std::move(value)); }
    // Failure with no value.
    static Result fail(Status s) { return Result(s, std::nullopt); }

    bool is_ok() const noexcept { return status_ == Status::Ok; }
    explicit operator bool() const noexcept { return is_ok(); }

    Status status() const noexcept { return status_; }

    // Only valid when is_ok(); callers must check first.
    const T& value() const { return *value_; }
    T&       value()       { return *value_; }

    // Convenience: value if present, else the supplied fallback.
    T value_or(T fallback) const { return value_ ? *value_ : std::move(fallback); }

private:
    Result(Status s, std::optional<T> v) : status_(s), value_(std::move(v)) {}

    Status           status_;
    std::optional<T> value_;
};

}  // namespace echo
