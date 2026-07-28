// ECHO OS appkit — URL helpers.
//
// Tiny, dependency-free percent-encoding + query-string assembly, used to build
// API request URLs and OAuth form bodies. Kept out of the backends so the encoding
// rules live in exactly one tested place — a mis-encoded scope or redirect_uri is a
// silent auth failure otherwise.
#pragma once

#include <map>
#include <string>

namespace echo::apps::net {

// Percent-encode per RFC 3986 unreserved set (A-Z a-z 0-9 - _ . ~); everything
// else becomes %XX. Suitable for both query values and application/x-www-form-
// urlencoded bodies (spaces become %20, which servers accept in form bodies).
std::string url_encode(const std::string& raw);

// Build "k1=v1&k2=v2" with every key and value encoded. Deterministic order
// (std::map is sorted) so tests can assert on the exact string.
std::string encode_query(const std::map<std::string, std::string>& params);

}  // namespace echo::apps::net
