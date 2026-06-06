#pragma once

#include "esp_err.h"
#include "esp_zigbee_core.h"

// Identifiants OTA (doivent correspondre au fichier .ota généré)
#define OTA_UPGRADE_MANUFACTURER   0x131B        // Espressif
#define OTA_UPGRADE_IMAGE_TYPE     0x0000
#define OTA_UPGRADE_FILE_VERSION   0x01000000   // v1.0.0.0
#define OTA_UPGRADE_MAX_DATA_SIZE  64

/**
 * @brief Crée le cluster OTA client (réception de mises à jour)
 */
esp_zb_attribute_list_t *ota_cluster_create(void);

/**
 * @brief Gère les événements OTA reçus du stack Zigbee
 */
esp_err_t ota_upgrade_status_handler(esp_zb_zcl_ota_upgrade_value_message_t message);
