#include "pwm.h"

#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "PWM";
static uint8_t s_current_level = 0;

static uint32_t percent_to_duty(uint8_t percent)
{
    if (percent == 0) return 0;
    if (percent >= 100) return 1023;
    return (uint32_t)percent * 1023 / 100;
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

    // ── Timer LEDC unique (mist + fan partagent le même timer) ──
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_PWM,
        .duty_resolution = PWM_RESOLUTION,
        .freq_hz         = PWM_FREQ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_cfg);

    // ── Canal LEDC brumisation ────────────────────────
    ledc_channel_config_t ch_mist = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_MIST,
        .timer_sel  = LEDC_TIMER_PWM,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = GPIO_PWM_MIST,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&ch_mist);

    // ── Canal LEDC ventilateur ────────────────────────
    ledc_channel_config_t ch_fan = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_FAN,
        .timer_sel  = LEDC_TIMER_PWM,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = GPIO_PWM_FAN,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&ch_fan);

    ESP_LOGI(TAG, "PWM initialisé — %dkHz, mist=GPIO%d fan=GPIO%d",
             PWM_FREQ / 1000, GPIO_PWM_MIST, GPIO_PWM_FAN);
}

void pwm_set_level(uint8_t percent)
{
    if (percent > 100) percent = 100;
    s_current_level = percent;

    uint32_t duty = percent_to_duty(percent);

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
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_MIST, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_MIST);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_FAN, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_FAN);
        gpio_set_level(GPIO_LED, 1);
        ESP_LOGI(TAG, "Niveau %d%% — duty=%lu", percent, duty);
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
