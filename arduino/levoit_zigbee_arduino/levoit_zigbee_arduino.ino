/*
 * Levoit Classic 300S -> Zigbee (XIAO ESP32-C6, Arduino IDE) — avec OTA Zigbee
 *
 * Réglages Arduino IDE (menu Outils) :
 *   Carte             : XIAO_ESP32C6
 *   Zigbee Mode       : Zigbee ZCZR (coordinator/router)
 *   Partition Scheme  : Zigbee ZCZR 4MB with spiffs   <- indispensable pour l'OTA
 *   USB CDC On Boot   : Enabled                       <- libère GPIO16/17 (UART)
 *   Core ESP32        : 3.2.0 ou plus récent (client OTA Zigbee inclus)
 *
 * Branchement (numéros GPIO) :
 *   GPIO22 -> CS    (connecteur blanc WHQ-DCP34V3)       broche D4
 *   GPIO23 -> PWM   (connecteur blanc, brumisation)      broche D5
 *   GPIO16 <- KS    (connecteur blanc, niveau d'eau)     broche D6
 *   GPIO17 -> Gate LR7843 via 100 ohm (ventilateur 12V)  broche D7
 *   GPIO19 -> LED jaune + résistance                     broche D8
 *   GPIO20 <- switch vers GND (appui 5 s = appairage)    broche D9
 *
 * Valeurs PWM reprises du firmware ESP-IDF calibré sur le matériel réel.
 */

#ifndef ZIGBEE_MODE_ZCZR
#error "Sélectionne 'Zigbee ZCZR (coordinator/router)' dans Outils -> Zigbee mode (et Partition Scheme 'Zigbee ZCZR 4MB with spiffs')"
#endif

// GPIO16 (KS) et GPIO17 (ventilateur) sont aussi TX/RX de l'UART par défaut :
// sans USB CDC, Serial s'y attache et perd ses broches au démarrage.
#if !defined(ARDUINO_USB_CDC_ON_BOOT) || (ARDUINO_USB_CDC_ON_BOOT == 0)
#error "Outils -> USB CDC On Boot -> Enabled (GPIO16/17 sont utilisés par KS et le ventilateur)"
#endif

#include "Zigbee.h"

// ── Broches (numéros GPIO) ────────────────────────────
#define PIN_CS      22    // D4 — Chip Select transducteur (enable)
#define PIN_MIST    23    // D5 — PWM brumisation
#define PIN_KS      16    // D6 — Détection niveau d'eau (flotteur)
#define PIN_FAN     17    // D7 — PWM ventilateur (via LR7843)
#define PIN_LED     19    // D8 — LED état
#define PIN_SWITCH  20    // D9 — Switch appairage

// ── PWM (calibré sur hardware réel) ───────────────────
#define FREQ_MIST        156250   // résonance piézo AW2205 (40 MHz / 256)
#define BITS_MIST        8        // 0-255
#define MIN_DUTY_MIST    110      // seuil minimum AW2205

#define FREQ_FAN         25000    // SF5020SL, 25 kHz standard
#define BITS_FAN         10       // 0-1023
#define MIN_DUTY_FAN     213      // seuil de démarrage (~21 %)
#define MAX_DUTY_FAN     1023

// ── Switch ────────────────────────────────────────────
#define PAIRING_PRESS_MS 5000   // maintenir 5 s pour lancer l'appairage

// ── Zigbee ────────────────────────────────────────────
#define EP_HUMIDIFIER    1
#define EP_WATER         2

// ── OTA ───────────────────────────────────────────────
// Version = 0xMMmmPP00 (majeur.mineur.correctif) : v1.0 -> 0x01000000,
// v1.1 -> 0x01010000. Le workflow GitHub impose la version du tag via
// -DOTA_FW_VERSION ; la valeur par défaut ci-dessous (flash USB) doit être
// égale au dernier tag publié.
#ifndef OTA_FW_VERSION
#define OTA_FW_VERSION       0x01010000  // v1.1
#endif
#define OTA_HW_VERSION       0x0001
#define OTA_MANUFACTURER_ID  0x131B      // doit matcher l'image .zigbee généré
#define OTA_IMAGE_TYPE       0x0000      // doit matcher l'image .zigbee généré

ZigbeeDimmableLight zbMist(EP_HUMIDIFIER);   // On/Off + Level Control
ZigbeeBinary        zbWater(EP_WATER);       // niveau d'eau

// ── État ──────────────────────────────────────────────
static uint8_t  currentPercent = 0;
static bool     lastWaterOk    = true;
static bool     otaRequested   = false;
static uint32_t lastWaterCheck = 0;

// ── Garde-fou OTA ─────────────────────────────────────
// Sur un ABORT, la lib Zigbee ne prévient pas le sketch et garde un état
// interne faux qui fait échouer les tentatives suivantes. Si l'OTA se termine
// sans redémarrage, ou dure plus de 2 h, on redémarre l'ESP.
#define OTA_MAX_DURATION_MS (2UL * 60UL * 60UL * 1000UL)
volatile bool          otaRunning = false;
volatile bool          otaEnded   = false;   // terminée sans redémarrage = échec
volatile unsigned long otaStartMs = 0;

void onOTAStateChange(bool active) {
  if (active) {
    otaStartMs = millis();
    otaEnded   = false;
  } else if (otaRunning) {
    otaEnded = true;   // en cas de succès la lib redémarre d'elle-même
  }
  otaRunning = active;
  Serial.println(active ? "OTA: mise à jour démarrée" : "OTA: terminée");
}

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

// ── Switch appairage ──────────────────────────────────
// Un seul geste : maintenir le switch 5 s.
// Le réseau Zigbee actuel est effacé et l'appareil redémarre en mode appairage
// (LED qui clignote jusqu'à la connexion).
void handleSwitch() {
  static bool     pressed   = false;
  static bool     fired     = false;
  static uint32_t pressedAt = 0;

  bool down = (digitalRead(PIN_SWITCH) == LOW);

  if (down && !pressed) {
    pressed   = true;
    fired     = false;
    pressedAt = millis();
  } else if (!down) {
    pressed = false;
  }

  if (pressed && !fired && (millis() - pressedAt >= PAIRING_PRESS_MS)) {
    fired = true;
    Serial.println("Appui 5 s -> effacement du réseau + appairage");
    blinkLed(5, 150, 150);       // confirmation visuelle
    Zigbee.factoryReset();       // efface le réseau et redémarre
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
  zbMist.addOTAClient(OTA_FW_VERSION, OTA_FW_VERSION, OTA_HW_VERSION,
                      OTA_MANUFACTURER_ID, OTA_IMAGE_TYPE);
  zbMist.onOTAStateChange(onOTAStateChange);
  Zigbee.addEndpoint(&zbMist);

  zbWater.addBinaryInput();
  zbWater.setBinaryInputApplication(BINARY_INPUT_APPLICATION_TYPE_SECURITY_OTHER);
  zbWater.setBinaryInputDescription("Water level OK");
  Zigbee.addEndpoint(&zbWater);

  // Appareil sur secteur : routeur Zigbee (relaie le réseau maillé)
  if (!Zigbee.begin(ZIGBEE_ROUTER)) {
    Serial.println("Échec démarrage Zigbee, redémarrage...");
    ESP.restart();
  }
  Serial.printf("Zigbee démarré, firmware v%lu.%lu.%lu (0x%08lX), recherche du réseau...\n",
                (unsigned long)((OTA_FW_VERSION >> 24) & 0xFF),
                (unsigned long)((OTA_FW_VERSION >> 16) & 0xFF),
                (unsigned long)((OTA_FW_VERSION >> 8) & 0xFF),
                (unsigned long)OTA_FW_VERSION);

  lastWaterOk = waterOk();
  zbWater.setBinaryInput(lastWaterOk);
}

// ── Loop ──────────────────────────────────────────────
void loop() {
  // Garde-fou OTA : redémarrage si interrompue ou bloquée ; pendant une OTA,
  // LED qui clignote vite et switch désactivé (reset réseau ou coupure en plein
  // flash = risque de brique).
  if (otaEnded) {
    Serial.println("OTA interrompue -> redémarrage pour repartir sur un état propre");
    delay(2000);
    ESP.restart();
  }
  if (otaRunning) {
    if (millis() - otaStartMs > OTA_MAX_DURATION_MS) {
      Serial.println("OTA bloquée depuis trop longtemps -> redémarrage");
      delay(500);
      ESP.restart();
    }
    digitalWrite(PIN_LED, (millis() / 100) % 2);
    delay(10);
    return;
  }

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
