// Convertisseur externe Zigbee2MQTT — humidificateur Levoit Classic 300S (XIAO ESP32-C6)
// NON TESTÉ : écrit d'après le modèle du capteur CO2, à valider dans Z2M.
//
// Installation :
//   1. Copier ce fichier dans le dossier "external_converters/" situé
//      à côté de configuration.yaml (le créer s'il n'existe pas).
//   2. Dans configuration.yaml :
//        advanced:
//          enable_external_js: true
//   3. Redémarrer Zigbee2MQTT, puis appairer l'appareil (ou re-déclencher
//      l'interview s'il apparaît comme "Unsupported").
//
// zigbeeModel / model doivent correspondre à setManufacturerAndModel() du sketch.
//
// Endpoints du firmware :
//   1 = brumisation + ventilateur couplés (On/Off + Level Control, 0-254 = 0-100 %)
//   2 = niveau d'eau (Binary Input : true = eau OK, false = réservoir vide)
//
// "ota: true" expose la mise à jour dans Z2M ; l'index OTA est l'index commun
// https://raw.githubusercontent.com/Flo69au/zigbee-ota/main/ota_index.json

import {deviceEndpoints, onOff, binary} from 'zigbee-herdsman-converters/lib/modernExtend';
import * as exposes from 'zigbee-herdsman-converters/lib/exposes';
import * as reporting from 'zigbee-herdsman-converters/lib/reporting';

const e = exposes.presets;
const ea = exposes.access;

// Curseur de brumisation en pourcentage (0-100 %) au lieu de "brightness" (0-254).
// 0 % = arrêt (CS bas, PWM à 0), le ventilateur suit 1:1 côté firmware.
const mistLevel = {
    exposes: [e.numeric('mist_level', ea.ALL).withUnit('%').withValueMin(0).withValueMax(100)
        .withValueStep(1).withDescription('Niveau de brumisation (le ventilateur suit)')],
    fromZigbee: [{
        cluster: 'genLevelCtrl',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg) => {
            if (msg.data.currentLevel !== undefined) {
                return {mist_level: Math.round((Number(msg.data.currentLevel) * 100) / 254)};
            }
        },
    }],
    toZigbee: [{
        key: ['mist_level'],
        convertSet: async (entity, key, value, meta) => {
            const ep = meta.device.getEndpoint(1);
            const pct = Math.max(0, Math.min(100, Math.round(Number(value))));
            const level = Math.round((pct * 254) / 100);
            await ep.command('genLevelCtrl', 'moveToLevelWithOnOff', {level, transtime: 0}, {});
            return {state: {mist_level: pct}};
        },
        convertGet: async (entity, key, meta) => {
            await meta.device.getEndpoint(1).read('genLevelCtrl', ['currentLevel']);
        },
    }],
    configure: [async (device, coordinatorEndpoint) => {
        const ep = device.getEndpoint(1);
        await reporting.bind(ep, coordinatorEndpoint, ['genLevelCtrl']);
        await reporting.brightness(ep);
        await ep.read('genLevelCtrl', ['currentLevel']);
    }],
    isModernExtend: true,
};

export default {
    zigbeeModel: ['Classic300S-Fan'],
    model: 'Classic300S-Fan',
    vendor: 'Florian-DIY',
    description: 'Humidificateur Levoit Classic 300S converti en Zigbee (XIAO ESP32-C6)',
    ota: true,
    extend: [
        deviceEndpoints({endpoints: {mist: 1, water: 2}}),
        // Marche/arrêt de la brumisation (endpoint 1)
        onOff({endpointNames: ['mist'], powerOnBehavior: false}),
        // Curseur 0-100 % (endpoint 1)
        mistLevel,
        // Niveau d'eau
        binary({
            name: 'water_ok',
            endpointName: 'water',
            cluster: 'genBinaryInput',
            attribute: 'presentValue',
            valueOn: ['OK', 1],
            valueOff: ['EMPTY', 0],
            description: 'Réservoir d\'eau (OK = eau présente, EMPTY = réservoir vide)',
            access: 'STATE_GET',
            // Indispensable : sans "reporting", Z2M ne crée pas la liaison (bind) du cluster
            // genBinaryInput sur l'endpoint 2. Le firmware envoie ses rapports vers la table de
            // liaison : sans bind, ils ne partent nulle part et l'état reste figé.
            reporting: {min: 'MIN', max: '1_HOUR', change: 0},
        }),
    ],
};
