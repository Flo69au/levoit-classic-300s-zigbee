#pragma once

#include <stdbool.h>

// ── GPIO ──────────────────────────────────────────────
#define GPIO_KS   16  // Détection niveau eau (flotteur) — D6

/**
 * @brief Initialise le GPIO capteur niveau eau
 */
void sensors_init(void);

/**
 * @brief Lit l'état du réservoir d'eau
 * @return true  = réservoir plein (ou niveau OK)
 *         false = réservoir vide
 */
bool sensors_water_ok(void);
