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
#if CONFIG_BOARD_PROBE_ONLY
#include "boards/fuji-devkit-s3/board_probe.h"
#endif

#define TAG "main"

namespace {

void InitializeNvs() {
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(result);
}

void IdleForever() {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

}  // namespace

extern "C" void app_main(void)
{
#if CONFIG_BOARD_PROBE_ONLY
    // Do not construct Board here: its base constructor persists a UUID in NVS.
    RunFujiDevKitS3BoardProbe();
    ESP_LOGI(TAG, "Board probe complete; NVS, peripherals and network remain disabled");
    IdleForever();
#elif CONFIG_BOARD_DISPLAY_TEST_ONLY
    InitializeNvs();
    Board::GetInstance();
    ESP_LOGI(TAG, "Display test complete; audio, button and network remain disabled");
    IdleForever();
#else
    InitializeNvs();

    // Initialize and run the application
    auto& app = Application::GetInstance();
    app.Initialize();
    app.Run();  // This function runs the main event loop and never returns
#endif
}
