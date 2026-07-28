// ECHO OS appkit — base64, standard and URL-safe.
//
// Two callers need it: the Spotify token endpoint wants HTTP Basic auth
// (base64 of "id:secret"), and the Gmail send endpoint wants the raw RFC 2822
// message base64url-encoded. Dependency-free and unit-tested.
#pragma once

#include <string>

namespace echo::apps::net {

// Standard base64 (+/ alphabet, '=' padding).
std::string base64_encode(const std::string& data);

// URL-safe base64 (-_ alphabet). `pad=false` drops '=' padding, as Gmail's
// raw-message field expects.
std::string base64url_encode(const std::string& data, bool pad = false);

}  // namespace echo::apps::net
