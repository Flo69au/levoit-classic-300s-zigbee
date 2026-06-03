#include "sensors.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "SENSORS";

void sensors_init(void)
{
    gpio_config_t ks_conf = {
        .pin_bit_mask = (1ULL << GPIO_KS),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,   // pull-up interne
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&ks_conf);

    ESP_LOGI(TAG, "Capteur niveau eau initialisé sur GPIO%d", GPIO_KS);
}

bool sensors_water_ok(void)
{
    // KS = HIGH (pull-up) → flotteur ouvert → réservoir VIDE
    // KS = LOW             → flotteur fermé → réservoir PLEIN
    // Inverser si nécessaire selon mesure réelle
    int level = gpio_get_level(GPIO_KS);
    bool water_ok = (level == 0);   // KS=LOW → flotteur fermé → eau OK
    ESP_LOGI(TAG, "KS=%d → eau %s", level, water_ok ? "OK" : "VIDE");
    return water_ok;
}
