/*
  Goal ESCLAVE bleu — Drone Soccer (ESP32)
  Dossier : Goal-Bleu  |  Carte : ESP32 Dev Module (shield)

  Recoit du goal ROUGE (maitre) via 433 MHz :
    SYNC:rouge,bleu  — scores a jour
    RST              — remise a zero
    LED:BLU          — but equipe bleue (flash vert)

  === BRANCHEMENTS ===
  RF-5V (depuis FS1000A du maitre)  DATA -> GPIO 4   GND  5V  antenne 17,3 cm
  WS2812B 60 LED                    DATA -> GPIO ?   (verifier decalage shield)
  Pas de Bluetooth — pas d'emetteur RF

  /!\ Ne pas utiliser GPIO 6 a 11 sur ESP32 — flash interne.
  GND commun entre goal rouge et goal bleu.
  RF_BITRATE identique au maitre : 2000 bps.
*/

#include <RH_ASK.h>
#include <SPI.h>
#include <string.h>
#define FASTLED_RMT5 1
#define FASTLED_ALLOW_INTERRUPTS 0
#include <FastLED.h>

#define ENABLE_LED     1
#define RF_BITRATE     2000
#define RF_RX_PIN      4
#define RF_TX_PIN      13   // non branche (RadioHead exige une broche TX)
#define RF_PTT_PIN     13
#define LED_PIN        2     // ajuster selon votre shield
#define NUM_LEDS       60
#define FLASH_MS       2000

RH_ASK rf(RF_BITRATE, RF_RX_PIN, RF_TX_PIN, RF_PTT_PIN);

CRGB leds[NUM_LEDS];
int scoreRed = 0;
int scoreBlue = 0;
int lastScoreBlue = -1;

unsigned long flashStartMs = 0;
bool flashing = false;

CRGB COLOR_IDLE = CRGB::Blue;   // goal bleu : repos
CRGB COLOR_GOAL = CRGB::Green;  // flash vert quand but bleu

void setStrip(CRGB c) {
#if ENABLE_LED
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = c;
  FastLED.show();
  delay(1);
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

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== Goal ESCLAVE bleu ===");

#if ENABLE_LED
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(200);
  FastLED.clear(true);
  setStrip(COLOR_IDLE);
  Serial.print("LED: GPIO");
  Serial.print(LED_PIN);
  Serial.println(" — repos bleu, but = vert 2 s");
#else
  Serial.println("LED: desactivee");
#endif

  pinMode(RF_RX_PIN, INPUT);

  if (!rf.init()) {
    Serial.println("ERREUR: RF init failed");
  } else {
    Serial.print("RF RX=GPIO");
    Serial.print(RF_RX_PIN);
    Serial.print("  ");
    Serial.print(RF_BITRATE);
    Serial.println(" bps — en attente du maitre rouge");
  }
}

void loop() {
  updateFlash();

  uint8_t buf[RH_ASK_MAX_MESSAGE_LEN];
  uint8_t buflen = sizeof(buf);
  if (rf.recv(buf, &buflen)) {
    buf[buflen] = '\0';
    Serial.print("RF: ");
    Serial.println((char *)buf);
    handleMessage((char *)buf);
  }
}
