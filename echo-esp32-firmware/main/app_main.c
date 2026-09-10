// ECHO sensor-node firmware — AI-Thinker ESP32-CAM
//
// LAPTOP-PROTOTYPE BUILD. Two things are disabled versus the
// touch-trigger build; both are marked DISABLED-FOR-PROTOTYPE and both
// must be restored before pilot testing:
//
//   1. The OLED. The panel is unplugged, but oled_init() probes 0x3C
//      and the CAMERA answers there — so the driver thinks a display
//      exists and every oled_status() writes SSD1306 commands into the
//      camera's registers. Harmful, not just useless.
//
//   2. The touch -> capture -> upload cycle. It holds the camera's only
//      frame buffer, blocks 5 s on audio, runs a WiFi scan that drops
//      the link, then blocks ~12 s on a dead TCP connect — so /snapshot
//      returns 500 for ~20 s per press. Presses are still logged.

#include "config.h"
#include "status_led.h"
#include "touch_trigger.h"
#include "wifi_manager.h"
#include "camera_capture.h"
#include "audio_io.h"
#include "network_client.h"
#include "oled.h"
#include "echo_web_capture.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *TAG = "app_main";

void app_main(void)
{
    ESP_LOGI(TAG, "ECHO sensor node booting — laptop-prototype build (OLED off, web capture on)");

    status_led_init();
    status_led_set(STATUS_LED_LISTENING);

    touch_trigger_init();

    if (!camera_capture_init()) {
        ESP_LOGE(TAG, "camera init failed — halting in error state");
        status_led_set(STATUS_LED_ERROR);
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (!audio_io_init()) {
        ESP_LOGE(TAG, "audio I2S init failed — halting in error state");
        status_led_set(STATUS_LED_ERROR);
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // DISABLED-FOR-PROTOTYPE (1): camera ACKs at 0x3C, so this would
    // "find" a display that isn't there and corrupt the camera.
    // oled_init();
    // oled_status("Connecting WiFi");
    ESP_LOGW(TAG, "OLED disabled in this build (panel unplugged; 0x3C belongs to the camera)");

    wifi_manager_init();
    ESP_LOGI(TAG, "waiting for WiFi connection...");
    wifi_manager_wait_connected();
    ESP_LOGI(TAG, "WiFi connected");

    echo_time_sync_init();
    start_capture_webserver();

    status_led_set(STATUS_LED_LISTENING);
    ESP_LOGI(TAG, "ready — laptop page can now pull /snapshot, /audio, /meta");

    while (1) {
        touch_trigger_wait_for_press();

        // DISABLED-FOR-PROTOTYPE (2): the full cycle made the board
        // unreachable for ~20 s per press. Logging keeps the sensor proven.
        ESP_LOGI(TAG, "touch press detected (capture cycle disabled in this build)");

        status_led_set(STATUS_LED_STREAMING);
        vTaskDelay(pdMS_TO_TICKS(300));
        status_led_set(STATUS_LED_LISTENING);

        // --- original cycle, restore before pilot testing ---
        // Three things to fix when restoring it:
        //   - network_client's server IP is hardcoded and stale
        //   - wifi_manager_rescan() drops the link while it scans
        //   - the frame buffer is held across the whole upload, which
        //     starves /snapshot; copy it out and release immediately
    }
}
