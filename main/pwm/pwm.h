#pragma once

#include <stdint.h>
#include <stdbool.h>

// ── GPIO ──────────────────────────────────────────────
#define GPIO_CS          22   // Chip Select AW2205 (enable) — D4
#define GPIO_PWM_MIST    23   // PWM 156,25 kHz → fréquence de résonance piézo AW2205 (36V) — D5
#define GPIO_PWM_FAN     17   // PWM ventilateur (via LR7843) — D7
#define GPIO_LED         19   // LED état — D8

// ── PWM ───────────────────────────────────────────────
#define PWM_FREQ_MIST        156250             // AW2205 piézo — résonance confirmée 156,25 kHz (40 MHz / 256)
#define PWM_FREQ_FAN         25000              // SF5020SL 12V 0.10A — standard brushless 25 kHz
#define PWM_RESOLUTION_MIST  LEDC_TIMER_8_BIT  // 8-bit (0–255) — compatible 156 kHz / XTAL 40 MHz
#define PWM_RESOLUTION_FAN   LEDC_TIMER_10_BIT // 10-bit (0–1023) — max valide à 25 kHz / XTAL 40 MHz
#define PWM_MIN_DUTY_MIST    110                // AW2205 seuil minimum
#define PWM_MIN_DUTY_FAN     213               // SF5020SL seuil démarrage (~21% de 1023) — à calibrer
#define PWM_MAX_DUTY_FAN     1023              // SF5020SL duty maximum (10-bit)

// ── Canaux LEDC ───────────────────────────────────────
#define LEDC_CHANNEL_MIST   LEDC_CHANNEL_0
#define LEDC_CHANNEL_FAN    LEDC_CHANNEL_1
#define LEDC_TIMER_MIST     LEDC_TIMER_0
#define LEDC_TIMER_FAN      LEDC_TIMER_1

/**
 * @brief Initialise les GPIOs et les canaux LEDC PWM
 */
void pwm_init(void);

/**
 * @brief Définit le niveau de brumisation et ventilateur (couplés)
 * @param percent  0–100 %
 *                 0  → CS=LOW, PWM=0%
 *                 >0 → CS=HIGH, PWM=percent%
 */
void pwm_set_level(uint8_t percent);

/**
 * @brief Retourne le niveau actuel (0–100)
 */
uint8_t pwm_get_level(void);

/**
 * @brief Allume ou éteint la LED d'état
 */
void led_set(bool on);
