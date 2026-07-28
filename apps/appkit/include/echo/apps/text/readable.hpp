// ECHO OS appkit — headless readability extraction.
//
// The Phase 2 approach, made real: a fetched page is never dumped raw to the user.
// We strip it to readable text on-device and speak a short summary. This is the
// same discipline as before (constraint #4) — no unnecessary page markup, script,
// or tracking cruft is retained or logged; only the human-readable prose the user
// asked to hear.
//
// This is intentionally a lightweight extractor, not a full DOM/Readability port:
// drop <script>/<style>/<head> noise, unwrap tags, decode the common entities,
// collapse whitespace. Dependency-free and unit-tested.
#pragma once

#include <string>

namespace echo::apps::text {

struct Readable {
    std::string title;  // from <title>, trimmed
    std::string text;   // readable body text, whitespace-collapsed
};

// Extract title + readable text from an HTML document.
Readable extract(const std::string& html);

// Trim `text` to at most `max_chars` on a word boundary, appending "…" if cut.
// Used to keep a spoken summary short enough to be heard, not endured.
std::string summarize(const std::string& text, std::size_t max_chars = 400);

}  // namespace echo::apps::text
