#pragma once

#include <stdint.h>
#include <stdbool.h>

// ── GPIO ──────────────────────────────────────────────
#define GPIO_CS          22   // Chip Select transducteur ultrasonique — D4
#define GPIO_PWM_MIST    23   // PWM brumisation — D5
#define GPIO_PWM_FAN     17   // PWM ventilateur (via LR7843) — D7
#define GPIO_LED         19   // LED état — D8

// ── PWM ───────────────────────────────────────────────
#define PWM_FREQ         40000              // 40 kHz — ultrasonique (fan tolère 25-100 kHz)
#define PWM_RESOLUTION   LEDC_TIMER_10_BIT // 0-1023

// ── Canaux LEDC ───────────────────────────────────────
#define LEDC_CHANNEL_MIST   LEDC_CHANNEL_0
#define LEDC_CHANNEL_FAN    LEDC_CHANNEL_1
#define LEDC_TIMER_PWM      LEDC_TIMER_0   // timer unique partagé

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
