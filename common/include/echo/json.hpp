// ECHO OS — a minimal, dependency-free JSON reader.
//
// Just enough to pull fields out of a structured payload: read-only, no
// serialization, no exceptions. Malformed input yields a Null value rather than
// throwing — a caller that gets garbage back degrades like any other failure.
//
// Why hand-rolled instead of a vendored library: the stub/CI build is
// zero-dependency by policy, and this parser (with its own tests and a fuzz
// harness) is small enough to audit at a glance.
//
// HISTORY / LOCATION (Phase 22): this parser was written in Phase 6 for the
// appkit HTTP backends and lived at apps/appkit/include/echo/apps/net/json.hpp.
// Phase 22 needed the SAME hardened decoder for the caregiver inbound path in
// companion-sync, but apps/ builds AFTER (and optionally without) the core
// modules, so a core module cannot link echo::appkit. Rather than grow a second
// parser — exactly the "no parallel mechanisms" the phase forbids — the parser
// MOVED here, into echo::common, and echo/apps/net/json.hpp became a two-line
// alias so every Phase 5-8 call site (and the fuzz harness) still compiles
// unchanged. There is one decoder in this repo, and both the network side and
// the caregiver side are hardened by it.
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace echo {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;

    // Parse a document. On any error, returns a Null value (is_null() == true).
    static Json parse(const std::string& text);

    Type type() const noexcept { return type_; }
    bool is_null()   const noexcept { return type_ == Type::Null; }
    bool is_bool()   const noexcept { return type_ == Type::Bool; }
    bool is_number() const noexcept { return type_ == Type::Number; }
    bool is_string() const noexcept { return type_ == Type::String; }
    bool is_array()  const noexcept { return type_ == Type::Array; }
    bool is_object() const noexcept { return type_ == Type::Object; }

    bool               as_bool(bool def = false) const noexcept;
    double             as_number(double def = 0.0) const noexcept;
    const std::string& as_string() const noexcept { return string_; }

    // Object member by key; returns a shared Null value if absent or not an object.
    const Json& operator[](const std::string& key) const noexcept;
    bool        contains(const std::string& key) const noexcept;

    // Array element by index; returns a shared Null value if out of range or not an
    // array. size() is the element/member count for arrays/objects, else 0.
    const Json& operator[](std::size_t index) const noexcept;
    std::size_t size() const noexcept;

    // Convenience: string field of an object with a fallback. Handy for the common
    // `obj["snippet"].str_or("")` extraction pattern.
    std::string str_or(const std::string& key, const std::string& def = {}) const;

private:
    // A member's value is stored indirectly (as the second of a pair inside a
    // vector) so this class can contain itself while still incomplete here.
    using Member = std::pair<std::string, Json>;

    Type                     type_ = Type::Null;
    bool                     bool_ = false;
    double                   number_ = 0.0;
    std::string              string_;  // holds string value or (unused otherwise)
    std::vector<Json>        array_;
    std::vector<Member>      object_;

    friend class JsonParser;
};

}  // namespace echo
