// ECHO OS apps — dependency-free smoke tests.
//
// Same spirit as the core's tests: a tiny CHECK macro under CTest, no framework.
// These guard the invariants that define the apps layer:
//   * voice command in -> correct app invoked -> mocked response out (media,
//     mail, telephony) — deliverable #4;
//   * two different apps render through the SAME HUD compositor using only the
//     shared primitives, neither carrying custom UI code — deliverable #2;
//   * the permission gate actually blocks an app that lacks a capability;
//   * an app really runs as an isolated OS process under the supervisor, answers
//     over IPC, and a crash-looping app is contained (restart backoff) without
//     touching anything else — constraint #2.
#include "echo/apps/framework/router.hpp"
#include "echo/apps/framework/supervisor.hpp"
#include "echo/apps/framework/voice_bridge.hpp"
#include "echo/apps/framework/nlu.hpp"
#include "echo/apps/hud/hud.hpp"

#include "echo/apps/media/media_app.hpp"
#include "echo/apps/mail/mail_app.hpp"
#include "echo/apps/telephony/telephony_app.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
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

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// A HUD compositor that records every frame it is asked to present, so a test can
// prove that different apps drew through the one shared surface.
class RecordingCompositor final : public hud::IHudCompositor {
public:
    struct Shown { std::string app_id; hud::HudFrame frame; };
    std::vector<Shown> shown;

    Status initialize() override { return Status::Ok; }
    void present(std::string_view app_id, const hud::HudFrame& f) override {
        shown.push_back({std::string(app_id), f});
    }
    void clear(std::string_view) override {}
    void shutdown() override {}
};

// A voice bridge that records spoken lines instead of driving audio.
class RecordingBridge final : public IVoiceBridge {
public:
    struct Said { std::string app_id; std::string text; SpeechTone tone; };
    std::vector<Said> said;

    bool speak(std::string_view app_id, std::string_view text, SpeechTone tone) override {
        said.push_back({std::string(app_id), std::string(text), tone});
        return true;
    }
};

// Grant every app exactly what its metadata declares (the host's default policy).
void grant_declared(PermissionModel& pm, IApp& app) {
    pm.grant(app.metadata().id, app.metadata().required_caps);
}

// --- Deliverable #4: command in -> right app -> mocked response out -----------
void test_routing_three_apps() {
    auto media     = media::make_media_app(true);
    auto mail      = mail::make_mail_app(true);
    auto telephony = telephony::make_telephony_app(true);
    CHECK(media->initialize() == Status::Ok);
    CHECK(mail->initialize() == Status::Ok);
    CHECK(telephony->initialize() == Status::Ok);

    PermissionModel pm;
    grant_declared(pm, *media);
    grant_declared(pm, *mail);
    grant_declared(pm, *telephony);

    RecordingCompositor hud;
    RecordingBridge     bridge;
    Router router(&hud, &bridge, &pm);
    router.register_app(media.get());
    router.register_app(mail.get());
    router.register_app(telephony.get());

    // media: "play some jazz" -> media, speaks now-playing.
    {
        auto cmd = nlu::parse("play some jazz", router.vocabulary());
        CHECK(cmd.intent == "play");
        auto r = router.route(cmd);
        CHECK(r.handled);
        CHECK(r.permitted);
        CHECK(r.app_id == "media");
        CHECK(contains(r.response.speech, "Now playing"));
        CHECK(r.response.hud.has_subtitle);  // now-playing band
    }
    // mail: "any unread messages" -> mail, summarizes the mock inbox.
    {
        auto cmd = nlu::parse("any unread messages", router.vocabulary());
        CHECK(cmd.intent == "unread");
        auto r = router.route(cmd);
        CHECK(r.handled);
        CHECK(r.app_id == "mail");
        CHECK(contains(r.response.speech, "unread"));
        CHECK(r.response.hud.icon.glyph == hud::Glyph::Mail);
    }
    // telephony: "call Sam" -> telephony, enters ringing.
    {
        auto cmd = nlu::parse("call Sam", router.vocabulary());
        CHECK(cmd.intent == "call");
        CHECK(cmd.slot("query") == "sam");
        auto r = router.route(cmd);
        CHECK(r.handled);
        CHECK(r.app_id == "telephony");
        CHECK(contains(r.response.speech, "Calling"));
        CHECK(r.response.hud.icon.glyph == hud::Glyph::Phone);
    }

    // Every routed command also reached the two shared surfaces.
    CHECK(hud.shown.size() == 3);
    CHECK(bridge.said.size() == 3);

    media->shutdown();
    mail->shutdown();
    telephony->shutdown();
}

// --- Deliverable #2: two apps, one HUD surface, only shared primitives --------
void test_shared_hud_surface() {
    auto media = media::make_media_app(true);
    auto mail  = mail::make_mail_app(true);
    media->initialize();
    mail->initialize();

    PermissionModel pm;
    grant_declared(pm, *media);
    grant_declared(pm, *mail);

    RecordingCompositor hud;  // the ONE surface both apps must use
    RecordingBridge     bridge;
    Router router(&hud, &bridge, &pm);
    router.register_app(media.get());
    router.register_app(mail.get());

    router.route(nlu::parse("play some jazz", router.vocabulary()));
    router.route(nlu::parse("inbox", router.vocabulary()));

    // Both apps drew through the same compositor instance...
    CHECK(hud.shown.size() == 2);
    CHECK(hud.shown[0].app_id == "media");
    CHECK(hud.shown[1].app_id == "mail");
    // ...and each frame is composed only of the shared primitives (nothing else
    // is expressible — the apps have no other drawing API).
    CHECK(hud.shown[0].frame.has_icon);
    CHECK(hud.shown[0].frame.icon.glyph == hud::Glyph::Play);
    CHECK(hud.shown[1].frame.has_icon);
    CHECK(hud.shown[1].frame.icon.glyph == hud::Glyph::Mail);

    media->shutdown();
    mail->shutdown();
}

// --- Constraint #1: no email sends without an explicit spoken confirmation ----
// This is the app-level companion to the appkit gate unit test: it drives the real
// MailApp (mock backend, no network) through the router and proves that a "reply"
// only sends after an explicit "send", and never on a decline or an ambiguous word.
void test_send_email_confirmation_gate() {
    auto mail = mail::make_mail_app(true);
    CHECK(mail->initialize() == Status::Ok);

    PermissionModel pm;
    grant_declared(pm, *mail);
    RecordingCompositor hud;
    RecordingBridge     bridge;
    Router router(&hud, &bridge, &pm);
    router.register_app(mail.get());

    auto say = [&](const std::string& utter) {
        return router.route(nlu::parse(utter, router.vocabulary())).response.speech;
    };

    // 1. "reply ..." stages the send but does NOT send it.
    {
        std::string r = say("reply saying I'll be there Thursday");
        CHECK(contains(r, "Ready to reply"));
        CHECK(contains(r, "send"));      // prompts for confirmation
        CHECK(!contains(r, "Sent"));     // nothing sent yet
    }
    // 2. An explicit "send" confirms — the ONE path that actually sends.
    {
        std::string r = say("send");
        CHECK(contains(r, "Sent"));
    }
    // 3. A decline never sends.
    {
        say("reply saying call me later");
        std::string r = say("no, cancel that");
        CHECK(contains(r, "won't send"));
        CHECK(!contains(r, "Sent"));
    }
    // 4. A non-confirmation utterance while armed fails safe: even a normally-valid
    //    mail command ("inbox") is treated as "not a yes" and sends nothing.
    {
        say("reply saying see you soon");
        std::string r = say("inbox");  // reaches mail (armed), but isn't a "send"
        CHECK(contains(r, "won't send"));
        CHECK(!contains(r, "Sent"));
    }
    // 5. After the one-shot gate clears, normal inbox summarizing works again.
    {
        std::string r = say("inbox");
        CHECK(contains(r, "unread"));
    }

    mail->shutdown();
}

// --- The permission gate blocks an under-privileged app ----------------------
void test_permission_gate() {
    auto media = media::make_media_app(true);
    media->initialize();

    PermissionModel pm;  // deliberately grant media NOTHING
    RecordingCompositor hud;
    RecordingBridge     bridge;
    Router router(&hud, &bridge, &pm);
    router.register_app(media.get());

    auto r = router.route(nlu::parse("play some jazz", router.vocabulary()));
    CHECK(r.handled);          // the app claims the intent...
    CHECK(!r.permitted);       // ...but is denied for lack of Network/Storage
    CHECK(r.response.status == Status::Unavailable);

    media->shutdown();
}

// --- Constraint #2: real process isolation + supervision ---------------------
#ifdef ECHO_APPS_BIN_DIR
std::string media_exe() {
    std::string p = std::string(ECHO_APPS_BIN_DIR) + "/echo-app-media";
#if defined(_WIN32)
    p += ".exe";
#endif
    return p;
}

void test_supervised_process_ipc() {
    Supervisor sup;
    AppSpec spec;
    spec.id         = "media";
    spec.executable = media_exe();
    spec.args       = {"--mock"};
    sup.add(spec);

    CHECK(sup.start("media") == Status::Ok);

    // Talk to the app across the process boundary and get its mocked response.
    VoiceCommand cmd;
    cmd.intent = "play";
    cmd.text   = "play some jazz";
    cmd.slots["query"] = "jazz";
    auto resp = sup.send_command("media", cmd);
    CHECK(resp.has_value());
    if (resp) CHECK(contains(resp->speech, "Now playing"));

    sup.stop("media");
    CHECK(!sup.status("media").running);
}

void test_crash_containment() {
    // An app that exits immediately (runs one selftest command and quits) stands
    // in for a crash-looping app. The supervisor must notice, restart it up to
    // the cap, then PARK it — never spin forever, never touch anything else.
    Supervisor sup;
    AppSpec spec;
    spec.id           = "flaky";
    spec.executable   = media_exe();
    spec.args         = {"--mock", "--selftest"};  // exits right away
    spec.max_restarts = 2;
    sup.add(spec);

    CHECK(sup.start("flaky") == Status::Ok);
    for (int i = 0; i < 50 && !sup.status("flaky").gave_up; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        sup.reap();
    }
    CHECK(sup.status("flaky").gave_up);
    CHECK(sup.status("flaky").restarts == spec.max_restarts);
}
#endif  // ECHO_APPS_BIN_DIR

}  // namespace

int main() {
    test_routing_three_apps();
    test_shared_hud_surface();
    test_send_email_confirmation_gate();
    test_permission_gate();
#ifdef ECHO_APPS_BIN_DIR
    test_supervised_process_ipc();
    test_crash_containment();
#endif

    if (g_failures == 0) {
        std::printf("all apps tests passed\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return EXIT_FAILURE;
}
