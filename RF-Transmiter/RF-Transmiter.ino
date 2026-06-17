/*
  Manette 1 — Drone Soccer (Pro Mini LGT8F328P)

  Carte : LGT8F328P (coeur lgt8fx)

  FS1000A -> D4 (DATA), GND, 5V, antenne ~17,3 cm
  Boutons (autre patte -> GND) : D5=+  D2=-  D7=reset
  LED RVB anode commune (+ sur 5V) : D12=R  D13=V  D10=B

  Messages RF : SCP  SCM  RST

  MODE 1 = test recepteur (autre Arduino), RF-5V DATA -> D11
*/

#define MODE 0

#include <RH_ASK.h>
#include <SPI.h>
#include <string.h>

#define RF_BITRATE 2000

#if MODE == 0

#define BTN_PLUS  5
#define BTN_MINUS 2
#define BTN_RESET 7
#define LED_R 12
#define LED_G 13
#define LED_B 10
#define RF_TX_PIN 4
#define COMMON_ANODE 1
#define BTN_DEBOUNCE_MS 400

RH_ASK rf(RF_BITRATE, 11, RF_TX_PIN);
unsigned long lastSend = 0;

void setLed(bool r, bool g, bool b) {
#if COMMON_ANODE
  digitalWrite(LED_R, r ? LOW : HIGH);
  digitalWrite(LED_G, g ? LOW : HIGH);
  digitalWrite(LED_B, b ? LOW : HIGH);
#else
  digitalWrite(LED_R, r ? HIGH : LOW);
  digitalWrite(LED_G, g ? HIGH : LOW);
  digitalWrite(LED_B, b ? HIGH : LOW);
#endif
}

void ledOff()   { setLed(false, false, false); }
void ledVert()  { setLed(false, true,  false); }
void ledRouge() { setLed(true,  false, false); }
void ledBleu()  { setLed(false, false, true);  }

void rfSend(const char *msg) {
  rf.send((uint8_t *)msg, strlen(msg));
  rf.waitPacketSent();
}

bool boutonOk(int pin, unsigned long now) {
  if (digitalRead(pin) != LOW) return false;
  if (now - lastSend < BTN_DEBOUNCE_MS) return false;
  delay(25);
  return digitalRead(pin) == LOW;
}

void attendreRelachement(int pin) {
  while (digitalRead(pin) == LOW) delay(10);
}

void envoyer(int pin, const char *msg, void (*couleur)()) {
  couleur();
  Serial.print("Envoi ");
  Serial.println(msg);
  rfSend(msg);
  lastSend = millis();
  attendreRelachement(pin);
  ledOff();
}

void setup() {
  pinMode(BTN_PLUS, INPUT_PULLUP);
  pinMode(BTN_MINUS, INPUT_PULLUP);
  pinMode(BTN_RESET, INPUT_PULLUP);
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  pinMode(RF_TX_PIN, OUTPUT);
  pinMode(11, INPUT_PULLUP);
  ledOff();

  Serial.begin(9600);
  if (!rf.init()) Serial.println("ERREUR: RF init failed");
  else Serial.println("Manette prete");
}

void loop() {
  unsigned long now = millis();
  if (boutonOk(BTN_PLUS, now)) {
    envoyer(BTN_PLUS, "SCP", ledVert);
    return;
  }
  if (boutonOk(BTN_MINUS, now)) {
    envoyer(BTN_MINUS, "SCM", ledRouge);
    return;
  }
  if (boutonOk(BTN_RESET, now)) {
    envoyer(BTN_RESET, "RST", ledBleu);
  }
}

#else

#define PIN_RX 11
RH_ASK rf(RF_BITRATE, PIN_RX, 12);

void setup() {
  Serial.begin(9600);
  if (!rf.init()) Serial.println("ERREUR: RF init failed");
  else Serial.println("Recepteur test — en attente...");
}

void loop() {
  uint8_t buf[RH_ASK_MAX_MESSAGE_LEN];
  uint8_t buflen = sizeof(buf);
  if (rf.recv(buf, &buflen)) {
    buf[buflen] = '\0';
    Serial.print("Recu: ");
    Serial.println((char *)buf);
  }
}

#endif
