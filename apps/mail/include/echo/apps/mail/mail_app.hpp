// ECHO OS apps — mail (Gmail, voice-first).
//
// "read my unread emails", "reply saying I'll call later." The inbox is heard,
// not scrolled: the app summarizes unread messages aloud and composes replies by
// voice, showing only a mail glyph and a one-line subtitle (constraint #1).
//
// Mock mode simulates an inbox so the flow is testable with no Gmail credentials;
// real mode wraps the Gmail API behind this identical interface (constraint #4).
// Sending a reply is a real side effect — on device it is confirmed out loud
// before it goes; the host is where that confirmation gate lives.
#pragma once

#include "echo/apps/framework/app.hpp"
#include <memory>

namespace echo::apps::mail {
std::unique_ptr<IApp> make_mail_app(bool mock = true);
}  // namespace echo::apps::mail
