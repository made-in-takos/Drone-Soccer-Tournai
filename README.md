# Drone-Soccer-Tournai

Projet Drone Soccer — Tournai.

## Contenu

| Dossier | Rôle |
|---------|------|
| `Goal-Rouge/` | Goal maître ESP32 — RF, Bluetooth, LEDs, WiFi **OTA**, lien esclave |
| `Goal-Bleu/` | Goal esclave ESP32 — réception RF du maître, LEDs |
| `Goal-Bleu-8266/` | Goal esclave ESP8266 D1 Mini — RF, LEDs, WiFi **OTA** |
| `RF-Transmiter/` | Manette rouge Pro Mini — SCP, SCM, RST → goal rouge |
| `RF-Transmiter-Bleu/` | Manette bleue Pro Mini — B+, B-, RST → goal rouge |
| `DroneSoccerScore/` | Application Android — affichage des scores via Bluetooth |

## Prérequis

- Arduino IDE + carte **ESP32 Dev Module** (core Espressif 3.x)
- Goal bleu compact : **LOLIN D1 Mini (ESP8266)** → sketch `Goal-Bleu-8266/` + core **esp8266**
- Bibliothèques : **RadioHead**, **FastLED**
- Android Studio pour l'application

### Mise à jour OTA (WiFi)

**Goal rouge (ESP32)** et **goal bleu (ESP8266)** :

1. Copier `wifi_secrets.example.h` → `wifi_secrets.h` dans le dossier du sketch.
2. Premier téléversement **en USB**.
3. Ensuite : Arduino IDE → **Outils → Port** → `goal-rouge at …` ou `goal-bleu at …`.
4. Mots de passe OTA par défaut : `goalrouge` / `goalbleu`.

## GitHub

https://github.com/made-in-takos/Drone-Soccer-Tournai
