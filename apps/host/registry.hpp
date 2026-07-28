// ECHO OS apps — the app registry.
//
// The single list of first-party apps the host and tests know about. Kept in one
// place so adding an app is one line here, not edits scattered across the host
// and the test harness. Each entry pairs an app's stable id with its in-process
// factory; the host reads metadata from a freshly-constructed instance rather
// than duplicating each app's intents/permissions here.
#pragma once

#include "echo/apps/framework/app.hpp"

#include "echo/apps/media/media_app.hpp"
#include "echo/apps/browser/browser_app.hpp"
#include "echo/apps/search/search_app.hpp"
#include "echo/apps/video/video_app.hpp"
#include "echo/apps/mail/mail_app.hpp"
#include "echo/apps/telephony/telephony_app.hpp"
#include "echo/apps/camera/camera_app.hpp"
#include "echo/apps/gallery/gallery_app.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace echo::apps::registry {

struct Entry {
    const char*                                    id;
    std::function<std::unique_ptr<IApp>(bool mock)> make;
};

// The eight core apps, in a stable order.
inline std::vector<Entry> all() {
    return {
        {"media",     media::make_media_app},
        {"browser",   browser::make_browser_app},
        {"search",    search::make_search_app},
        {"video",     video::make_video_app},
        {"mail",      mail::make_mail_app},
        {"telephony", telephony::make_telephony_app},
        {"camera",    camera::make_camera_app},
        {"gallery",   gallery::make_gallery_app},
    };
}

}  // namespace echo::apps::registry
