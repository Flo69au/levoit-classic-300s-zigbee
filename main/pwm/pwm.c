#include "pwm.h"

#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "PWM";
static uint8_t s_current_level = 0;

// Ventilateur : quadratique — compense la réponse non-linéaire du SF5020SL
static uint32_t percent_to_duty(uint8_t percent)
{
    if (percent == 0)   return 0;
    if (percent >= 100) return PWM_MAX_DUTY_FAN;
    uint32_t range = PWM_MAX_DUTY_FAN - PWM_MIN_DUTY_FAN;
    return PWM_MIN_DUTY_FAN + (uint32_t)percent * percent * range / 10000;
}

// Brumisation : linéaire + seuil minimum AW2205 (8-bit, max=255)
static uint32_t percent_to_duty_mist(uint8_t percent)
{
    if (percent == 0)   return 0;
    if (percent >= 100) return 255;
    return PWM_MIN_DUTY_MIST + (uint32_t)percent * (255 - PWM_MIN_DUTY_MIST) / 100;
}

void pwm_init(void)
{
    // ── GPIO CS (enable transducteur) ─────────────────
    gpio_config_t cs_conf = {
        .pin_bit_mask = (1ULL << GPIO_CS),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cs_conf);
    gpio_set_level(GPIO_CS, 0);

    // ── GPIO LED ──────────────────────────────────────
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << GPIO_LED),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_conf);
    gpio_set_level(GPIO_LED, 0);

    // ── Timer LEDC brumisation AW2205 (100 kHz, 8-bit, 100 niveaux) ─
    ledc_timer_config_t timer_mist = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_MIST,
        .duty_resolution = PWM_RESOLUTION_MIST,
        .freq_hz         = PWM_FREQ_MIST,
        .clk_cfg         = LEDC_USE_XTAL_CLK,
    };
    ledc_timer_config(&timer_mist);

    // ── Canal LEDC brumisation ────────────────────────
    ledc_channel_config_t ch_mist = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_MIST,
        .timer_sel  = LEDC_TIMER_MIST,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = GPIO_PWM_MIST,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&ch_mist);

    // ── Timer LEDC ventilateur SF5020SL (25 kHz, 10-bit / XTAL 40 MHz) ──────
    ledc_timer_config_t timer_fan = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_FAN,
        .duty_resolution = PWM_RESOLUTION_FAN,
        .freq_hz         = PWM_FREQ_FAN,
        .clk_cfg         = LEDC_USE_XTAL_CLK,
    };
    ledc_timer_config(&timer_fan);

    // ── Canal LEDC ventilateur ────────────────────────
    ledc_channel_config_t ch_fan = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_FAN,
        .timer_sel  = LEDC_TIMER_FAN,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = GPIO_PWM_FAN,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&ch_fan);

    ESP_LOGI(TAG, "PWM mist=%dkHz (GPIO%d)  fan=%dkHz (GPIO%d)",
             PWM_FREQ_MIST / 1000, GPIO_PWM_MIST,
             PWM_FREQ_FAN  / 1000, GPIO_PWM_FAN);
}

void pwm_set_level(uint8_t percent)
{
    if (percent > 100) percent = 100;
    s_current_level = percent;

    uint32_t duty_mist = percent_to_duty_mist(percent);
    uint32_t duty_fan  = percent_to_duty(percent);

    if (percent == 0) {
        gpio_set_level(GPIO_CS, 0);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_MIST, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_MIST);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_FAN, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_FAN);
        gpio_set_level(GPIO_LED, 0);
        ESP_LOGI(TAG, "Arrêt — CS=LOW, PWM=0%%");
    } else {
        gpio_set_level(GPIO_CS, 1);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_MIST, duty_mist);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_MIST);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_FAN, duty_fan);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_FAN);
        gpio_set_level(GPIO_LED, 1);
        ESP_LOGI(TAG, "Niveau %d%% — mist duty=%lu  fan duty=%lu", percent, duty_mist, duty_fan);
    }
}

uint8_t pwm_get_level(void)
{
    return s_current_level;
}

void led_set(bool on)
{
    gpio_set_level(GPIO_LED, on ? 1 : 0);
}
