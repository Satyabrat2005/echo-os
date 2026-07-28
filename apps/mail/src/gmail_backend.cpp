// ECHO OS mail — real Gmail API backend.
//
// OAuth: Authorization Code with a loopback redirect and the *narrowest* scopes
// that do the job — gmail.readonly + gmail.send, nothing broader (no modify, no
// delete, no full mailbox). The one-time consent is a documented setup step (see
// README); this backend implements silent refresh, unread listing, and send.
//
// Send is gated upstream: the MailApp only calls send() after its ConfirmationGate
// returns Confirmed. This backend never sends on its own initiative, and it never
// logs a message body to disk (constraint #4).
#include "echo/apps/mail/mail_backend.hpp"

#include "echo/apps/config/credentials.hpp"
#include "echo/apps/config/token_store.hpp"
#include "echo/apps/net/base64.hpp"
#include "echo/apps/net/http.hpp"
#include "echo/apps/net/json.hpp"
#include "echo/apps/net/url.hpp"
#include "echo/log.hpp"

#include <chrono>

namespace echo::apps::mail {

using echo::Status;
namespace net = echo::apps::net;
namespace cfg = echo::apps::config;

// --- Pure helpers (unit-tested) ----------------------------------------------

std::string gmail_refresh_body(const std::string& client_id,
                               const std::string& client_secret,
                               const std::string& refresh_token) {
    std::map<std::string, std::string> f = {
        {"client_id", client_id},
        {"client_secret", client_secret},
        {"refresh_token", refresh_token},
        {"grant_type", "refresh_token"},
    };
    return net::encode_query(f);
}

// Strip CR/LF (and other control chars) from a value destined for a message
// header. Without this, a "To"/"Subject" carrying an embedded newline could inject
// extra headers (e.g. a hidden Bcc) into the outgoing mail — header injection. The
// body is exempt: it lives after the blank-line separator and cannot forge headers.
static std::string sanitize_header_value(const std::string& v) {
    std::string out;
    out.reserve(v.size());
    for (char c : v) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc == '\r' || uc == '\n') continue;           // the injection vector
        if (uc < 0x20 && uc != '\t') continue;            // other control chars
        out.push_back(c);
    }
    return out;
}

std::string gmail_build_raw_message(const std::string& to, const std::string& subject,
                                    const std::string& body) {
    // CRLF line endings per RFC 2822; headers, blank line, then body. Header fields
    // are sanitized so a spoken recipient/subject can never inject extra headers.
    return "To: " + sanitize_header_value(to) + "\r\n" +
           "Subject: " + sanitize_header_value(subject) + "\r\n" +
           "Content-Type: text/plain; charset=UTF-8\r\n" +
           "\r\n" + body;
}

std::string gmail_send_json(const std::string& raw_base64url, const std::string& thread_id) {
    std::string j = "{\"raw\":\"" + raw_base64url + "\"";
    if (!thread_id.empty()) j += ",\"threadId\":\"" + thread_id + "\"";
    j += "}";
    return j;
}

int gmail_parse_unread_count(const std::string& list_json) {
    auto j = net::Json::parse(list_json);
    if (!j.is_object()) return 0;
    return static_cast<int>(j["resultSizeEstimate"].as_number(0));
}

std::vector<std::pair<std::string, std::string>> gmail_parse_message_ids(
    const std::string& list_json) {
    std::vector<std::pair<std::string, std::string>> out;
    auto j = net::Json::parse(list_json);
    const auto& msgs = j["messages"];
    if (!msgs.is_array()) return out;
    for (std::size_t i = 0; i < msgs.size(); ++i)
        out.emplace_back(msgs[i].str_or("id"), msgs[i].str_or("threadId"));
    return out;
}

MailSummary gmail_parse_headers(const std::string& message_json) {
    MailSummary s;
    auto j = net::Json::parse(message_json);
    s.id        = j.str_or("id");
    s.thread_id = j.str_or("threadId");
    const auto& headers = j["payload"]["headers"];
    if (headers.is_array()) {
        for (std::size_t i = 0; i < headers.size(); ++i) {
            std::string name  = headers[i].str_or("name");
            std::string value = headers[i].str_or("value");
            if (name == "From") s.from = value;
            else if (name == "Subject") s.subject = value;
        }
    }
    return s;
}

namespace {

std::int64_t now_unix() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// --- Mock backend ------------------------------------------------------------
class MockMailBackend final : public IMailBackend {
public:
    Status initialize() override { return Status::Ok; }
    Inbox list_unread() override {
        Inbox in;
        in.unread_count = 3;
        in.top = {
            {"Dr. Alvarez", "Appointment reminder for Thursday", "m1", "t1"},
            {"Sam", "Lunch this weekend?", "m2", "t2"},
            {"Pharmacy", "Your prescription is ready", "m3", "t3"},
        };
        return in;
    }
    Status send(const std::string& to, const std::string&, const std::string&,
                const std::string&) override {
        // Mock never touches the network; it reports success so the confirmation
        // flow (and its smoke test) exercises end-to-end without credentials.
        log_info("mail", (std::string("[mock] would send to ") + to).c_str());
        return Status::Ok;
    }
};

// --- Real Gmail backend ------------------------------------------------------
class GmailBackend final : public IMailBackend {
public:
    explicit GmailBackend(net::IHttpClient* http) : http_(http) {}

    Status initialize() override {
        if (!http_) {
            log_warn("mail", "no network transport compiled; Gmail unavailable");
            return Status::Unavailable;
        }
        if (!cfg::gmail_configured()) {
            log_warn("mail", "Gmail credentials not set (.env); use --mock");
            return Status::Unavailable;
        }
        tokens_ = cfg::load_tokens("gmail");
        if (!tokens_.has_refresh()) {
            log_warn("mail",
                     "no Gmail refresh token cached; run the one-time authorize "
                     "step (see README). Real mail unavailable until then.");
            return Status::Unavailable;
        }
        return refresh_if_needed();
    }

    Inbox list_unread() override {
        Inbox in;
        if (Status s = refresh_if_needed(); s != Status::Ok) { in.status = s; return in; }

        net::HttpRequest req;
        req.method = net::Method::Get;
        req.url = "https://gmail.googleapis.com/gmail/v1/users/me/messages?"
                  "q=" + net::url_encode("is:unread") + "&maxResults=5";
        req.headers["Authorization"] = "Bearer " + tokens_.access_token;
        net::HttpResponse resp = http_->send(req);
        if (resp.transport_failed()) { in.status = Status::HardwareError; return in; }
        if (!resp.ok())              { in.status = Status::Unavailable;  return in; }

        in.unread_count = gmail_parse_unread_count(resp.body);
        auto ids = gmail_parse_message_ids(resp.body);
        for (const auto& [id, thread] : ids) {
            (void)thread;
            MailSummary s = fetch_headers(id);
            if (!s.id.empty()) in.top.push_back(s);
            if (in.top.size() >= 3) break;
        }
        return in;
    }

    Status send(const std::string& to, const std::string& subject,
                const std::string& body, const std::string& thread_id) override {
        if (Status s = refresh_if_needed(); s != Status::Ok) return s;

        std::string raw = net::base64url_encode(
            gmail_build_raw_message(to, subject, body), /*pad=*/false);

        net::HttpRequest req;
        req.method = net::Method::Post;
        req.url = "https://gmail.googleapis.com/gmail/v1/users/me/messages/send";
        req.headers["Authorization"]  = "Bearer " + tokens_.access_token;
        req.headers["Content-Type"]   = "application/json";
        req.body = gmail_send_json(raw, thread_id);

        net::HttpResponse resp = http_->send(req);
        if (resp.transport_failed()) return Status::HardwareError;
        return resp.ok() ? Status::Ok : Status::Unavailable;
    }

private:
    MailSummary fetch_headers(const std::string& id) {
        net::HttpRequest req;
        req.method = net::Method::Get;
        req.url = "https://gmail.googleapis.com/gmail/v1/users/me/messages/" + id +
                  "?format=metadata&metadataHeaders=From&metadataHeaders=Subject";
        req.headers["Authorization"] = "Bearer " + tokens_.access_token;
        net::HttpResponse resp = http_->send(req);
        if (!resp.ok()) return {};
        return gmail_parse_headers(resp.body);
    }

    Status refresh_if_needed() {
        if (!tokens_.needs_refresh(now_unix())) return Status::Ok;
        if (!tokens_.has_refresh()) return Status::Unavailable;

        net::HttpRequest req;
        req.method = net::Method::Post;
        req.url    = "https://oauth2.googleapis.com/token";
        req.headers["Content-Type"] = "application/x-www-form-urlencoded";
        req.body = gmail_refresh_body(cfg::gmail_client_id(), cfg::gmail_client_secret(),
                                      tokens_.refresh_token);

        net::HttpResponse resp = http_->send(req);
        if (resp.transport_failed()) return Status::HardwareError;
        if (!resp.ok()) return Status::Unavailable;

        auto j = net::Json::parse(resp.body);
        std::string at = j.str_or("access_token");
        if (at.empty()) return Status::Unavailable;
        tokens_.access_token    = at;
        tokens_.expires_at_unix = now_unix() +
            static_cast<std::int64_t>(j["expires_in"].as_number(3600));
        cfg::save_tokens("gmail", tokens_);
        return Status::Ok;
    }

    net::IHttpClient* http_ = nullptr;
    cfg::OAuthTokens  tokens_;
};

}  // namespace

std::unique_ptr<IMailBackend> make_mock_mail_backend() {
    return std::make_unique<MockMailBackend>();
}
std::unique_ptr<IMailBackend> make_gmail_backend(net::IHttpClient* http) {
    return std::make_unique<GmailBackend>(http);
}

}  // namespace echo::apps::mail
