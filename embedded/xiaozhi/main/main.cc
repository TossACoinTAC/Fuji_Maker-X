#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "application.h"
#include "board.h"

#define TAG "main"

extern "C" void app_main(void)
{
#if CONFIG_BOARD_PROBE_ONLY
    // Constructing the selected board prints the probe report. Keep this path
    // read-only: do not mount NVS or initialize display, audio, buttons or Wi-Fi.
    Board::GetInstance();
    ESP_LOGI(TAG, "Board probe complete; NVS, peripherals and network remain disabled");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#else
    // Initialize NVS flash for WiFi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize and run the application
    auto& app = Application::GetInstance();
    app.Initialize();
    app.Run();  // This function runs the main event loop and never returns
#endif
}
