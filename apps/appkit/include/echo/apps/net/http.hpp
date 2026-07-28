// ECHO OS appkit — the HTTP boundary.
//
// Every real third-party backend (Spotify, Gmail, Custom Search, YouTube, page
// fetch) reaches the network through this one interface and nothing else. That is
// deliberate:
//   * it is the ONLY place a socket is opened, so the isolation rule (constraint
//     #2) is enforceable — an app never links libcurl directly;
//   * the real request-building and response-parsing in each backend can be unit
//     tested against a FakeHttpClient with canned responses, deterministically and
//     with no network, which is the only honest way to cover that logic in CI;
//   * the actual transport (libcurl) is compiled only behind ECHO_WITH_NETWORK, so
//     the stub/CI build stays zero-dependency and credential-free.
//
// A null client is a first-class, expected state: make_default_http_client()
// returns nullptr when the build has no network transport. Callers treat that the
// same as any other failure — degrade to a calm spoken line, never crash.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace echo::apps::net {

enum class Method : std::uint8_t { Get, Post, Put, Delete };

struct HttpRequest {
    Method                             method = Method::Get;
    std::string                        url;
    std::map<std::string, std::string> headers;  // e.g. {"Authorization", "Bearer .."}
    std::string                        body;      // form or JSON, per Content-Type
    int                                timeout_ms = 8000;  // never block the app forever
};

// A response, or a transport-level failure. `status == 0` means the request never
// completed (DNS failure, timeout, TLS error, no transport compiled). Any real
// HTTP status (including 4xx/5xx) is a completed request with status set.
struct HttpResponse {
    long                               status = 0;   // 0 == transport failure
    std::string                        body;
    std::map<std::string, std::string> headers;
    std::string                        error;        // human-readable transport error, if any

    bool ok() const noexcept { return status >= 200 && status < 300; }
    bool transport_failed() const noexcept { return status == 0; }
};

// The one network primitive. Implementations must never throw.
class IHttpClient {
public:
    virtual ~IHttpClient() = default;
    virtual HttpResponse send(const HttpRequest& request) = 0;
};

// The libcurl-backed client, or nullptr when ECHO_WITH_NETWORK is off. Returning
// nullptr is not an error to log loudly — it is how a stub build reports "no real
// backend here," and the app layer already knows to fall back gracefully.
std::unique_ptr<IHttpClient> make_default_http_client();

// True when this build was compiled with a real network transport. Lets apps and
// tests branch without catching a null client first.
bool network_available() noexcept;

}  // namespace echo::apps::net
