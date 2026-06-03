#include "pwm.h"
#include "sensors.h"
#include "zigbee.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_zigbee_core.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAIN";

#define GPIO_BUTTON   20    // D9 XIAO ESP32-C6

// ── Clignotement LED feedback bouton ─────────────────

static void blink_feedback(int count, int on_ms, int off_ms)
{
    for (int i = 0; i < count; i++) {
        led_set(true);
        vTaskDelay(pdMS_TO_TICKS(on_ms));
        led_set(false);
        vTaskDelay(pdMS_TO_TICKS(off_ms));
    }
}

// ── Tâche bouton ──────────────────────────────────────

static void button_task(void *pvParameters)
{
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << GPIO_BUTTON),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_conf);

    while (1) {
        if (gpio_get_level(GPIO_BUTTON) == 0) {
            // Appui détecté — anti-rebond
            vTaskDelay(pdMS_TO_TICKS(30));
            if (gpio_get_level(GPIO_BUTTON) != 0) {
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }

            TickType_t press_start = xTaskGetTickCount();
            bool long_done = false;

            while (gpio_get_level(GPIO_BUTTON) == 0) {
                uint32_t held = pdTICKS_TO_MS(xTaskGetTickCount() - press_start);

                if (!long_done && held >= 5000) {
                    long_done = true;
                    ESP_LOGI(TAG, "Long appui — reset usine Zigbee");
                    blink_feedback(5, 400, 400);   // 5x lent
                    esp_zb_factory_reset();         // efface NVS Zigbee + reboot
                }
                vTaskDelay(pdMS_TO_TICKS(50));
            }

            if (!long_done) {
                uint32_t held = pdTICKS_TO_MS(xTaskGetTickCount() - press_start);
                if (held < 3000) {
                    ESP_LOGI(TAG, "Court appui — appairage");
                    blink_feedback(3, 100, 100);   // 3x rapide
                    zigbee_start_pairing();
                }
                // 3-5 s : ignoré
            }

            // Anti-rebond relâchement
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ── Tâche surveillance capteurs + LED eau vide ────────

static void sensor_task(void *pvParameters)
{
    bool last_water = true;
    bool led_state  = false;
    TickType_t last_check = xTaskGetTickCount();

    while (1) {
        if ((xTaskGetTickCount() - last_check) >= pdMS_TO_TICKS(2000)) {
            last_check = xTaskGetTickCount();
            bool water_ok = sensors_water_ok();

            if (water_ok != last_water) {
                last_water = water_ok;
                zigbee_report_water(water_ok);

                if (!water_ok) {
                    ESP_LOGW(TAG, "Réservoir vide ! Arrêt brumisation.");
                    pwm_set_level(0);
                    zigbee_report_level(0);
                } else {
                    led_set(pwm_get_level() > 0);
                }
            }
        }

        // Clignotement rapide LED si eau vide + connecté
        if (!last_water && zigbee_is_connected()) {
            led_state = !led_state;
            led_set(led_state);
            vTaskDelay(pdMS_TO_TICKS(150));
        } else {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}

// ── Point d'entrée ────────────────────────────────────

void app_main(void)
{
    ESP_LOGI(TAG, "=== Levoit Classic 300S — Zigbee ESP32-C6 ===");
    ESP_LOGI(TAG, "Firmware v1.0.0");

    pwm_init();
    sensors_init();
    zigbee_init();

    xTaskCreate(sensor_task, "sensor_task", 2048, NULL, 4, NULL);
    xTaskCreate(button_task, "button_task", 2048, NULL, 3, NULL);

    ESP_LOGI(TAG, "Système démarré — en attente réseau Zigbee...");
}
