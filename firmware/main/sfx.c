#include "sfx.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "audio.h"

static const char *TAG = "sfx";

#define SFX_QUEUE_LEN 8

static QueueHandle_t s_sfx_queue = NULL;
static bool s_sfx_enabled = false;

static void play_tone_safe(int freq_hz, int duration_ms)
{
    audio_play_tone(freq_hz, duration_ms);
}

static void play_rest(int duration_ms)
{
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
}

static void sfx_task(void *arg)
{
    sfx_type_t sfx;
    while (1) {
        if (xQueueReceive(s_sfx_queue, &sfx, portMAX_DELAY) == pdTRUE) {
            switch (sfx) {
            case SFX_MOVE:
                play_tone_safe(520, 35);
                break;

            case SFX_CAPTURE:
                play_tone_safe(650, 40);
                play_tone_safe(950, 60);
                break;

            case SFX_INVALID:
                play_tone_safe(160, 90);
                break;

            case SFX_SELECT:
                play_tone_safe(750, 20);
                break;

            case SFX_START:
                play_tone_safe(523, 60);
                play_rest(20);
                play_tone_safe(784, 120);
                break;

            case SFX_WIN:
                play_tone_safe(523, 100);  // C5
                play_tone_safe(659, 100);  // E5
                play_tone_safe(784, 100);  // G5
                play_tone_safe(1046, 250); // C6
                break;

            case SFX_LOSE:
                play_tone_safe(392, 120);  // G4
                play_tone_safe(311, 120);  // Eb4
                play_tone_safe(261, 250);  // C4
                break;

            case SFX_PANIC_TICK:
                play_tone_safe(1000, 25);
                break;

            default:
                break;
            }
        }
    }
}

void sfx_init(void)
{
    if (s_sfx_queue != NULL) {
        return;
    }
    s_sfx_queue = xQueueCreate(SFX_QUEUE_LEN, sizeof(sfx_type_t));
    if (s_sfx_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create SFX queue");
        return;
    }

    if (xTaskCreate(sfx_task, "sfx_task", 3072, NULL, 4, NULL) == pdPASS) {
        s_sfx_enabled = true;
        ESP_LOGI(TAG, "SFX system initialized");
    } else {
        ESP_LOGE(TAG, "Failed to create SFX task");
    }
}

void sfx_play(sfx_type_t sfx)
{
    if (!s_sfx_enabled || s_sfx_queue == NULL || sfx == SFX_NONE) {
        return;
    }
    xQueueSend(s_sfx_queue, &sfx, 0);
}
