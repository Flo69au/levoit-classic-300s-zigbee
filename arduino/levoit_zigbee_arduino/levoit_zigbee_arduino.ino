/*
 * Levoit Classic 300S -> Zigbee (XIAO ESP32-C6, Arduino IDE) — avec OTA Zigbee
 *
 * Réglages Arduino IDE (menu Outils) :
 *   Carte             : XIAO_ESP32C6
 *   Zigbee Mode       : Zigbee ED (end device)
 *   Partition Scheme  : Zigbee 4MB with spiffs   <- indispensable pour l'OTA
 *   Core ESP32        : 3.2.0 ou plus récent (client OTA Zigbee inclus)
 *
 * Branchement :
 *   D4 -> CS    (connecteur blanc WHQ-DCP34V3)
 *   D5 -> PWM   (connecteur blanc, brumisation AW2205)
 *   D6 <- KS    (connecteur blanc, niveau d'eau)
 *   D7 -> Gate LR7843 via 100 ohm (ventilateur SF5020SL 12V)
 *   D8 -> LED jaune + résistance
 *   D9 <- switch vers GND (appairage)
 *
 * Valeurs PWM reprises du firmware ESP-IDF calibré sur le matériel réel.
 */

#ifndef ZIGBEE_MODE_ED
#error "Outils > Zigbee Mode > Zigbee ED (end device)"
#endif

#include "Zigbee.h"

// ── Broches ───────────────────────────────────────────
#define PIN_CS      D4    // GPIO22
#define PIN_MIST    D5    // GPIO23
#define PIN_KS      D6    // GPIO16
#define PIN_FAN     D7    // GPIO17
#define PIN_LED     D8    // GPIO19
#define PIN_SWITCH  D9    // GPIO20

// ── PWM (calibré sur hardware réel) ───────────────────
#define FREQ_MIST        156250   // résonance piézo AW2205 (40 MHz / 256)
#define BITS_MIST        8        // 0-255
#define MIN_DUTY_MIST    110      // seuil minimum AW2205

#define FREQ_FAN         25000    // SF5020SL, 25 kHz standard
#define BITS_FAN         10       // 0-1023
#define MIN_DUTY_FAN     213      // seuil de démarrage (~21 %)
#define MAX_DUTY_FAN     1023

// ── Switch ────────────────────────────────────────────
#define LONG_PRESS_MS    5000
#define SHORT_PRESS_MAX  3000
#define DEBOUNCE_MS      50

// ── Zigbee ────────────────────────────────────────────
#define EP_HUMIDIFIER    1
#define EP_WATER         2

// ── OTA ───────────────────────────────────────────────
// Doivent correspondre à generate_ota.py (MANUFACTURER_CODE / IMAGE_TYPE).
// FILE_VERSION : à incrémenter à chaque nouvelle version OTA,
// et doit être égal au numéro passé à `python generate_ota.py <version> ...`
#define OTA_MANUFACTURER   0x131B
#define OTA_IMAGE_TYPE     0x0000
#define OTA_FILE_VERSION   2
#define OTA_HW_VERSION     0x0001
#define OTA_MAX_DATA_SIZE  64

ZigbeeDimmableLight zbMist(EP_HUMIDIFIER);   // On/Off + Level Control
ZigbeeBinary        zbWater(EP_WATER);       // niveau d'eau

// ── État ──────────────────────────────────────────────
static uint8_t  currentPercent = 0;
static bool     lastWaterOk    = true;
static bool     otaRequested   = false;
static uint32_t lastWaterCheck = 0;

// ── Conversions de duty ───────────────────────────────
// Brumisation : linéaire + seuil minimum AW2205 (8 bits)
static uint32_t dutyMist(uint8_t percent) {
  if (percent == 0)   return 0;
  if (percent >= 100) return 255;
  return MIN_DUTY_MIST + (uint32_t)percent * (255 - MIN_DUTY_MIST) / 100;
}

// Ventilateur : quadratique, compense la réponse non linéaire du SF5020SL
static uint32_t dutyFan(uint8_t percent) {
  if (percent == 0)   return 0;
  if (percent >= 100) return MAX_DUTY_FAN;
  uint32_t range = MAX_DUTY_FAN - MIN_DUTY_FAN;
  return MIN_DUTY_FAN + (uint32_t)percent * percent * range / 10000;
}

// ── Contrôle brumisation + ventilateur (couplés 1:1) ──
void setMist(uint8_t percent) {
  if (percent > 100) percent = 100;
  currentPercent = percent;

  if (percent == 0) {
    digitalWrite(PIN_CS, LOW);
    ledcWrite(PIN_MIST, 0);
    ledcWrite(PIN_FAN, 0);
  } else {
    digitalWrite(PIN_CS, HIGH);
    ledcWrite(PIN_MIST, dutyMist(percent));
    ledcWrite(PIN_FAN, dutyFan(percent));
  }
  Serial.printf("Brumisation: %d%% (mist=%lu fan=%lu)\n", percent,
                (unsigned long)dutyMist(percent), (unsigned long)dutyFan(percent));
}

// ── Niveau d'eau ──────────────────────────────────────
bool waterOk() {
  // KS = LOW -> flotteur fermé -> eau OK (validé sur le matériel)
  return digitalRead(PIN_KS) == LOW;
}

// ── Callback Zigbee (on/off + niveau 0-254) ───────────
void onMistChange(bool on, uint8_t level) {
  uint8_t percent = on ? (uint8_t)((uint32_t)level * 100 / 254) : 0;
  if (on && level > 0 && percent == 0) percent = 1;

  if (percent > 0 && !waterOk()) {
    Serial.println("Réservoir vide, démarrage refusé");
    setMist(0);
    zbMist.setLight(false, 0);
    return;
  }
  setMist(percent);
}

void checkWater() {
  bool ok = waterOk();
  if (ok != lastWaterOk) {
    lastWaterOk = ok;
    zbWater.setBinaryInput(ok);
    zbWater.reportBinaryInput();
    if (!ok) {
      Serial.println("Réservoir vide -> arrêt");
      setMist(0);
      zbMist.setLight(false, 0);
    }
  }
}

// ── LED ───────────────────────────────────────────────
void updateLed() {
  if (!Zigbee.connected()) {
    // en attente de réseau : 300 ms allumée / 300 ms éteinte
    digitalWrite(PIN_LED, (millis() / 300) % 2);
  } else if (!lastWaterOk) {
    // connecté mais réservoir vide : clignotement rapide
    digitalWrite(PIN_LED, (millis() / 150) % 2);
  } else {
    // connecté : allumée seulement si brumisation active
    digitalWrite(PIN_LED, currentPercent > 0 ? HIGH : LOW);
  }
}

void blinkLed(int times, int onMs, int offMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(PIN_LED, HIGH);
    delay(onMs);
    digitalWrite(PIN_LED, LOW);
    delay(offMs);
  }
}

// ── Appairage (network steering) ──────────────────────
void startPairing() {
  esp_zb_lock_acquire(portMAX_DELAY);
  esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
  esp_zb_lock_release();
}

// ── Switch appairage ──────────────────────────────────
void handleSwitch() {
  static bool     pressed   = false;
  static bool     longDone  = false;
  static uint32_t pressedAt = 0;

  bool down = (digitalRead(PIN_SWITCH) == LOW);

  if (down && !pressed) {
    pressed   = true;
    longDone  = false;
    pressedAt = millis();
  }

  // appui long : déclenché dès 5 s
  if (down && pressed && !longDone && (millis() - pressedAt >= LONG_PRESS_MS)) {
    longDone = true;
    Serial.println("Appui long -> reset réseau + réappairage");
    blinkLed(5, 400, 400);
    Zigbee.factoryReset();      // efface le réseau et redémarre
  }

  // appui court : au relâchement (3-5 s ignoré)
  if (!down && pressed) {
    pressed = false;
    uint32_t held = millis() - pressedAt;
    if (!longDone && held >= DEBOUNCE_MS && held < SHORT_PRESS_MAX) {
      Serial.println("Appui court -> appairage");
      blinkLed(3, 100, 100);
      startPairing();
    }
  }
}

// ── Setup ─────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, LOW);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_KS, INPUT_PULLUP);
  pinMode(PIN_SWITCH, INPUT_PULLUP);

  if (!ledcAttach(PIN_MIST, FREQ_MIST, BITS_MIST))
    Serial.println("ERREUR: PWM brumisation (156,25 kHz / 8 bits) refusé");
  if (!ledcAttach(PIN_FAN, FREQ_FAN, BITS_FAN))
    Serial.println("ERREUR: PWM ventilateur (25 kHz / 10 bits) refusé");
  ledcWrite(PIN_MIST, 0);
  ledcWrite(PIN_FAN, 0);

  // Appareil Zigbee (mêmes noms que le firmware ESP-IDF)
  zbMist.setManufacturerAndModel("Levoit", "Classic300S-Fan");
  zbMist.onLightChange(onMistChange);
  zbMist.addOTAClient(OTA_FILE_VERSION, OTA_FILE_VERSION, OTA_HW_VERSION,
                      OTA_MANUFACTURER, OTA_IMAGE_TYPE, OTA_MAX_DATA_SIZE);
  Zigbee.addEndpoint(&zbMist);

  zbWater.addBinaryInput();
  zbWater.setBinaryInputApplication(BINARY_INPUT_APPLICATION_TYPE_SECURITY_ALARM);
  zbWater.setBinaryInputDescription("Water level OK");
  Zigbee.addEndpoint(&zbWater);

  if (!Zigbee.begin()) {
    Serial.println("Échec démarrage Zigbee, redémarrage...");
    ESP.restart();
  }
  Serial.printf("Zigbee démarré (firmware OTA v%d), recherche du réseau...\n",
                OTA_FILE_VERSION);

  lastWaterOk = waterOk();
  zbWater.setBinaryInput(lastWaterOk);
}

// ── Loop ──────────────────────────────────────────────
void loop() {
  handleSwitch();
  updateLed();

  // Dès que le réseau est rejoint : demande d'une éventuelle mise à jour OTA
  if (Zigbee.connected() && !otaRequested) {
    otaRequested = true;
    zbMist.requestOTAUpdate();
    zbWater.setBinaryInput(lastWaterOk);
    zbWater.reportBinaryInput();
  }
  if (!Zigbee.connected()) otaRequested = false;

  if (millis() - lastWaterCheck >= 2000) {
    lastWaterCheck = millis();
    checkWater();
  }
  delay(10);
}
