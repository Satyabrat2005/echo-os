// ECHO OS appkit — dependency-free unit tests.
//
// Same tiny-CHECK style as the rest of the tree, under CTest, no framework, no
// network. These cover the parts of Phase 5 that CAN be honestly verified in CI:
// the JSON reader, URL encoding, the .env/credential resolver, the readability
// extractor, and — most importantly — the confirm-before-action gate that guards
// every state-changing action (constraint #1). The real API calls can't run here;
// the logic that builds and guards them can.
#include "echo/apps/net/json.hpp"
#include "echo/apps/net/url.hpp"
#include "echo/apps/config/credentials.hpp"
#include "echo/apps/confirm/confirmation.hpp"
#include "echo/apps/text/readable.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

using namespace echo::apps;

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// --- JSON reader -------------------------------------------------------------
void test_json_basic() {
    auto j = net::Json::parse(R"({"a":1,"b":"two","c":true,"d":null,"e":[10,20,30]})");
    CHECK(j.is_object());
    CHECK(j["a"].as_number() == 1.0);
    CHECK(j["b"].as_string() == "two");
    CHECK(j["c"].as_bool() == true);
    CHECK(j["d"].is_null());
    CHECK(j["e"].is_array());
    CHECK(j["e"].size() == 3);
    CHECK(j["e"][1].as_number() == 20.0);
    // absent keys and out-of-range indices are Null, not a crash
    CHECK(j["missing"].is_null());
    CHECK(j["e"][99].is_null());
    CHECK(j.str_or("b", "x") == "two");
    CHECK(j.str_or("nope", "x") == "x");
}

void test_json_nested_and_escapes() {
    // Shape mirrors a Google Custom Search response.
    const char* doc = R"({
      "items": [
        {"title": "First \"quoted\" result", "link": "https://ex.com/a",
         "snippet": "line one\nline two"},
        {"title": "Second", "link": "https://ex.com/b", "snippet": "café"}
      ]
    })";
    auto j = net::Json::parse(doc);
    CHECK(j["items"].is_array());
    CHECK(j["items"].size() == 2);
    CHECK(j["items"][0]["title"].as_string() == "First \"quoted\" result");
    CHECK(contains(j["items"][0]["snippet"].as_string(), "\n"));
    CHECK(j["items"][1]["link"].as_string() == "https://ex.com/b");
    // é (é) decodes to 2-byte UTF-8
    CHECK(j["items"][1]["snippet"].as_string() == "caf\xC3\xA9");
}

void test_json_malformed_is_null() {
    CHECK(net::Json::parse("{ not json").is_null());
    CHECK(net::Json::parse("").is_null());
    CHECK(net::Json::parse("{\"a\":1} trailing").is_null());  // trailing garbage
    CHECK(net::Json::parse("[1,2,").is_null());               // unterminated
    // numbers and negative
    CHECK(net::Json::parse("-3.5e2").as_number() == -350.0);
}

// A hostile/corrupt payload of thousands of nested brackets must fail as Null, not
// overflow the stack. Guards the recursion-depth cap added to the parser.
void test_json_deep_nesting_is_null_not_crash() {
    std::string deep_arr(5000, '[');   // 5000 levels of unterminated array
    CHECK(net::Json::parse(deep_arr).is_null());
    std::string deep_obj;
    for (int i = 0; i < 5000; ++i) deep_obj += "{\"a\":";
    CHECK(net::Json::parse(deep_obj).is_null());
    // A modestly nested, WELL-FORMED document still parses fine (cap not too tight).
    std::string nested = "0";
    for (int i = 0; i < 50; ++i) nested = "[" + nested + "]";
    auto j = net::Json::parse(nested);
    CHECK(j.is_array());
}

// --- URL encoding ------------------------------------------------------------
void test_url_encode() {
    CHECK(net::url_encode("hello world") == "hello%20world");
    CHECK(net::url_encode("a+b&c=d") == "a%2Bb%26c%3Dd");
    CHECK(net::url_encode("safe-_.~") == "safe-_.~");
    std::map<std::string, std::string> p = {{"q", "kind of blue"}, {"key", "AB+C"}};
    // std::map is sorted: key then q
    CHECK(net::encode_query(p) == "key=AB%2BC&q=kind%20of%20blue");
}

// --- .env / credential resolver ---------------------------------------------
void test_dotenv_overlay() {
    const std::string path = "appkit_test.env";
    {
        std::ofstream out(path);
        out << "# a comment\n";
        out << "\n";
        out << "ECHO_SPOTIFY_CLIENT_ID=abc123\n";
        out << "export ECHO_SPOTIFY_CLIENT_SECRET=\"sh h\"\n";
        out << "ECHO_GOOGLE_SEARCH_KEY='k-e-y'\n";
        out << "ECHO_GOOGLE_SEARCH_CX=cx-000\n";
    }
    CHECK(config::load_dotenv(path));
    CHECK(config::get("ECHO_SPOTIFY_CLIENT_ID") == "abc123");
    CHECK(config::spotify_client_secret() == "sh h");     // quotes stripped
    CHECK(config::google_search_key() == "k-e-y");        // single-quotes stripped
    CHECK(config::spotify_configured());
    CHECK(config::search_configured());
    // youtube key falls back to the search key (same Cloud project)
    CHECK(config::youtube_api_key() == "k-e-y");
    CHECK(config::youtube_configured());
    // a service with nothing set is not configured
    CHECK(!config::gmail_configured());
    CHECK(config::load_dotenv("does-not-exist.env") == false);
    std::remove(path.c_str());
}

// --- Readability extraction --------------------------------------------------
void test_readable() {
    const std::string html =
        "<html><head><title>Hello &amp; World</title>"
        "<style>.x{color:red}</style></head>"
        "<body><script>evil()</script>"
        "<h1>Heading</h1><p>First&nbsp;paragraph with <b>bold</b> text.</p>"
        "</body></html>";
    auto r = text::extract(html);
    CHECK(r.title == "Hello & World");
    CHECK(contains(r.text, "Heading"));
    CHECK(contains(r.text, "First paragraph with bold text."));
    CHECK(!contains(r.text, "evil"));      // script dropped
    CHECK(!contains(r.text, "color:red")); // style dropped
    CHECK(!contains(r.text, "<"));         // no tags survive

    std::string s = text::summarize("one two three four five", 12);
    CHECK(contains(s, "…"));
    CHECK(s.size() <= 14);
    CHECK(text::summarize("short", 400) == "short");  // no ellipsis when it fits
}

// --- Confirmation gate (the safety-critical one) ----------------------------
void test_confirmation_gate_vocab() {
    using confirm::ConfirmationGate;
    CHECK(ConfirmationGate::is_affirmative("send"));
    CHECK(ConfirmationGate::is_affirmative("yes send it"));
    CHECK(ConfirmationGate::is_affirmative("Confirm"));
    CHECK(!ConfirmationGate::is_affirmative("sender wrote back"));  // word-bounded
    CHECK(!ConfirmationGate::is_affirmative("play some jazz"));

    CHECK(ConfirmationGate::is_negative("no"));
    CHECK(ConfirmationGate::is_negative("cancel that"));
    CHECK(ConfirmationGate::is_negative("wait, don't"));
    CHECK(!ConfirmationGate::is_negative("now playing"));  // "no" not inside "now"
}

void test_confirmation_gate_flow() {
    using namespace confirm;
    ConfirmationGate gate;

    // Nothing armed -> decide is a no-op.
    CHECK(gate.decide("send") == Decision::NoPending);
    CHECK(!gate.armed());

    // Arm a send, confirm it.
    gate.arm({"send_email", "reply to Sam: on my way", {{"to", "Sam"}}});
    CHECK(gate.armed());
    CHECK(gate.pending().kind == "send_email");
    CHECK(gate.pending().params.at("to") == "Sam");
    CHECK(gate.decide("yes, send it") == Decision::Confirmed);
    CHECK(!gate.armed());  // one-shot: cleared after decide

    // Arm again, explicitly decline.
    gate.arm({"send_email", "reply", {}});
    CHECK(gate.decide("no, cancel") == Decision::Declined);
    CHECK(!gate.armed());

    // Ambiguous / unrelated utterance must NOT send, and must clear the gate.
    gate.arm({"send_email", "reply", {}});
    CHECK(gate.decide("play some jazz") == Decision::Unrecognized);
    CHECK(!gate.armed());

    // "no, don't send" trips both lists -> must resolve to Declined, never Confirmed.
    gate.arm({"send_email", "reply", {}});
    CHECK(gate.decide("no don't send") == Decision::Declined);
    CHECK(!gate.armed());

    // A fresh arm supersedes a stale pending action.
    gate.arm({"send_email", "first", {}});
    gate.arm({"send_email", "second", {}});
    CHECK(gate.pending().summary == "second");
}

}  // namespace

int main() {
    test_json_basic();
    test_json_nested_and_escapes();
    test_json_malformed_is_null();
    test_json_deep_nesting_is_null_not_crash();
    test_url_encode();
    test_dotenv_overlay();
    test_readable();
    test_confirmation_gate_vocab();
    test_confirmation_gate_flow();

    if (g_failures == 0) {
        std::printf("all appkit tests passed\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "%d appkit check(s) failed\n", g_failures);
    return EXIT_FAILURE;
}
