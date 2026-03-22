#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ===== CONFIGURATION PINS =====
#define WIEGAND_D0        36  // pink
#define WIEGAND_D1        39  //brown
#define RELAY_OPEN        21
#define RELAY_CLOSE       29
#define PHOTO_BARRIER     13
#define STATUS_LED        25
#define READER_LED_RED    14  // LED rouge du lecteur
#define READER_LED_GREEN  12  // LED verte du lecteur

// Broches pour les interrupteurs manuels
#define PIN_UP_SWITCH 25
#define PIN_DOWN_SWITCH 26

// ===== STRUCTURES =====
struct AccessCode {
  uint32_t code;
  uint8_t type;  // 0=Wiegand/Keypad, 1=RFID, 2=Fingerprint
  char name[32];
  bool active;
};

struct Config {
  unsigned long relayDuration;
  bool photoBarrierEnabled;
  char mqttServer[64];
  int mqttPort;
  char mqttUser[32];
  char mqttPassword[32];
  char mqttTopic[64];
  char adminPassword[32];
  bool initialized;
};

struct AccessLog {
  unsigned long timestamp;
  uint32_t code;
  bool granted;
  uint8_t type;
};

#endif
