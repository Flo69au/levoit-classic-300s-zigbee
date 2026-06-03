#pragma once

#include <stdint.h>
#include <stdbool.h>

// ── Identifiants Zigbee ───────────────────────────────
#define ZIGBEE_ENDPOINT          1

// Clusters utilisés
#define CLUSTER_ON_OFF           0x0006
#define CLUSTER_LEVEL_CONTROL    0x0008
#define CLUSTER_BINARY_INPUT     0x000F   // niveau eau

// ── Callbacks vers l'application ─────────────────────

/**
 * @brief Appelé quand Zigbee2MQTT change le niveau (0-254)
 *        Convertit en pourcentage et applique PWM
 */
void zigbee_on_level_change(uint8_t level_254);

/**
 * @brief Appelé quand Zigbee2MQTT envoie ON/OFF
 */
void zigbee_on_onoff(bool on);

// ── Fonctions publiques ───────────────────────────────

/**
 * @brief Initialise et démarre le stack Zigbee (tâche dédiée)
 */
void zigbee_init(void);

/**
 * @brief Retourne true si le device est connecté au réseau Zigbee
 */
bool zigbee_is_connected(void);

/**
 * @brief Démarre le network steering (appairage) et relance le clignotement LED
 */
void zigbee_start_pairing(void);

/**
 * @brief Met à jour l'attribut niveau eau dans le cluster Zigbee
 * @param water_ok  true=plein, false=vide
 */
void zigbee_report_water(bool water_ok);

/**
 * @brief Met à jour l'attribut niveau dans le cluster Level Control
 * @param percent  0-100
 */
void zigbee_report_level(uint8_t percent);
