# Drone-Soccer-Tournai

Projet Drone Soccer — Tournai.

## Contenu

| Dossier | Rôle |
|---------|------|
| `Goal-Rouge/` | Goal maître ESP32 — RF manettes, Bluetooth, LEDs, lien vers goal bleu |
| `Goal-Bleu/` | Goal esclave ESP32 — réception RF du maître, LEDs |
| `RF-Transmiter/` | Manette rouge Pro Mini — SCP, SCM, RST → goal rouge |
| `RF-Transmiter-Bleu/` | Manette bleue Pro Mini — B+, B-, RST → goal rouge |
| `DroneSoccerScore/` | Application Android — affichage des scores via Bluetooth |

## Prérequis

- Arduino IDE + carte **ESP32 Dev Module** (core Espressif 3.x)
- Bibliothèques : **RadioHead**, **FastLED**
- Android Studio pour l'application

## GitHub

https://github.com/made-in-takos/Drone-Soccer-Tournai
