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

  Mettre ENABLE_LED 0 ou ENABLE_RF_TX 0 pour tester sans ces modules.

  /!\ Ne pas utiliser GPIO 6 à 11 sur ESP32 — réservées à la flash.
*/

#include <RH_ASK.h>
#include <SPI.h>
#include <string.h>
#define FASTLED_RMT5 1
#define FASTLED_ALLOW_INTERRUPTS 0
#include <FastLED.h>
#include "BluetoothSerial.h"

#define ENABLE_LED     1  // 0 pour desactiver, 1 pour activer
#define ENABLE_RF_TX   1
#define RF_DEBUG       1   // 1 = affiche signal brut GPIO4 (desactiver quand RF OK)

// 2000 = manette RF-Transmiter (SCP/SCM)  |  1000 = manette Ref-Remote (SC+/SC-)
#define RF_BITRATE     2000
#define RF_RX_PIN      4
#define RF_TX_PIN      12   // FS1000A esclave — JAMAIS GPIO 6-11 (flash interne ESP32)
#define RF_PTT_PIN     13   // broche fictive (pas de PTT) — eviter 0 et 6-11
#define LED_PIN        2
#define NUM_LEDS       60
#define BT_NAME        "Goal-Rouge"
#define RF_DEBOUNCE_MS 500
#define FLASH_MS       2000

RH_ASK rf(RF_BITRATE, RF_RX_PIN, RF_TX_PIN, RF_PTT_PIN);
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

  Serial.println("Pret — en attente manettes (SCP SCM RST)");
#if RF_DEBUG
  Serial.println("RF_DEBUG: GPIO4= niveau + changements/s (appuyez manette)");
#endif
  delay(800);
  sendScoresBT();
}

void loop() {
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
