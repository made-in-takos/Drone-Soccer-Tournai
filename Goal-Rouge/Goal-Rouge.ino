/*
  Goal MAITRE rouge — Drone Soccer (ESP32)
  Dossier : Goal-Rouge  |  Carte : ESP32 Dev Module

  === BRANCHEMENTS ===
  RF-5V (reception manettes)  DATA -> GPIO 4   GND  5V  antenne 17,3 cm
  FS1000A (vers esclave bleu) DATA -> GPIO 12  GND  5V  antenne 17,3 cm
  WS2812B 60 LED             DATA -> GPIO 2   GND  5V (alim separee conseillee)
  Attention : sur ESP32 Dev Module, GPIO 2 a souvent une LED bleue
  integree — si le ruban ne s'allume pas, essayer GPIO 13 ou 16.
  Verifier que "D2" sur votre carte = bien GPIO 2 (pas GPIO 4).

  === TESTS (dans l ordre) ===
  1) Televerser sans rien branche -> moniteur 115200 OK
  2) Brancher RF-5V GPIO 4 -> manette SCP/SCM/RST
  3) Brancher ruban GPIO 2 -> repos rouge, but = vert 2 s
  4) Appairer Bluetooth "Goal-Rouge" -> app recoit RED:0;BLACK:0
  5) Brancher FS1000A GPIO 12 quand l esclave existera

  === MISE A JOUR OTA (WiFi) ===
  Permet de televerser le firmware sans ouvrir le goal (apres le 1er flash USB).

  Preparation (une seule fois) :
    1) Copier wifi_secrets.example.h -> wifi_secrets.h (meme dossier)
    2) Renseigner WIFI_SSID et WIFI_PASS de votre box / reseau terrain
    3) wifi_secrets.h reste local — jamais sur GitHub

  Premier televersement (obligatoire en USB) :
    1) Carte IDE : ESP32 Dev Module  |  Moniteur serie : 115200 baud
    2) Brancher USB, televerser le sketch
    3) Verifier dans le moniteur :
         "WiFi OK — IP: x.x.x.x"
         "OTA pret — hostname: goal-rouge"
    4) Noter l'adresse IP affichee

  Mises a jour suivantes (sans cable USB) :
    1) PC portable et goal sur le MEME reseau WiFi
    2) Arduino IDE -> Outils -> Carte : ESP32 Dev Module
    3) Outils -> Port -> "goal-rouge at x.x.x.x on ..."
       (le port reseau apparait ~30 s apres le demarrage du goal)
    4) Televerser comme d'habitude — mot de passe OTA : goalrouge
    5) Pendant l'OTA : LEDs eteintes, flash but suspendu, redemarrage auto

  Depannage OTA :
    - Port reseau absent -> WiFi incorrect, goal trop loin de la box, ou redemarrer
    - Mot de passe refuse -> constante OTA_PASSWORD (goalrouge) dans ce fichier
    - Désactiver temporairement : mettre ENABLE_OTA a 0 puis re-flasher en USB

  Mettre ENABLE_LED 0 ou ENABLE_RF_TX 0 pour tester sans ces modules.

  Manettes RF (2000 bps, vers RF-5V GPIO 4) :
    Manette rouge : SCP / SCM / RST
    Manette bleue : B+  / B-  / RST  (sketch RF-Transmiter-Bleu)

  /!\ Ne pas utiliser GPIO 6 à 11 sur ESP32 — réservées à la flash.
*/
 
#include <RH_ASK.h>
#include <SPI.h>
#include <string.h>
#define FASTLED_RMT5 1
#define FASTLED_ALLOW_INTERRUPTS 0
#include <FastLED.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include "BluetoothSerial.h"

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#else
#define WIFI_SSID "A_CONFIGURER"
#define WIFI_PASS "A_CONFIGURER"
#endif

#define ENABLE_OTA     1
#define OTA_HOSTNAME   "goal-rouge"
#define OTA_PASSWORD   "goalrouge"

#define ENABLE_LED     1  // 0 pour desactiver, 1 pour activer
#define ENABLE_RF_TX   1
#define RF_DEBUG       1   // 1 = affiche signal brut GPIO4 (desactiver quand RF OK)

// 2000 = vitesse (en bits/seconde) pour la communication RF avec la manette RF-Transmiter (SCP/SCM)
// 1000 = ancienne vitesse (ex: manette Ref-Remote), non utilisée ici
#define RF_BITRATE     2000  // Débit en bauds pour le module RF
#define RF_RX_PIN      4     // Broche de réception RF, connectée au module RF-5V (reçoit SCP/SCM depuis manettes)
#define RF_TX_PIN      12    // Broche de transmission RF, utile pour envoyer les scores à l'esclave (FS1000A) — ne jamais utiliser GPIO 6-11 sur ESP32 (car utilisé par la mémoire flash)
#define RF_PTT_PIN     13    // Broche "Push-To-Talk" : ici fictive (non utilisée, mais requise par la lib) — éviter GPIO 0 et 6-11
#define LED_PIN        2     // Broche de commande pour le ruban de LED WS2812B (60 LEDs)
#define NUM_LEDS       60    // Nombre de LEDs WS2812B sur le ruban
#define BT_NAME        "Goal-Rouge" // Nom Bluetooth visible lors de l'appairage du but rouge
#define RF_DEBOUNCE_MS 500   // Temporisation (antirebond) en millisecondes pour valider boutons RF
#define FLASH_MS       2000  // Durée du flash vert lors d’un but (en millisecondes)

// Instanciation de la classe RH_ASK pour la communication RF (RadioHead) avec les bons paramètres
RH_ASK rf(RF_BITRATE, RF_RX_PIN, RF_TX_PIN, RF_PTT_PIN);

// Instanciation de la communication série Bluetooth (ESP32)
BluetoothSerial SerialBT;

CRGB leds[NUM_LEDS];
int scoreRed = 0;
int scoreBlue = 0;

unsigned long lastRedMs = 0;
unsigned long lastBlueMs = 0;
unsigned long lastResetMs = 0;
unsigned long flashStartMs = 0;
bool flashing = false;
bool btSentInitial = false;

CRGB COLOR_IDLE = CRGB::Red;    // goal rouge : repos
CRGB COLOR_GOAL = CRGB::Green;  // flash vert quand but marque

void rfPinSelfTest() {
  pinMode(RF_RX_PIN, INPUT);
  delay(5);
  int niveau = digitalRead(RF_RX_PIN);
  pinMode(RF_RX_PIN, INPUT_PULLUP);
  delay(5);
  int avecPullup = digitalRead(RF_RX_PIN);
  pinMode(RF_RX_PIN, INPUT);

  Serial.print("RF test GPIO");
  Serial.print(RF_RX_PIN);
  Serial.print(" : DATA=");
  Serial.print(niveau);
  Serial.print("  avec pull-up interne=");
  Serial.println(avecPullup);

  if (niveau == 0 && avecPullup == 0) {
    Serial.println("-> Toujours LOW : pas de signal sur DATA (cablage, alim 5V RF-5V, ou GND commun)");
  } else if (niveau == 1 || avecPullup == 1) {
    Serial.println("-> Broche OK au repos ; si edges/s=0, regler vis RF-5V ou tester emetteur");
  }
  Serial.println("Test fil : debranchez DATA du RF-5V, touchez GPIO4 sur 3V3 de l ESP32 -> edges/s doit monter");
}

void setStrip(CRGB c) {
#if ENABLE_LED
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = c;
  FastLED.show();
  delay(1);  // laisse le RMT ESP32 terminer l'envoi
#endif
}

void ledBootTest() {
#if ENABLE_LED
  Serial.println("LED test demarrage...");
  setStrip(CRGB::White);
  delay(300);
  setStrip(CRGB::Red);
  delay(300);
  setStrip(COLOR_IDLE);
  Serial.println("LED test OK — repos rouge");
#endif
}

void startFlashGoal() {
#if ENABLE_LED
  setStrip(COLOR_GOAL);
  flashStartMs = millis();
  flashing = true;
#endif
}

void updateFlash() {
  if (!flashing) return;
  if (millis() - flashStartMs >= FLASH_MS) {
    setStrip(COLOR_IDLE);
    flashing = false;
  }
}

void rfSendSlave(const char *msg) {
#if ENABLE_RF_TX
  for (int i = 0; i < 3; i++) {
    rf.send((uint8_t *)msg, strlen(msg));
    rf.waitPacketSent();
    delay(30);
  }
  Serial.print("-> Esclave: ");
  Serial.println(msg);
#endif
}

void sendSyncToSlave() {
  char buf[20];
  snprintf(buf, sizeof(buf), "SYNC:%d,%d", scoreRed, scoreBlue);
  rfSendSlave(buf);
}

void sendScoresBT() {
  char buf[32];
  // App Android DroneSoccerScore attend RED:x;BLACK:y (BLUE ignore)
  snprintf(buf, sizeof(buf), "RED:%d;BLACK:%d", scoreRed, scoreBlue);
  if (SerialBT.hasClient()) {
    SerialBT.println(buf);
  }
  Serial.print("BT: ");
  Serial.println(buf);
}

void resetScores() {
  scoreRed = 0;
  scoreBlue = 0;
  flashing = false;
  setStrip(COLOR_IDLE);
  sendScoresBT();
  rfSendSlave("RST");
  sendSyncToSlave();
  Serial.println("Scores remis a 0");
}

void butRedPlus() {
  unsigned long now = millis();
  if (now - lastRedMs < RF_DEBOUNCE_MS) return;
  lastRedMs = now;
  scoreRed++;
  Serial.print("But ROUGE: ");
  Serial.println(scoreRed);
  sendScoresBT();
  sendSyncToSlave();
  startFlashGoal();
}

void butRedMinus() {
  unsigned long now = millis();
  if (now - lastRedMs < RF_DEBOUNCE_MS) return;
  if (scoreRed <= 0) return;
  lastRedMs = now;
  scoreRed--;
  Serial.print("Correction ROUGE: ");
  Serial.println(scoreRed);
  sendScoresBT();
  sendSyncToSlave();
}

void butBluePlus() {
  unsigned long now = millis();
  if (now - lastBlueMs < RF_DEBOUNCE_MS) return;
  lastBlueMs = now;
  scoreBlue++;
  Serial.print("But BLEU: ");
  Serial.println(scoreBlue);
  sendScoresBT();
  sendSyncToSlave();
  rfSendSlave("LED:BLU");
}

void butBlueMinus() {
  unsigned long now = millis();
  if (now - lastBlueMs < RF_DEBOUNCE_MS) return;
  if (scoreBlue <= 0) return;
  lastBlueMs = now;
  scoreBlue--;
  Serial.print("Correction BLEU: ");
  Serial.println(scoreBlue);
  sendScoresBT();
  sendSyncToSlave();
}

void handleMessage(const char *msg) {
  if (strcmp(msg, "SCP") == 0 || strcmp(msg, "SC+") == 0) {
    butRedPlus();
  } else if (strcmp(msg, "SCM") == 0 || strcmp(msg, "SC-") == 0) {
    butRedMinus();
  } else if (strcmp(msg, "B+") == 0) {
    butBluePlus();
  } else if (strcmp(msg, "B-") == 0) {
    butBlueMinus();
  } else if (strcmp(msg, "RST") == 0) {
    unsigned long now = millis();
    if (now - lastResetMs < RF_DEBOUNCE_MS) return;
    lastResetMs = now;
    resetScores();
  } else {
    Serial.print("RF inconnu: ");
    Serial.println(msg);
  }
}

void setupWiFiAndOta() {
#if ENABLE_OTA
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("WiFi ");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi echec — BT/RF actifs, OTA indisponible");
    return;
  }

  Serial.print("WiFi OK — IP: ");
  Serial.println(WiFi.localIP());

  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    Serial.println("OTA demarre");
    flashing = false;
#if ENABLE_LED
    setStrip(CRGB::Black);
#endif
  });
  ArduinoOTA.onEnd([]() { Serial.println("OTA termine — redemarrage"); });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA erreur [%u]\n", error);
  });

  ArduinoOTA.begin();
  Serial.print("OTA pret — hostname: ");
  Serial.println(OTA_HOSTNAME);
#endif
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== Goal MAITRE rouge ===");

#if ENABLE_LED
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(200);
  FastLED.clear(true);
  ledBootTest();
  Serial.print("LED: GPIO");
  Serial.print(LED_PIN);
  Serial.println(" — repos rouge, but = vert 2 s");
#else
  Serial.println("LED: desactivee (ENABLE_LED 0)");
#endif

  pinMode(RF_RX_PIN, INPUT);
#if ENABLE_RF_TX
  pinMode(RF_TX_PIN, OUTPUT);
#endif

#if RF_DEBUG
  rfPinSelfTest();
#endif

  if (!rf.init()) {
    Serial.println("ERREUR: RF init failed");
  } else {
    Serial.print("RF RX=GPIO");
    Serial.print(RF_RX_PIN);
    Serial.print("  TX=GPIO");
    Serial.print(RF_TX_PIN);
    Serial.print("  ");
    Serial.print(RF_BITRATE);
    Serial.println(" bps");
  }

  if (!SerialBT.begin(BT_NAME)) {
    Serial.println("ERREUR: Bluetooth init failed");
  } else {
    Serial.print("Bluetooth SPP: ");
    Serial.println(BT_NAME);
  }

  setupWiFiAndOta();

  Serial.println("Pret — en attente manettes (SCP SCM RST)");
#if RF_DEBUG
  Serial.println("RF_DEBUG: GPIO4= niveau + changements/s (appuyez manette)");
#endif
  delay(800);
  sendScoresBT();
}

void loop() {
#if ENABLE_OTA
  ArduinoOTA.handle();
#endif

  if (SerialBT.hasClient() && !btSentInitial) {
    sendScoresBT();
    btSentInitial = true;
    Serial.println("App Bluetooth connectee");
  }
  if (!SerialBT.hasClient()) {
    btSentInitial = false;
  }

  updateFlash();

#if RF_DEBUG
  {
    static unsigned long lastDbg = 0;
    static unsigned long edges = 0;
    static int lastLvl = -1;
    int lvl = digitalRead(RF_RX_PIN);
    if (lastLvl >= 0 && lvl != lastLvl) edges++;
    lastLvl = lvl;
    if (millis() - lastDbg >= 1000) {
      Serial.print("RF diag GPIO4=");
      Serial.print(lvl);
      Serial.print(" edges/s=");
      Serial.println(edges);
      edges = 0;
      lastDbg = millis();
    }
  }
#endif

  uint8_t buf[RH_ASK_MAX_MESSAGE_LEN];
  uint8_t buflen = sizeof(buf);
  if (rf.recv(buf, &buflen)) {
    buf[buflen] = '\0';
    Serial.print("RF: ");
    Serial.println((char *)buf);
    handleMessage((char *)buf);
  }
}
