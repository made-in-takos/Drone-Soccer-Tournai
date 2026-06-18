/*
  Goal ESCLAVE bleu — Drone Soccer (ESP8266 D1 Mini ESP-12F)

  Recoit du goal ROUGE (maitre) en 433 MHz :
    SYNC:rouge,bleu  RST  LED:BLU

  === MISE A JOUR OTA (WiFi) ===
  Permet de televerser le firmware sans ouvrir le goal (apres le 1er flash USB).

  Preparation (une seule fois) :
    1) Copier wifi_secrets.example.h -> wifi_secrets.h (meme dossier)
    2) Renseigner WIFI_SSID et WIFI_PASS de votre box / reseau terrain
    3) wifi_secrets.h reste local — jamais sur GitHub

  Premier televersement (obligatoire en USB) :
    1) Carte IDE : LOLIN(WEMOS) D1 R2 & mini  |  Moniteur serie : 115200 baud
    2) Brancher USB, televerser le sketch
    3) Verifier dans le moniteur :
         "WiFi OK — IP: x.x.x.x"
         "OTA pret — hostname: goal-bleu"
    4) Noter l'adresse IP affichee

  Mises a jour suivantes (sans cable USB) :
    1) PC portable et goal sur le MEME reseau WiFi
    2) Arduino IDE -> Outils -> Carte : LOLIN(WEMOS) D1 R2 & mini
    3) Outils -> Port -> "goal-bleu at x.x.x.x on ..."
       (le port reseau apparait ~30 s apres le demarrage du goal)
    4) Televerser comme d'habitude — mot de passe OTA : goalbleu
    5) Pendant l'OTA : LEDs eteintes, flash but suspendu, redemarrage auto

  Depannage OTA :
    - Port reseau absent -> WiFi incorrect, goal trop loin de la box, ou redemarrer
    - Mot de passe refuse -> constante OTA_PASSWORD (goalbleu) dans ce fichier
    - WiFi echec au boot -> RF et LEDs restent actifs, mais pas d'OTA tant que le WiFi ne connecte pas

  === BRANCHEMENTS (WeMos / LOLIN D1 Mini) ===
  RF-5V DATA  ->  D2  (GPIO 4)   GND  5V  antenne 17,3 cm
  WS2812B     ->  D1  (GPIO 5)   GND  alim 5V separee
  GND commun avec goal rouge et alim ruban.
*/

#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <RH_ASK.h>
#include <SPI.h>
#include <string.h>
#include <FastLED.h>

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#else
#define WIFI_SSID "A_CONFIGURER"
#define WIFI_PASS "A_CONFIGURER"
#endif

#define OTA_HOSTNAME   "goal-bleu"
#define OTA_PASSWORD   "goalbleu"

#define ENABLE_LED     1
#define RF_BITRATE     2000
#define RF_RX_PIN      4      // broche D2
#define RF_TX_PIN      14     // D5 — non branche (RadioHead)
#define RF_PTT_PIN     0      // exemple RadioHead ESP8266
#define LED_PIN        5      // broche D1
#define NUM_LEDS       60
#define FLASH_MS       2000

RH_ASK rf(RF_BITRATE, RF_RX_PIN, RF_TX_PIN, RF_PTT_PIN);

CRGB leds[NUM_LEDS];
int scoreRed = 0;
int scoreBlue = 0;
int lastScoreBlue = -1;

unsigned long flashStartMs = 0;
bool flashing = false;

CRGB COLOR_IDLE = CRGB::Blue;
CRGB COLOR_GOAL = CRGB::Green;

void setStrip(CRGB c) {
#if ENABLE_LED
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = c;
  FastLED.show();
  yield();
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

void applyReset() {
  scoreRed = 0;
  scoreBlue = 0;
  lastScoreBlue = 0;
  flashing = false;
  setStrip(COLOR_IDLE);
  Serial.println("Scores remis a 0");
}

void handleSync(const char *msg) {
  int r = 0, b = 0;
  if (sscanf(msg + 5, "%d,%d", &r, &b) != 2) return;

  bool blueIncreased = (lastScoreBlue >= 0 && b > lastScoreBlue);
  scoreRed = r;
  scoreBlue = b;
  lastScoreBlue = b;

  Serial.print("Sync RED:");
  Serial.print(scoreRed);
  Serial.print(" BLUE:");
  Serial.println(scoreBlue);

  if (blueIncreased) {
    startFlashGoal();
  }
}

void handleMessage(const char *msg) {
  if (strncmp(msg, "SYNC:", 5) == 0) {
    handleSync(msg);
  } else if (strcmp(msg, "RST") == 0) {
    applyReset();
  } else if (strcmp(msg, "LED:BLU") == 0) {
    Serial.println("But BLEU (LED:BLU)");
    startFlashGoal();
  } else {
    Serial.print("RF inconnu: ");
    Serial.println(msg);
  }
}

void setupWiFiAndOta() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("WiFi ");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print(".");
    yield();
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi OK — IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi echec — RF/LED actifs, OTA indisponible");
    return;
  }

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
  ArduinoOTA.onError([](ota_error_t err) {
    Serial.printf("OTA erreur [%u]\n", err);
  });

  ArduinoOTA.begin();
  Serial.print("OTA pret — hostname: ");
  Serial.println(OTA_HOSTNAME);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== Goal ESCLAVE bleu (ESP8266) ===");

#if ENABLE_LED
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(200);
  setStrip(COLOR_IDLE);
  Serial.println("LED: D1 (GPIO5) — repos bleu");
#endif

  pinMode(RF_RX_PIN, INPUT);

  if (!rf.init()) {
    Serial.println("ERREUR: RF init failed");
  } else {
    Serial.println("RF RX=D2 (GPIO4)  2000 bps");
  }

  setupWiFiAndOta();
}

void loop() {
  ArduinoOTA.handle();
  updateFlash();

  uint8_t buf[RH_ASK_MAX_MESSAGE_LEN];
  uint8_t buflen = sizeof(buf);
  if (rf.recv(buf, &buflen)) {
    buf[buflen] = '\0';
    Serial.print("RF: ");
    Serial.println((char *)buf);
    handleMessage((char *)buf);
  }

  yield();
}
