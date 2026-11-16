#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>

#include "application.h"
#include "system_info.h"
#include "desktop_bridge.h"

#define TAG "main"

extern "C" void app_main(void)
{
    // Initialize the default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Shared NVS with the desktop app: never erase it here (would wipe desktop settings).
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(TAG, "NVS is not usable (%s); refusing to erase shared NVS", esp_err_to_name(ret));
    } else {
        ESP_ERROR_CHECK(ret);
    }

    // Import desktop-managed Wi-Fi credentials and device identity before the board starts
    ImportDesktopIdentity();

    // Launch the application
    auto& app = Application::GetInstance();
    app.Start();
    app.MainEventLoop();
}
