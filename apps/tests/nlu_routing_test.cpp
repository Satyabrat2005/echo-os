// ECHO OS apps — NLU routing regression test (Phase 6, Part A).
//
// Guards the bug fixed in Phase 6: the keyword NLU greedily matched the browser's
// generic "read" (or "open") at the front of an utterance and misrouted mail
// commands like "read my unread email" to the browser instead of mail. The fix
// makes a domain-specific intent anywhere in the utterance win over a generic
// catch-all verb (see apps/app-framework/src/nlu.cpp).
//
// Same dependency-free CHECK harness as apps_smoke.cpp. This test registers the
// two apps whose vocabularies collide (browser owns "read"/"open"/"browse", mail
// owns "unread"/"email"/"inbox"/"message(s)") plus media as a neutral control,
// then routes real utterances end to end and asserts the app that handled each.
#include "echo/apps/framework/router.hpp"
#include "echo/apps/framework/voice_bridge.hpp"
#include "echo/apps/framework/nlu.hpp"
#include "echo/apps/hud/hud.hpp"

#include "echo/apps/browser/browser_app.hpp"
#include "echo/apps/mail/mail_app.hpp"
#include "echo/apps/media/media_app.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

using namespace echo;
using namespace echo::apps;

// Null output surfaces — this test only cares about which app claimed the intent.
class NullCompositor final : public hud::IHudCompositor {
public:
    Status initialize() override { return Status::Ok; }
    void present(std::string_view, const hud::HudFrame&) override {}
    void clear(std::string_view) override {}
    void shutdown() override {}
};
class NullBridge final : public IVoiceBridge {
public:
    bool speak(std::string_view, std::string_view, SpeechTone) override { return true; }
};

}  // namespace

int main() {
    auto browser = browser::make_browser_app(true);
    auto mail    = mail::make_mail_app(true);
    auto media   = media::make_media_app(true);
    CHECK(browser->initialize() == Status::Ok);
    CHECK(mail->initialize() == Status::Ok);
    CHECK(media->initialize() == Status::Ok);

    // Grant each app exactly what it declared, so routing reaches the app rather
    // than tripping the permission gate.
    PermissionModel pm;
    pm.grant(browser->metadata().id, browser->metadata().required_caps);
    pm.grant(mail->metadata().id,    mail->metadata().required_caps);
    pm.grant(media->metadata().id,   media->metadata().required_caps);

    NullCompositor hud;
    NullBridge     bridge;
    Router router(&hud, &bridge, &pm);
    // Registration order mirrors the host: browser (owner of "read") before mail.
    // A naive "first known token wins" parser would hand the mail utterances below
    // to browser precisely because browser is registered first.
    router.register_app(browser.get());
    router.register_app(mail.get());
    router.register_app(media.get());

    auto route_of = [&](const std::string& utter) {
        auto cmd = nlu::parse(utter, router.vocabulary());
        return router.route(cmd).app_id;
    };
    auto intent_of = [&](const std::string& utter) {
        return nlu::parse(utter, router.vocabulary()).intent;
    };

    // --- The exact phrase that broke: must reach mail, not browser --------------
    CHECK(route_of("read my unread email") == "mail");
    CHECK(intent_of("read my unread email") == "unread");

    // --- Other utterances that hit the same generic-verb ambiguity --------------
    // "read my messages": leading generic "read", specific "messages" wins -> mail.
    CHECK(route_of("read my messages") == "mail");
    // "check my mail": no generic verb, "mail" is the only intent -> mail.
    CHECK(route_of("check my mail") == "mail");
    // "open my inbox": leading generic "open", specific "inbox" wins -> mail.
    CHECK(route_of("open my inbox") == "mail");
    // "read me the unread messages": two specific mail words; earliest ("unread")
    // is chosen, and either way it stays in mail.
    CHECK(route_of("read me the unread messages") == "mail");

    // --- Genuine browser commands must still route to browser -------------------
    // No mail-specific word present -> the generic verb is the correct fallback.
    CHECK(route_of("read this page") == "browser");
    CHECK(intent_of("read this page") == "read");
    CHECK(route_of("open wikipedia") == "browser");

    // --- A neutral command is unaffected by the two-tier logic ------------------
    CHECK(route_of("play some jazz") == "media");
    CHECK(intent_of("play some jazz") == "play");

    browser->shutdown();
    mail->shutdown();
    media->shutdown();

    if (g_failures == 0) {
        std::printf("all NLU routing tests passed\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return EXIT_FAILURE;
}
