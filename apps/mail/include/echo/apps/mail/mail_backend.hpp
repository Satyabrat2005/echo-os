// ECHO OS mail — the backend seam.
//
// The MailApp owns the voice interface AND the confirm-before-send gate; the
// backend only reads the inbox and (once confirmed) sends. In --mock the inbox is
// canned and send is a no-op that reports what WOULD be sent; in --real the Gmail
// API drives the same structs. Read-only by default: send() is only ever reached
// after the app's ConfirmationGate returns Confirmed (constraint #1).
#pragma once

#include "echo/result.hpp"

#include <memory>
#include <string>
#include <vector>

namespace echo::apps::net { class IHttpClient; }

namespace echo::apps::mail {

struct MailSummary {
    std::string from;
    std::string subject;
    std::string id;         // Gmail message id
    std::string thread_id;  // for threading a reply
};

struct Inbox {
    echo::Status             status = echo::Status::Ok;
    int                      unread_count = 0;
    std::vector<MailSummary> top;  // a few most-recent unread, newest first
};

class IMailBackend {
public:
    virtual ~IMailBackend() = default;
    virtual echo::Status initialize() = 0;
    virtual Inbox        list_unread() = 0;
    // Send a message. `thread_id` empty for a fresh mail, set to reply in-thread.
    virtual echo::Status send(const std::string& to, const std::string& subject,
                              const std::string& body, const std::string& thread_id) = 0;
};

std::unique_ptr<IMailBackend> make_mock_mail_backend();
std::unique_ptr<IMailBackend> make_gmail_backend(net::IHttpClient* http);

// --- Pure helpers (unit-tested) ----------------------------------------------
// Google token-refresh form body (client id/secret go in the body, not Basic auth).
std::string gmail_refresh_body(const std::string& client_id,
                               const std::string& client_secret,
                               const std::string& refresh_token);

// Assemble a minimal RFC 2822 message (To/Subject/blank line/body).
std::string gmail_build_raw_message(const std::string& to, const std::string& subject,
                                    const std::string& body);

// The JSON body for users/me/messages/send: {"raw": "...", "threadId": "..."}.
std::string gmail_send_json(const std::string& raw_base64url, const std::string& thread_id);

// Count of unread from a users/me/messages list response (resultSizeEstimate).
int gmail_parse_unread_count(const std::string& list_json);
// Message ids from that same list response.
std::vector<std::pair<std::string, std::string>> gmail_parse_message_ids(
    const std::string& list_json);  // (id, threadId) pairs

// From/Subject out of a format=metadata message response.
MailSummary gmail_parse_headers(const std::string& message_json);

}  // namespace echo::apps::mail
