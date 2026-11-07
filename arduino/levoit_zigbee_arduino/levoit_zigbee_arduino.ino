/*
 * Levoit Classic 300S -> Zigbee (XIAO ESP32-C6, Arduino IDE)
 *
 * Réglages Arduino IDE (menu Outils) :
 *   Carte             : XIAO_ESP32C6
 *   Zigbee Mode       : Zigbee ED (end device)
 *   Partition Scheme  : Zigbee 4MB with spiffs
 *   Core ESP32        : 3.2.0 ou plus récent
 *
 * Branchement :
 *   D4 -> CS    (connecteur blanc WHQ-DCP34V3)
 *   D5 -> PWM   (connecteur blanc, brumisation)
 *   D6 <- KS    (connecteur blanc, niveau d'eau)
 *   D7 -> Gate LR7843 via 100 ohm (ventilateur 12V)
 *   D8 -> LED jaune + résistance
 *   D9 <- switch vers GND (appairage)
 */

#ifndef ZIGBEE_MODE_ED
#error "Outils > Zigbee Mode > Zigbee ED (end device)"
#endif

#include "Zigbee.h"

// ── Broches ───────────────────────────────────────────
#define PIN_CS      D4
#define PIN_MIST    D5
#define PIN_KS      D6
#define PIN_FAN     D7
#define PIN_LED     D8
#define PIN_SWITCH  D9

// ── PWM ───────────────────────────────────────────────
#define FREQ_MIST   40000   // à ajuster après mesure à l'oscilloscope
#define FREQ_FAN    25000
#define PWM_BITS    10
#define PWM_MAX     ((1 << PWM_BITS) - 1)

// ── Switch ────────────────────────────────────────────
#define LONG_PRESS_MS   5000
#define SHORT_PRESS_MAX 3000
#define DEBOUNCE_MS     50

// ── Zigbee ────────────────────────────────────────────
#define EP_HUMIDIFIER   10
#define EP_WATER        11

ZigbeeDimmableLight zbMist(EP_HUMIDIFIER);   // On/Off + Level Control
ZigbeeBinary        zbWater(EP_WATER);       // niveau d'eau

// ── État ──────────────────────────────────────────────
static uint8_t  currentPercent = 0;
static bool     lastWaterOk    = true;
static uint32_t lastWaterCheck = 0;

// ── Contrôle brumisation + ventilateur (option A) ─────
void setMist(uint8_t percent) {
  if (percent > 100) percent = 100;
  currentPercent = percent;

  uint32_t duty = (uint32_t)percent * PWM_MAX / 100;

  if (percent == 0) {
    digitalWrite(PIN_CS, LOW);
    ledcWrite(PIN_MIST, 0);
    ledcWrite(PIN_FAN, 0);
  } else {
    digitalWrite(PIN_CS, HIGH);
    ledcWrite(PIN_MIST, duty);
    ledcWrite(PIN_FAN, duty);   // ratio 1:1
  }
  Serial.printf("Brumisation: %d%%\n", percent);
}

// ── Callback Zigbee (on/off + niveau 0-254) ───────────
void onMistChange(bool on, uint8_t level) {
  uint8_t percent = on ? (uint8_t)((uint32_t)level * 100 / 254) : 0;

  // niveau > 0 mais arrondi à 0 -> minimum 1%
  if (on && level > 0 && percent == 0) percent = 1;

  // réservoir vide : on refuse de démarrer
  if (percent > 0 && digitalRead(PIN_KS) == HIGH) {   // HIGH = vide (à valider)
    Serial.println("Réservoir vide, démarrage refusé");
    setMist(0);
    return;
  }
  setMist(percent);
}

// ── Niveau d'eau ──────────────────────────────────────
bool waterOk() {
  // LOW = flotteur fermé = eau OK. Inverser si ta mesure montre l'inverse.
  return digitalRead(PIN_KS) == LOW;
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
      zbMist.setLightState(false);
      zbMist.setLightLevel(0);
    }
  }
}

// ── LED ───────────────────────────────────────────────
void updateLed() {
  if (!Zigbee.connected()) {
    // en attente de réseau : clignote 300 ms / 300 ms
    digitalWrite(PIN_LED, (millis() / 300) % 2);
  } else {
    // connecté : allumée seulement si brumisation active
    digitalWrite(PIN_LED, currentPercent > 0 ? HIGH : LOW);
  }
}

void blinkLed(int times, int ms) {
  for (int i = 0; i < times; i++) {
    digitalWrite(PIN_LED, HIGH);
    delay(ms);
    digitalWrite(PIN_LED, LOW);
    delay(ms);
  }
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
    blinkLed(5, 300);
    Zigbee.factoryReset();      // efface le réseau et redémarre
  }

  // appui court : au relâchement
  if (!down && pressed) {
    pressed = false;
    uint32_t held = millis() - pressedAt;
    if (!longDone && held >= DEBOUNCE_MS && held <= SHORT_PRESS_MAX) {
      Serial.println("Appui court -> appairage");
      blinkLed(3, 100);
      if (!Zigbee.connected()) {
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
      }
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

  ledcAttach(PIN_MIST, FREQ_MIST, PWM_BITS);
  ledcAttach(PIN_FAN,  FREQ_FAN,  PWM_BITS);
  ledcWrite(PIN_MIST, 0);
  ledcWrite(PIN_FAN, 0);

  // Appareil Zigbee
  zbMist.setManufacturerAndModel("Levoit", "Classic300S-ZB");
  zbMist.onLightChange(onMistChange);
  Zigbee.addEndpoint(&zbMist);

  zbWater.addBinaryInput();
  zbWater.setBinaryInputApplication(BINARY_INPUT_APPLICATION_TYPE_SECURITY_ALARM);
  zbWater.setBinaryInputDescription("Water level OK");
  Zigbee.addEndpoint(&zbWater);

  if (!Zigbee.begin()) {
    Serial.println("Échec démarrage Zigbee, redémarrage...");
    ESP.restart();
  }
  Serial.println("Zigbee démarré, recherche du réseau...");

  lastWaterOk = waterOk();
  zbWater.setBinaryInput(lastWaterOk);
}

// ── Loop ──────────────────────────────────────────────
void loop() {
  handleSwitch();
  updateLed();

  if (millis() - lastWaterCheck >= 2000) {
    lastWaterCheck = millis();
    checkWater();
  }
  delay(10);
}
