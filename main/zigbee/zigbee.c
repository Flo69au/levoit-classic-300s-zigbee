#include "zigbee.h"
#include "pwm.h"
#include "sensors.h"
#include "ota.h"

#include "esp_zigbee_core.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "ZIGBEE";

// ── État connexion ────────────────────────────────────
static bool s_zigbee_connected = false;
static TaskHandle_t s_blink_task_handle = NULL;

// ── Tâche clignotement LED ────────────────────────────

static void blink_task(void *pvParameters)
{
    for (;;) {
        led_set(true);
        vTaskDelay(pdMS_TO_TICKS(300));
        led_set(false);
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

// ── Attributs locaux (miroir) ─────────────────────────
static bool     s_on_off   = false;
static uint8_t  s_level    = 0;     // 0-254 (Zigbee)
static bool     s_water_ok = true;

// ── Conversion Zigbee (0-254) ↔ pourcentage (0-100) ───

static uint8_t level_to_percent(uint8_t level_254)
{
    if (level_254 == 0)   return 0;
    if (level_254 >= 254) return 100;
    return (uint8_t)((uint32_t)level_254 * 100 / 254);
}

static uint8_t percent_to_level(uint8_t percent)
{
    if (percent == 0)   return 0;
    if (percent >= 100) return 254;
    return (uint8_t)((uint32_t)percent * 254 / 100);
}

// ── Callbacks application ─────────────────────────────

void zigbee_on_level_change(uint8_t level_254)
{
    s_level = level_254;
    uint8_t pct = level_to_percent(level_254);

    if (!s_on_off && pct > 0) {
        s_on_off = true;
    }

    if (!sensors_water_ok() && pct > 0) {
        ESP_LOGW(TAG, "Réservoir vide — niveau ignoré");
        return;
    }

    pwm_set_level(s_on_off ? pct : 0);
    ESP_LOGI(TAG, "Niveau Zigbee=%d → %d%%", level_254, pct);
}

void zigbee_on_onoff(bool on)
{
    s_on_off = on;

    if (!sensors_water_ok() && on) {
        ESP_LOGW(TAG, "Réservoir vide — allumage ignoré");
        return;
    }

    uint8_t pct = on ? level_to_percent(s_level) : 0;
    pwm_set_level(pct);
    ESP_LOGI(TAG, "ON/OFF=%s → %d%%", on ? "ON" : "OFF", pct);
}

bool zigbee_is_connected(void)
{
    return s_zigbee_connected;
}

void zigbee_start_pairing(void)
{
    s_zigbee_connected = false;
    pwm_set_level(0);

    // Relancer le clignotement LED (recherche réseau)
    if (s_blink_task_handle == NULL) {
        xTaskCreate(blink_task, "blink_task", 1024, NULL, 3, &s_blink_task_handle);
    }

    esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
    ESP_LOGI(TAG, "Appairage démarré");
}

// ── Handler attributs Zigbee ──────────────────────────

static esp_err_t zb_attribute_handler(
    const esp_zb_zcl_set_attr_value_message_t *message)
{
    if (!message) return ESP_ERR_INVALID_ARG;

    uint16_t cluster = message->info.cluster;
    uint16_t attr_id = message->attribute.id;

    ESP_LOGI(TAG, "Attribut reçu — cluster=0x%04X attr=0x%04X",
             cluster, attr_id);

    // ── On/Off cluster (0x0006) ───────────────────────
    if (cluster == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF) {
        if (attr_id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID) {
            bool on = *(bool *)message->attribute.data.value;
            zigbee_on_onoff(on);
        }
    }

    // ── Level Control cluster (0x0008) ────────────────
    if (cluster == ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL) {
        if (attr_id == ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID) {
            uint8_t level = *(uint8_t *)message->attribute.data.value;
            zigbee_on_level_change(level);
        }
    }

    return ESP_OK;
}

// ── Handler actions Zigbee (commandes) ────────────────

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t cb_id,
                                   const void *message)
{
    switch (cb_id) {
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
        return zb_attribute_handler(
            (esp_zb_zcl_set_attr_value_message_t *)message);
    case ESP_ZB_CORE_OTA_UPGRADE_VALUE_CB_ID:
        return ota_upgrade_status_handler(
            *(esp_zb_zcl_ota_upgrade_value_message_t *)message);
    default:
        ESP_LOGW(TAG, "Action non gérée: 0x%x", cb_id);
        break;
    }
    return ESP_OK;
}

// ── Wrapper pour esp_zb_scheduler_alarm (évite cast de type incompatible) ─

static void steering_cb(uint8_t mode)
{
    esp_zb_bdb_start_top_level_commissioning(mode);
}

// ── Handler événements stack Zigbee ──────────────────

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t          *p_sg_p     = signal_struct->p_app_signal;
    esp_err_t          err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;

    switch (sig_type) {

    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Démarrage Zigbee...");
        esp_zb_bdb_start_top_level_commissioning(
            ESP_ZB_BDB_MODE_INITIALIZATION);
        break;

    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "Démarrage OK");
            esp_zb_bdb_start_top_level_commissioning(
                ESP_ZB_BDB_MODE_NETWORK_STEERING);
        } else {
            ESP_LOGW(TAG, "Échec démarrage, retry...");
            esp_zb_scheduler_alarm(
                steering_cb,
                ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;

    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            esp_zb_ieee_addr_t extended_pan_id;
            esp_zb_get_extended_pan_id(extended_pan_id);
            ESP_LOGI(TAG, "Réseau rejoint ! PAN ID: 0x%04hx",
                     esp_zb_get_pan_id());
            // Suppression immédiate de la tâche blink pour éviter le race condition
            if (s_blink_task_handle != NULL) {
                vTaskDelete(s_blink_task_handle);
                s_blink_task_handle = NULL;
            }
            s_zigbee_connected = true;
            led_set(false);  // LED OFF — s'allume uniquement via pwm_set_level()
        } else {
            ESP_LOGW(TAG, "Réseau non trouvé, retry...");
            esp_zb_scheduler_alarm(
                steering_cb,
                ESP_ZB_BDB_MODE_NETWORK_STEERING, 5000);
        }
        break;

    default:
        ESP_LOGD(TAG, "Signal: 0x%x, status: %s",
                 sig_type, esp_err_to_name(err_status));
        break;
    }
}

// ── Création des clusters et endpoint ─────────────────

static void zigbee_task(void *pvParameters)
{
    // ── Config stack Zigbee (End Device) ──────────────
    esp_zb_cfg_t zb_config = {
        .esp_zb_role              = ESP_ZB_DEVICE_TYPE_ED,
        .install_code_policy      = false,
        .nwk_cfg.zed_cfg = {
            .ed_timeout = ESP_ZB_ED_AGING_TIMEOUT_64MIN,
            .keep_alive = 3000,
        },
    };
    esp_zb_init(&zb_config);

    // ── Cluster On/Off ────────────────────────────────
    esp_zb_on_off_cluster_cfg_t on_off_cfg = {
        .on_off = false,
    };
    esp_zb_attribute_list_t *on_off_cluster =
        esp_zb_on_off_cluster_create(&on_off_cfg);

    // ── Cluster Level Control ─────────────────────────
    esp_zb_level_cluster_cfg_t level_cfg = {
        .current_level = 0,
    };
    esp_zb_attribute_list_t *level_cluster =
        esp_zb_level_cluster_create(&level_cfg);

    // ── Cluster Binary Input (niveau eau) ─────────────
    esp_zb_binary_input_cluster_cfg_t bin_cfg = {
        .out_of_service = false,
        .present_value  = true,   // true = eau OK
        .status_flags   = 0,
    };
    esp_zb_attribute_list_t *binary_cluster =
        esp_zb_binary_input_cluster_create(&bin_cfg);

    // ── Cluster Basic ─────────────────────────────────
    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version   = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source  = 0x01,  // mains
    };
    esp_zb_attribute_list_t *basic_cluster =
        esp_zb_basic_cluster_create(&basic_cfg);

    // Nom du fabricant et modèle
    esp_zb_basic_cluster_add_attr(
        basic_cluster,
        ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
        "Levoit");
    esp_zb_basic_cluster_add_attr(
        basic_cluster,
        ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
        "Classic300S-Fan");

    // ── Liste de clusters ─────────────────────────────
    esp_zb_cluster_list_t *cluster_list =
        esp_zb_zcl_cluster_list_create();

    // Serveur (l'appareil reçoit les commandes)
    esp_zb_cluster_list_add_basic_cluster(
        cluster_list, basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_on_off_cluster(
        cluster_list, on_off_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_level_cluster(
        cluster_list, level_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_binary_input_cluster(
        cluster_list, binary_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    // ── Cluster OTA (client — reçoit les mises à jour) ──
    esp_zb_cluster_list_add_ota_cluster(
        cluster_list, ota_cluster_create(), ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);

    // ── Endpoint ──────────────────────────────────────
    esp_zb_endpoint_config_t ep_config = {
        .endpoint        = ZIGBEE_ENDPOINT,
        .app_profile_id  = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id   = ESP_ZB_HA_DIMMABLE_LIGHT_DEVICE_ID,
        .app_device_version = 0,
    };
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_ep_list_add_ep(ep_list, cluster_list, ep_config);

    // ── Enregistrement ────────────────────────────────
    esp_zb_device_register(ep_list);
    esp_zb_core_action_handler_register(zb_action_handler);

    // ── Démarrage ─────────────────────────────────────
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();

    vTaskDelete(NULL);
}

// ── Report vers Zigbee2MQTT ───────────────────────────

void zigbee_report_water(bool water_ok)
{
    s_water_ok = water_ok;
    esp_zb_zcl_set_attribute_val(
        ZIGBEE_ENDPOINT,
        ESP_ZB_ZCL_CLUSTER_ID_BINARY_INPUT,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_BINARY_INPUT_PRESENT_VALUE_ID,
        &water_ok,
        false);
}

void zigbee_report_level(uint8_t percent)
{
    uint8_t level_254 = percent_to_level(percent);
    esp_zb_zcl_set_attribute_val(
        ZIGBEE_ENDPOINT,
        ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID,
        &level_254,
        false);
}

// ── Init publique ─────────────────────────────────────

void zigbee_init(void)
{
    // NVS requis par le stack Zigbee
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Démarrage clignotement LED (connexion en cours)
    s_zigbee_connected = false;
    xTaskCreate(blink_task, "blink_task", 1024, NULL, 3, &s_blink_task_handle);

    // Tâche Zigbee sur core 0 (radio IEEE 802.15.4)
    xTaskCreate(zigbee_task, "zigbee_task", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Tâche Zigbee démarrée — LED clignote jusqu'à connexion");
}
