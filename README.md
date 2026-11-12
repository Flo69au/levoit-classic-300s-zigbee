# Levoit Classic 300S → Zigbee (XIAO ESP32-C6, Arduino IDE)

Firmware : `arduino/levoit_zigbee_arduino/levoit_zigbee_arduino.ino`

| | |
|---|---|
| Carte | XIAO ESP32-C6 |
| Fabricant / type d'image OTA | `0x131B` / `0x0000` |
| Modèle Zigbee | `Classic300S-Fan` (fabricant `Levoit`) |
| Endpoints | 1 = brumisation + ventilateur (On/Off + Level), 2 = niveau d'eau (Binary Input) |

## Réglages Arduino IDE

- Carte : `XIAO_ESP32C6`
- Zigbee Mode : `Zigbee ZCZR (coordinator/router)` (l'appareil est un routeur Zigbee)
- Partition Scheme : `Zigbee ZCZR 4MB with spiffs`
- USB CDC On Boot : `Enabled` (GPIO16/17 servent à KS et au ventilateur)
- Core ESP32 : 3.3.12 (même version que la CI)
- Erase All Flash : Disabled (pour garder l'appairage)

## Premier flash (USB)

L'ancienne table de partitions ESP-IDF (une seule partition OTA de 576 Ko) est trop petite
pour un firmware Arduino : le passage à Arduino se fait **une fois par USB**. Ensuite, tout
passe par l'OTA Zigbee. Refaire l'appairage après ce flash (appui de 5 s sur le switch).

La version par défaut du sketch (`OTA_FW_VERSION`, flash USB) doit être égale au dernier
tag publié.

## Publier une mise à jour OTA

Version = `0xMMmmPP00` : v1.0 → `0x01000000`, v1.1 → `0x01010000`.

1. Modifier le sketch, puis mettre à jour la valeur par défaut de `OTA_FW_VERSION`.
2. GitHub : *Releases → Draft a new release → nouveau tag `v1.1` → Publish release*
   (ou pousser le tag).
3. Le workflow `.github/workflows/ota-release.yml` compile avec la version du tag, fabrique
   l'image `.zigbee`, la publie dans la Release et met à jour `ota_index.json`.
4. L'index commun (`Flo69au/zigbee-ota`) fusionne cet index toutes les heures
   (ou *Actions → Fusion des index OTA → Run workflow*).
5. Zigbee2MQTT : fiche de l'appareil → OTA → *Check for updates* → *Update* (~1 h).

Configuration Zigbee2MQTT (index commun, identique pour tous mes projets Zigbee) :

```yaml
ota:
  zigbee_ota_override_index_location: https://raw.githubusercontent.com/Flo69au/zigbee-ota/main/ota_index.json
  disable_automatic_update_check: false
  update_check_interval: 1440
  default_maximum_data_size: 50
```

`ota_index.json` ne contient que ce projet et **jamais `"force": true"`**.

## Zigbee2MQTT

Convertisseur externe : `levoit_classic300s.js` (à copier dans `external_converters/`,
avec `advanced.enable_external_js: true`). Il expose la brumisation (lumière dimmable)
et le niveau d'eau (`water_ok`) et active l'OTA. Non testé.

## Comportement

- Brumisation et ventilateur couplés (curseur 0–100 %), CS bas et PWM à 0 à l'arrêt.
  PWM calibré : brumisation 156,25 kHz / 8 bits (seuil 110), ventilateur 25 kHz / 10 bits
  (courbe quadratique, seuil 213).
- LED : clignote (300 ms) en attente de réseau, fixe quand la brumisation tourne,
  clignotement rapide (150 ms) si le réservoir est vide, très rapide (100 ms) pendant une OTA.
- Switch D9 : maintenir 5 s = effacement du réseau Zigbee + appairage
  (désactivé pendant une OTA). Aucune autre fonction.
- Garde-fou OTA : redémarrage si l'OTA se termine sans redémarrer ou dure plus de 2 h.
