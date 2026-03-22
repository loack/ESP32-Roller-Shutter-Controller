#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ===== BOUTON RESET WIFI (non configurable : bouton BOOT de l'ESP32) =====
#define RESET_WIFI_BUTTON 0

// ===== MOT DE PASSE ADMIN PAR DÉFAUT (modifiable via l'interface web) =====
#define DEFAULT_ADMIN_PASSWORD "admin"

// ===== VALEURS PAR DÉFAUT DES BROCHES (modifiables via l'interface web) =====
#define DEFAULT_WIEGAND_D0         36
#define DEFAULT_WIEGAND_D1         39
#define DEFAULT_RELAY_OPEN         21
#define DEFAULT_RELAY_CLOSE        29
#define DEFAULT_PHOTO_BARRIER      13
#define DEFAULT_STATUS_LED         2
#define DEFAULT_READER_LED_RED     14
#define DEFAULT_READER_LED_GREEN   12
#define DEFAULT_PIN_UP_SWITCH      32
#define DEFAULT_PIN_DOWN_SWITCH    33

// ===== CONFIGURATION DES BROCHES (instance globale définie dans main.cpp) =====
struct PinConfig {
  uint8_t wiegandD0;
  uint8_t wiegandD1;
  uint8_t relayOpen;
  uint8_t relayClose;
  uint8_t photoBarrier;
  uint8_t statusLed;
  uint8_t readerLedRed;
  uint8_t readerLedGreen;
  uint8_t pinUpSwitch;
  uint8_t pinDownSwitch;
};

extern PinConfig pins;

// ===== STRUCTURES =====
struct AccessCode {
  uint32_t code;
  uint8_t type;  // 0=Wiegand/Keypad, 1=RFID, 2=Fingerprint
  char name[32];
  bool active;
};

// Modes d'identification
#define AUTH_MODE_PIN_ONLY    0  // Code PIN long seul (clavier)
#define AUTH_MODE_RFID_ONLY   1  // Badge RFID seul
#define AUTH_MODE_PIN_RFID    2  // Badge RFID + Code PIN (2FA)

struct Config {
  unsigned long relayDuration;
  bool photoBarrierEnabled;
  char mqttServer[64];
  int mqttPort;
  char mqttUser[32];
  char mqttPassword[32];
  char mqttTopic[64];
  char adminPassword[32];
  uint8_t authMode;      // AUTH_MODE_PIN_ONLY / AUTH_MODE_RFID_ONLY / AUTH_MODE_PIN_RFID
  // Réseau WiFi
  bool useStaticIP;
  char staticIP[16];
  char staticGateway[16];
  char staticSubnet[16];
  bool initialized;
};

struct AccessLog {
  unsigned long timestamp;
  uint32_t code;
  bool granted;
  uint8_t type;
};

#endif
