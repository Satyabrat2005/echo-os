#include "echo/apps/net/http.hpp"

namespace echo::apps::net {

#if defined(ECHO_WITH_NETWORK)

// --- Real transport: libcurl -------------------------------------------------
// Compiled only when the build opts in (-DECHO_WITH_NETWORK=ON, which links
// CURL::libcurl). This is the single file in the whole apps layer that touches a
// socket. It is intentionally thin: no retries, no redirects we don't ask for, a
// hard timeout, and TLS verification left ON (never disable peer verification for
// a device serving a vulnerable user).
}  // namespace echo::apps::net

#include <curl/curl.h>

namespace echo::apps::net {
namespace {

size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

size_t header_cb(char* buffer, size_t size, size_t nitems, void* userdata) {
    auto*  headers = static_cast<std::map<std::string, std::string>*>(userdata);
    size_t len     = size * nitems;
    std::string line(buffer, len);
    auto colon = line.find(':');
    if (colon != std::string::npos) {
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        // trim surrounding whitespace/CRLF
        auto trim = [](std::string& s) {
            const char* ws = " \t\r\n";
            s.erase(0, s.find_first_not_of(ws));
            auto end = s.find_last_not_of(ws);
            if (end != std::string::npos) s.erase(end + 1);
        };
        trim(key);
        trim(val);
        if (!key.empty()) (*headers)[key] = val;
    }
    return len;
}

const char* method_str(Method m) {
    switch (m) {
        case Method::Get:    return "GET";
        case Method::Post:   return "POST";
        case Method::Put:    return "PUT";
        case Method::Delete: return "DELETE";
    }
    return "GET";
}

class CurlClient final : public IHttpClient {
public:
    CurlClient() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlClient() override { curl_global_cleanup(); }

    HttpResponse send(const HttpRequest& req) override {
        HttpResponse resp;
        CURL* curl = curl_easy_init();
        if (!curl) {
            resp.error = "curl init failed";
            return resp;
        }

        curl_easy_setopt(curl, CURLOPT_URL, req.url.c_str());
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method_str(req.method));
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp.headers);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(req.timeout_ms));
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
        // TLS verification stays ON. Do not weaken this.
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "echo-os/5.0");

        if (req.method == Method::Post || req.method == Method::Put) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                             static_cast<long>(req.body.size()));
        }

        struct curl_slist* header_list = nullptr;
        for (const auto& [k, v] : req.headers) {
            std::string h = k + ": " + v;
            header_list   = curl_slist_append(header_list, h.c_str());
        }
        if (header_list) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);

        CURLcode rc = curl_easy_perform(curl);
        if (rc != CURLE_OK) {
            resp.status = 0;  // transport failure
            resp.error  = curl_easy_strerror(rc);
        } else {
            long code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
            resp.status = code;
        }

        if (header_list) curl_slist_free_all(header_list);
        curl_easy_cleanup(curl);
        return resp;
    }
};

}  // namespace

std::unique_ptr<IHttpClient> make_default_http_client() {
    return std::make_unique<CurlClient>();
}

bool network_available() noexcept { return true; }

#else  // !ECHO_WITH_NETWORK

// --- No transport compiled ---------------------------------------------------
// The stub/CI build. There is no socket here at all; real backends see a null
// client and report Unavailable, which the host degrades to a calm spoken line.
std::unique_ptr<IHttpClient> make_default_http_client() { return nullptr; }

bool network_available() noexcept { return false; }

#endif  // ECHO_WITH_NETWORK

}  // namespace echo::apps::net
