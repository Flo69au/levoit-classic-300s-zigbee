#include "ota.h"

#include "esp_zigbee_ota.h"
#include "zcl/esp_zigbee_zcl_ota.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "OTA";

esp_zb_attribute_list_t *ota_cluster_create(void)
{
    esp_zb_ota_cluster_cfg_t ota_cfg = {
        .ota_upgrade_file_version        = OTA_UPGRADE_FILE_VERSION,
        .ota_upgrade_manufacturer        = OTA_UPGRADE_MANUFACTURER,
        .ota_upgrade_image_type          = OTA_UPGRADE_IMAGE_TYPE,
        .ota_min_block_reque             = 0,
        .ota_upgrade_file_offset         = ESP_ZB_ZCL_OTA_UPGRADE_FILE_OFFSET_DEF_VALUE,
        .ota_upgrade_downloaded_file_ver = ESP_ZB_ZCL_OTA_UPGRADE_DOWNLOADED_FILE_VERSION_DEF_VALUE,
        .ota_upgrade_server_id           = ESP_ZB_ZCL_OTA_UPGRADE_SERVER_DEF_VALUE,
        .ota_image_upgrade_status        = ESP_ZB_ZCL_OTA_UPGRADE_IMAGE_STATUS_DEF_VALUE,
    };
    return esp_zb_ota_cluster_create(&ota_cfg);
}

esp_err_t ota_upgrade_status_handler(esp_zb_zcl_ota_upgrade_value_message_t message)
{
    static esp_ota_handle_t     s_ota_handle    = 0;
    static const esp_partition_t *s_ota_part    = NULL;
    esp_err_t err = ESP_OK;

    if (message.info.status != ESP_ZB_ZCL_STATUS_SUCCESS) {
        return ESP_FAIL;
    }

    switch (message.upgrade_status) {

    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_START:
        s_ota_part = esp_ota_get_next_update_partition(NULL);
        if (!s_ota_part) {
            ESP_LOGE(TAG, "Aucune partition OTA disponible");
            return ESP_ERR_NOT_FOUND;
        }
        err = esp_ota_begin(s_ota_part, OTA_WITH_SEQUENTIAL_WRITES, &s_ota_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "OTA démarré → %s  v%08lx",
                     s_ota_part->label, message.ota_header.file_version);
        }
        break;

    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_RECEIVE:
        if (message.payload && message.payload_size) {
            err = esp_ota_write(s_ota_handle, message.payload, message.payload_size);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_write: %s", esp_err_to_name(err));
            }
        }
        break;

    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_CHECK:
        ESP_LOGI(TAG, "OTA vérification intégrité...");
        break;

    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_FINISH:
        err = esp_ota_end(s_ota_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_end: %s", esp_err_to_name(err));
            break;
        }
        err = esp_ota_set_boot_partition(s_ota_part);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_set_boot_partition: %s", esp_err_to_name(err));
            break;
        }
        ESP_LOGI(TAG, "OTA terminé — redémarrage dans 3 s");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
        break;

    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_ABORT:
        ESP_LOGW(TAG, "OTA annulé");
        if (s_ota_handle) {
            esp_ota_abort(s_ota_handle);
            s_ota_handle = 0;
            s_ota_part   = NULL;
        }
        break;

    default:
        break;
    }
    return err;
}
