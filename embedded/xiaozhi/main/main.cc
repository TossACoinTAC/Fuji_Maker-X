#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>
#include <nvs_flash.h>

#include "application.h"
#include "board.h"
#if CONFIG_BOARD_PROBE_ONLY
#include "board_probe.h"
#endif

#define TAG "main"

namespace {

[[maybe_unused]] void InitializeNvs() {
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(result);
}

[[maybe_unused]] void IdleForever() {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

}  // namespace

extern "C" void app_main(void) {
#if CONFIG_BOARD_PROBE_ONLY
    // Do not construct Board here: its base constructor persists a UUID in NVS.
    RunBoardProbe();
    ESP_LOGI(TAG, "Board probe complete; NVS, peripherals and network remain disabled");
    IdleForever();
#elif CONFIG_BOARD_DISPLAY_TEST_ONLY
    InitializeNvs();
    Board::GetInstance();
    ESP_LOGI(TAG, "Display test complete; audio, button and network remain disabled");
    IdleForever();
#elif CONFIG_BOARD_OLED_TEST_ONLY
    InitializeNvs();
    Board::GetInstance();
    ESP_LOGI(TAG, "OLED test complete; audio, button and network remain disabled");
    IdleForever();
#elif CONFIG_BOARD_MIC_TEST_ONLY
    InitializeNvs();
    Board::GetInstance();
    ESP_LOGI(TAG, "Microphone test complete; speaker, button and network remain disabled");
    IdleForever();
#elif CONFIG_BOARD_SPEAKER_TEST_ONLY
    InitializeNvs();
    Board::GetInstance();
    ESP_LOGI(TAG, "Speaker test complete; microphone, button and network remain disabled");
    IdleForever();
#else
    InitializeNvs();

    // Initialize and run the application
    auto& app = Application::GetInstance();
    app.Initialize();
    app.Run();  // This function runs the main event loop and never returns
#endif
}
