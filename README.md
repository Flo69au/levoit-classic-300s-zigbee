# Levoit Classic 300S → Zigbee (XIAO ESP32-C6, Arduino IDE)

Firmware Arduino : `arduino/levoit_zigbee_arduino/levoit_zigbee_arduino.ino`

## Réglages Arduino IDE

- Carte : `XIAO_ESP32C6`
- Zigbee Mode : `Zigbee ED (end device)`
- Partition Scheme : `Zigbee 4MB with spiffs` (obligatoire pour l'OTA)
- Core ESP32 : 3.2.0 ou plus récent

## Premier flash (USB)

La table de partitions ESP-IDF précédente (une seule partition OTA de 576 Ko) est trop
petite pour un firmware Arduino. Le passage à Arduino se fait donc **une fois par USB**.
Après ce flash, les mises à jour se font en OTA.

L'appareil devra être réappairé (reset réseau : appui long de 5 s sur le switch).

## Publier une mise à jour OTA

1. Dans le sketch, incrémenter `OTA_FILE_VERSION` (ex. 3).
2. Arduino IDE : `Croquis > Exporter les binaires compilés`.
3. `python generate_ota.py 3 <token_github>`
   - génère le `.ota`, crée la release GitHub, met à jour `ota_index.json`
4. Dans `configuration.yaml` de Zigbee2MQTT, pointer sur l'index commun
   [zigbee-ota](https://github.com/Flo69au/zigbee-ota), qui reprend toutes les heures le `ota_index.json`
   de ce dépôt (et ceux de mes autres projets Zigbee) :

   ```yaml
   ota:
     zigbee_ota_override_index_location: https://raw.githubusercontent.com/Flo69au/zigbee-ota/main/ota_index.json
   ```

   Pour une prise en compte immédiate : dépôt `zigbee-ota` → *Actions* → *Fusion des index OTA* → *Run workflow*.

5. Redémarrer Z2M, puis l'appareil → OTA → Check.

Le numéro passé à `generate_ota.py` doit être égal à `OTA_FILE_VERSION` du sketch compilé
et strictement supérieur à la version en cours sur l'appareil.

## Comportement

- Brumisation et ventilateur couplés (curseur 0–100 %), CS bas et PWM à 0 à l'arrêt.
- LED : clignote (300 ms) en attente de réseau, fixe quand la brumisation tourne,
  clignotement rapide si le réservoir est vide.
- Switch D9 : appui < 3 s = appairage, appui > 5 s = reset réseau + réappairage.
