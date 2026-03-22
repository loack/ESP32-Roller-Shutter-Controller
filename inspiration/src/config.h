#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#define MAX_IOS 20
#define MAX_LOG_LINES 64


// ===== CONFIGURATION PINS =====
#define RELAY_K1        16
#define RELAY_K2       17

// ===== STRUCTURES =====
// Structure for a single configurable I/O pin
struct IOPin {
  uint8_t pin;
  char name[32];
  uint8_t mode; // 0 = DISABLED, 1 = INPUT, 2 = OUTPUT
  uint8_t inputType; // For inputs: 0 = INPUT, 1 = INPUT_PULLUP, 2 = INPUT_PULLDOWN
  bool state;   // Current state (for outputs) or last read state (for inputs)
  bool defaultState; // Default state at boot for outputs
};


struct AccessLog {
  char timestamp[25];
  char ip[16];
  char resource[50];
};

// Maximum number of scheduled commands
#define MAX_SCHEDULED_COMMANDS 10

struct ScheduledCommand {
  bool active;
  int pin;
  int state;
  uint32_t exec_at_sec;  // Unix timestamp en secondes
  uint32_t exec_at_us;   // Microsecondes (0-999999)
};


// Main configuration structure
struct Config {
  char deviceName[32];
  char adminPassword[32];
  
  // Network settings
  bool useEthernet;      // true = Ethernet, false = WiFi
  char ethernetType[16]; // "WT32-ETH01" ou autres types futurs
  bool useStaticIP;
  char staticIP[16];
  char staticGateway[16];
  char staticSubnet[16];

  // MQTT Settings
  char mqttServer[64];
  int mqttPort;
  char mqttUser[32];
  char mqttPassword[32];
  char mqttTopic[32];

  // NTP Settings
  char ntpServer[64];
  long gmtOffset_sec;
  int daylightOffset_sec;

  // Serial Bridge Settings
  bool useSerialBridge;
  int serialRxPin;
  int serialTxPin;
  long serialBaudRate;

  bool initialized;
};

#endif // CONFIG_H
