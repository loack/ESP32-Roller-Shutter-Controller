#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ETH.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFiUdp.h>
#include <SPIFFS.h>
#include <time.h>

#include "config.h"
#include "mqtt.h"
#include "serial_manager.h"

// WebSocket declared in web_server.cpp
extern AsyncWebSocket ws;

// ===== LOGGING GLOBALS =====
String logBuffer[64];
int logBufferIndex = 0;
bool logBufferFull = false;

// Centralized logging: prints to Serial, stores in circular buffer and
// broadcasts to connected WebSocket clients (if any).
void logMessage(const String& message);
void logPrintf(const char* fmt, ...);

// ===== GLOBAL OBJECTS =====
AsyncWebServer server(80);
// WiFiClient and mqttClient are now defined in src/mqtt.cpp
Preferences preferences;
WiFiManager wifiManager;

Config config;
IOPin ioPins[MAX_IOS];
AccessLog accessLogs[100];   // Max 100 logs
int ioPinCount = 0;

ScheduledCommand scheduledCommands[MAX_SCHEDULED_COMMANDS];

unsigned long lastMqttReconnect = 0;

// Ethernet globals
bool ethConnected = false;
void WiFiEvent(WiFiEvent_t event);

// Bouton pour reset WiFi - GPIO39 (disponible sur WT32-ETH01)
#define RESET_WIFI_BUTTON 39
#define STATUS_LED 2  // LED on WT32-ETH01 (GPIO2)

// ===== PROTOTYPES =====
void loadConfig();
void saveConfig();
void loadIOs();
void saveIOs();
void applyIOPinModes();
void handleIOs(void *pvParameters); // Modified for FreeRTOS
void setupWebServer();
void blinkStatusLED(int times, int delayMs);
void processScheduledCommands();
void WiFiEvent(WiFiEvent_t event);
bool initEthernet();

// ===== FreeRTOS Task Handles =====
TaskHandle_t ioTaskHandle = NULL;

// ===== FONCTION RESET WiFi =====
// Fonction pour détecter 3 appuis sur le bouton BOOT
bool checkTriplePress() {
  int pressCount = 0;
  unsigned long startTime = millis();
  unsigned long lastPressTime = 0;
  bool lastState = HIGH;
  
  logMessage("\n⏱ WiFi Reset Check (5 seconds window)...");
  logMessage("Press button on GPIO39 three times to reset WiFi credentials");
  
  while (millis() - startTime < 5000) {  // 5 secondes
    bool currentState = digitalRead(RESET_WIFI_BUTTON);
    
    // Détection front descendant (appui)
    if (lastState == HIGH && currentState == LOW) {
      pressCount++;
      lastPressTime = millis();
      logPrintf("✓ Press %d/3 detected", pressCount);
      
      if (pressCount >= 3) {
        logMessage("\n🔥 Triple press detected!");
        return true;
      }
      
      delay(50);  // Anti-rebond
    }
    
    lastState = currentState;
    delay(10);
  }
  
  if (pressCount > 0) {
    logPrintf("Only %d press(es) detected. Reset cancelled.", pressCount);
  }
  logMessage("No reset requested. Continuing...\n");
  return false;
}

// ===== ETHERNET EVENT HANDLER =====
void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      logMessage("ETH Started");
      ETH.setHostname(config.deviceName);
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      logMessage("ETH Connected");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP: {
      String s = "ETH MAC: " + ETH.macAddress() + ", IPv4: " + ETH.localIP().toString();
      if (ETH.fullDuplex()) s += ", FULL_DUPLEX";
      s += ", " + String(ETH.linkSpeed()) + "Mbps";
      logMessage(s);
      ethConnected = true;
      break;
    }
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      logMessage("ETH Disconnected");
      ethConnected = false;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      logMessage("ETH Stopped");
      ethConnected = false;
      break;
    default:
      break;
  }
}

// ===== ETHERNET INITIALIZATION =====
bool initEthernet() {
  logMessage("\n=== Initializing Ethernet ===");
  
  if (strcmp(config.ethernetType, "WT32-ETH01") == 0) {
    logMessage("Board: WT32-ETH01");
    
    WiFi.onEvent(WiFiEvent);
    
    // WT32-ETH01 pinout - using #undef to avoid redefinition warnings
    #undef ETH_PHY_TYPE
    #undef ETH_PHY_ADDR
    #undef ETH_PHY_MDC
    #undef ETH_PHY_MDIO
    #undef ETH_PHY_POWER
    #undef ETH_CLK_MODE
    
    #define ETH_PHY_TYPE ETH_PHY_LAN8720
    #define ETH_PHY_ADDR 1
    #define ETH_PHY_MDC 23
    #define ETH_PHY_MDIO 18
    #define ETH_PHY_POWER 16
    #define ETH_CLK_MODE ETH_CLOCK_GPIO0_IN
    
    if (config.useStaticIP) {
      IPAddress localIP, gateway, subnet, dns1(8, 8, 8, 8);
      localIP.fromString(config.staticIP);
      gateway.fromString(config.staticGateway);
      subnet.fromString(config.staticSubnet);
      
      if (!ETH.begin(ETH_PHY_ADDR, ETH_PHY_POWER, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_TYPE, ETH_CLK_MODE)) {
        logMessage("ETH start failed");
        return false;
      }
      
      if (!ETH.config(localIP, gateway, subnet, dns1)) {
        logMessage("ETH config failed");
        return false;
      }
    } else {
      if (!ETH.begin(ETH_PHY_ADDR, ETH_PHY_POWER, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_TYPE, ETH_CLK_MODE)) {
        logMessage("ETH start failed");
        return false;
      }
    }
    
    // Wait for connection with improved feedback
    unsigned long startAttemptTime = millis();
    logMessage("Waiting for Ethernet connection");
    while (!ethConnected && millis() - startAttemptTime < 15000) {
      delay(500);
      Serial.print(".");
      blinkStatusLED(1, 50);
    }
    Serial.println();
    
    if (ethConnected) {
      logMessage("✓✓✓ ETHERNET CONNECTED ✓✓✓");
      logMessage("IP Address: " + ETH.localIP().toString());
      logMessage("Gateway: " + ETH.gatewayIP().toString());
      return true;
    } else {
      logMessage("✗ Ethernet connection timeout (no link detected)");
      logMessage("Check: Cable connection, router port, PHY power");
      return false;
    }
  }
  
  logMessage("✗ Unknown Ethernet type");
  return false;
}

// ===== SETUP =====

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Initialize scheduled commands queue
  for (int i = 0; i < MAX_SCHEDULED_COMMANDS; i++) {
    scheduledCommands[i].active = false;
  }

  logMessage("\n\n=== ESP32 Generic IO Controller ===");
  logMessage("Version 1.0 - WT32-ETH01");
  logMessage(String("Chip ID: ") + String((uint32_t)ESP.getEfuseMac(), HEX));
  logMessage(String("SDK Version: ") + String(ESP.getSdkVersion()));

  pinMode(STATUS_LED, OUTPUT);
  blinkStatusLED(3, 200);

  // Load configuration from flash
  preferences.begin("generic-io", false);
  
  // Check WiFi connection failure counter
  int wifiFailCount = preferences.getInt("wifiFailCount", 0);
  logPrintf("WiFi failure count: %d/3", wifiFailCount);
  
  if (wifiFailCount >= 3) {
    logMessage("\n⚠️⚠️⚠️ TOO MANY WiFi FAILURES ⚠️⚠️⚠️");
    logMessage("Resetting WiFi credentials...");
    wifiManager.resetSettings();
    preferences.putInt("wifiFailCount", 0);
    delay(2000);
    logMessage("WiFi reset complete. Restarting...");
    ESP.restart();
  }
  
  loadConfig();
  
  // Force Ethernet type sur WT32-ETH01
  if (strlen(config.ethernetType) == 0) {
    strcpy(config.ethernetType, "WT32-ETH01");
  }
  if (!config.initialized) {
    config.initialized = true;
    saveConfig();
    logMessage("First boot detected - Configuration initialized");
  }
  
  loadIOs();
  logMessage("Configuration and I/O settings loaded.");
  blinkStatusLED(2, 100);

  // Initialize Serial Bridge (BEFORE applying IO modes so IO modes can override)
  serialManager.begin();
  if (config.useSerialBridge) {
    logMessage("Serial Bridge is enabled.");
  } else {
    logMessage("Serial Bridge is disabled.");
  }

  // Apply I/O pin configurations
  applyIOPinModes();
  logMessage("I/O pin configurations applied.");
  blinkStatusLED(2, 100);

  // ===== NETWORK INITIALIZATION =====
  bool networkConnected = false;
  
  // TOUJOURS essayer Ethernet en premier sur WT32-ETH01
  logMessage("\n🌐 Attempting Ethernet connection (WT32-ETH01)...");
  
  if (initEthernet()) {
    // Ethernet OK
    networkConnected = true;
    config.useEthernet = true;
    digitalWrite(STATUS_LED, LOW);
    logMessage("✓ Using Ethernet as primary network");
  } else {
    // Ethernet FAILED - Basculer vers WiFi
    logMessage("⚠️ Ethernet failed, switching to WiFi fallback");
    
    config.useEthernet = false;
    
    // MODE WiFi - Configure GPIO39 pour le bouton de reset
    pinMode(RESET_WIFI_BUTTON, INPUT_PULLUP);
    delay(100);  // Stabiliser le pull-up
    
    // Check for WiFi reset (triple press) - seulement en mode WiFi fallback
    if (checkTriplePress()) {
      logMessage("\n⚠⚠⚠ RESETTING WiFi credentials ⚠⚠⚠");
      wifiManager.resetSettings();
      preferences.putInt("wifiFailCount", 0);
      delay(1000);
      logMessage("Credentials erased. Restarting...");
      delay(2000);
      ESP.restart();
    }
    
    // Configuration WiFiManager (AVANT les paramètres WiFi)
    wifiManager.setConfigPortalTimeout(180);  // 3 minutes pour configurer
    wifiManager.setConnectTimeout(30);        // 30 secondes pour se connecter
    wifiManager.setConnectRetries(3);         // 3 tentatives de connexion
    wifiManager.setDebugOutput(true);         // Activer le debug
    
    // Tentative de connexion WiFi
    logMessage("\n⏱ Starting WiFi configuration...");
    logMessage("If no saved credentials, access point will start:");
    logMessage("SSID: ESP32-Roller-Setup");
    logMessage("No password required");
    logMessage("Connect and configure WiFi at: http://192.168.4.1\n");
    
    // Configuration WiFi pour compatibilité Freebox (juste avant autoConnect)
   // WiFi.setTxPower(WIFI_POWER_19_5dBm);  // Réduire la puissance pour éviter les timeouts
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    
    if (config.useStaticIP) {
      IPAddress localIP, gateway, subnet, dns1(8, 8, 8, 8);
      localIP.fromString(config.staticIP);
      gateway.fromString(config.staticGateway);
      subnet.fromString(config.staticSubnet);
      if (WiFi.config(localIP, gateway, subnet, dns1) == false) {
        logMessage("⚠️ Static IP Configuration Failed");
      } else {
        logMessage(String("✓ Static IP configured: ") + localIP.toString());
      }
    }
    
    // Faire clignoter la LED pendant la tentative de connexion
    blinkStatusLED(5, 100);
    
    if (!wifiManager.autoConnect((String(config.deviceName) + "-Setup").c_str())) {
      logMessage("\n✗✗✗ WiFiManager failed to connect ✗✗✗");
      
      // Incrémenter le compteur d'échecs
      int failCount = preferences.getInt("wifiFailCount", 0);
      failCount++;
      preferences.putInt("wifiFailCount", failCount);
      logPrintf("WiFi failure count incremented to: %d/3", failCount);
      
      logMessage("Restarting in 5 seconds...");
      
      // Clignoter rapidement la LED pour indiquer l'échec
      blinkStatusLED(10, 250);
      
      ESP.restart();
    }
    
    // Connexion réussie - réinitialiser le compteur d'échecs
    preferences.putInt("wifiFailCount", 0);
    blinkStatusLED(3, 100);  // Signal de succès
    logMessage("\n✓✓✓ WiFi CONNECTED ✓✓✓");
    logMessage(String("IP Address: ") + WiFi.localIP().toString());
    
    // === OPTIMISATION LATENCE ===
    // Désactiver le mode économie d'énergie du WiFi pour réduire la latence du ping
    WiFi.setSleep(false);
    logMessage("✓ WiFi power-saving mode disabled to reduce latency.");
    // ==========================

    logMessage(String("Gateway: ") + WiFi.gatewayIP().toString());
    logPrintf("RSSI: %d dBm", WiFi.RSSI());
    digitalWrite(STATUS_LED, LOW);
    networkConnected = true;
  }
  
  // Arrêter le serveur de configuration WiFiManager pour libérer le port 80 (seulement en mode WiFi)
  if (!config.useEthernet) {
    wifiManager.stopConfigPortal();
    delay(500);  // Attendre la libération du port
    logMessage("✓ Config portal stopped to free port 80");
  }

  // Initialize SPIFFS AVANT de configurer le serveur web
  if(!SPIFFS.begin(true)){
    logMessage("An Error has occurred while mounting SPIFFS");
    return;
  }
  logMessage("SPIFFS mounted successfully.");

  // Setup Web Server (configure toutes les routes)
  setupWebServer();

  // Setup MQTT
  setupMQTT();
  if (strlen(config.mqttServer) > 0) {
    logMessage("MQTT configuration found, enabling MQTT.");
    mqttEnabled = true;
    blinkStatusLED(2, 100);  // Signal MQTT activé
  }

  // === DÉMARRAGE TÂCHE I/O ===
  xTaskCreatePinnedToCore(
      handleIOs,        
      "IOTask",         
      4096,             
      NULL,             
      1,                
      &ioTaskHandle,    
      0);               

  // Démarrage du serveur web (UNE SEULE FOIS, après avoir configuré toutes les routes)
  server.begin();
  String ipAddress = config.useEthernet ? ETH.localIP().toString() : WiFi.localIP().toString();
  logMessage("✓ Web server started");
  logMessage("\n========================================");
  logMessage("Access the web interface at:");
  logMessage(String("http://") + ipAddress);
  logMessage("========================================\n");
  
  blinkStatusLED(1, 500);  // Signal de démarrage complet
}


// ===== LOOP =====
void loop() {
  // The main loop is now responsible for high-frequency tasks only.
  // I/O handling is moved to a separate FreeRTOS task.

  processScheduledCommands();
  serialManager.loop();

  // Check network connection (WiFi or Ethernet)
  bool networkOk = config.useEthernet ? ethConnected : (WiFi.status() == WL_CONNECTED);
  
  if (networkOk) {
    if (mqttEnabled) {
      if (!mqttClient.connected()) {
        long now = millis();
        // Attempt to reconnect every 5 seconds if disconnected.
        if (now - lastMqttReconnect > 5000) {
          lastMqttReconnect = now;
          reconnectMQTT();
        }
      }
      // This should be called as often as possible.
      mqttClient.loop();
    }
  }

  // ElegantOTA loop for web updates.
  ElegantOTA.loop();

  // A small delay can be added here if needed to prevent watchdog timeouts,
  // but it should be as small as possible (e.g., 1ms) or removed entirely
  // if other tasks yield frequently enough.
  delay(1);
}

void processScheduledCommands() {
  // Obtenir le temps actuel avec précision microseconde
  struct timeval tv;
  gettimeofday(&tv, NULL);
  uint64_t currentTimeUs = (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
  
  for (int i = 0; i < MAX_SCHEDULED_COMMANDS; i++) {
    if (scheduledCommands[i].active) {
      // Calculer le temps d'exécution prévu en microsecondes
      uint64_t execTimeUs = ((uint64_t)scheduledCommands[i].exec_at_sec * 1000000ULL) + 
                             (uint64_t)scheduledCommands[i].exec_at_us;
      
      // Vérifier si le moment d'exécution est arrivé
      if (currentTimeUs >= execTimeUs) {
        // Calculer le délai d'exécution (peut être négatif si en avance)
        int64_t delay_us = (int64_t)currentTimeUs - (int64_t)execTimeUs;
        
        // Exécuter la commande
        executeCommand(scheduledCommands[i].pin, scheduledCommands[i].state);
        
        // Désactiver cette commande
        scheduledCommands[i].active = false;
        
        // Afficher le délai en millisecondes avec 3 décimales
        double delay_ms = delay_us / 1000.0;
        logPrintf("⏰ Scheduled command executed (delay: %.3f ms)", delay_ms);
      }
    }
  }
}

// ===== CONFIGURATION FUNCTIONS =====
void loadConfig() {
  preferences.getString("deviceName", config.deviceName, sizeof(config.deviceName));
  if (strlen(config.deviceName) == 0) strcpy(config.deviceName, "esp32-eth01");

  config.useEthernet = preferences.getBool("useEthernet", true);  // Default to Ethernet for WT32-ETH01
  preferences.getString("ethType", config.ethernetType, sizeof(config.ethernetType));
  if (strlen(config.ethernetType) == 0) strcpy(config.ethernetType, "WT32-ETH01");
  
  config.useStaticIP = preferences.getBool("useStaticIP", false);
  preferences.getString("staticIP", config.staticIP, sizeof(config.staticIP));
  preferences.getString("staticGW", config.staticGateway, sizeof(config.staticGateway));
  preferences.getString("staticSN", config.staticSubnet, sizeof(config.staticSubnet));

  preferences.getString("adminPw", config.adminPassword, sizeof(config.adminPassword));
  if (strlen(config.adminPassword) == 0) strcpy(config.adminPassword, "admin");

  preferences.getString("mqttSrv", config.mqttServer, sizeof(config.mqttServer));
  config.mqttPort = preferences.getInt("mqttPort", 1883);
  preferences.getString("mqttUser", config.mqttUser, sizeof(config.mqttUser));
  preferences.getString("mqttPass", config.mqttPassword, sizeof(config.mqttPassword));
  preferences.getString("mqttTop", config.mqttTopic, sizeof(config.mqttTopic));
  if (strlen(config.mqttTopic) == 0) {
    snprintf(config.mqttTopic, sizeof(config.mqttTopic), "%s/io", config.deviceName);
  }

  // NTP settings are now for display and offset, not for server connection
  config.gmtOffset_sec = preferences.getLong("gmtOffset", 3600);
  config.daylightOffset_sec = preferences.getInt("daylightOff", 3600);

  config.useSerialBridge = preferences.getBool("useSerial", false);
  config.serialRxPin = preferences.getInt("serRx", 4);
  config.serialTxPin = preferences.getInt("serTx", 5);
  config.serialBaudRate = preferences.getLong("serBaud", 9600);

  config.initialized = preferences.getBool("init", false);
  logMessage("Configuration loaded.");
}

void saveConfig() {
  preferences.putString("deviceName", config.deviceName);
  preferences.putBool("useEthernet", config.useEthernet);
  preferences.putString("ethType", config.ethernetType);
  preferences.putBool("useStaticIP", config.useStaticIP);
  preferences.putString("staticIP", config.staticIP);
  preferences.putString("staticGW", config.staticGateway);
  preferences.putString("staticSN", config.staticSubnet);
  
  preferences.putString("adminPw", config.adminPassword);
  preferences.putString("mqttSrv", config.mqttServer);
  preferences.putInt("mqttPort", config.mqttPort);
  preferences.putString("mqttUser", config.mqttUser);
  preferences.putString("mqttPass", config.mqttPassword);
  preferences.putString("mqttTop", config.mqttTopic);
  //preferences.putString("ntpSrv", config.ntpServer); // No longer needed
  preferences.putLong("gmtOffset", config.gmtOffset_sec);
  preferences.putInt("daylightOff", config.daylightOffset_sec);
  
  preferences.putBool("useSerial", config.useSerialBridge);
  preferences.putInt("serRx", config.serialRxPin);
  preferences.putInt("serTx", config.serialTxPin);
  preferences.putLong("serBaud", config.serialBaudRate);

  preferences.putBool("init", true);
  logMessage("Configuration saved.");
}

void loadIOs() {
  ioPinCount = preferences.getInt("ioCount", 0);
  if (ioPinCount > MAX_IOS) ioPinCount = 0;
  for (int i = 0; i < ioPinCount; i++) {
    String key = "io" + String(i);
    preferences.getBytes(key.c_str(), &ioPins[i], sizeof(IOPin));
  }
  logPrintf("Loaded %d I/O pin configurations.", ioPinCount);
}

void saveIOs() {
  preferences.putInt("ioCount", ioPinCount);
  for (int i = 0; i < ioPinCount; i++) {
    String key = "io" + String(i);
    preferences.putBytes(key.c_str(), &ioPins[i], sizeof(IOPin));
  }
  logPrintf("Saved %d I/O pin configurations.", ioPinCount);
}

void applyIOPinModes() {

    pinMode(STATUS_LED, OUTPUT); // Définit GPIO 2 comme une sortie (LED sur WT32-ETH01)
    for (int i = 0; i < ioPinCount; i++) {
        if (ioPins[i].mode == 1) { // INPUT
            // Apply the selected input type
            switch (ioPins[i].inputType) {
                case 0:
                    pinMode(ioPins[i].pin, INPUT);
                    logPrintf("Pin %d (%s) configured as INPUT", ioPins[i].pin, ioPins[i].name);
                    break;
                case 1:
                    pinMode(ioPins[i].pin, INPUT_PULLUP);
                    logPrintf("Pin %d (%s) configured as INPUT_PULLUP", ioPins[i].pin, ioPins[i].name);
                    break;
                case 2:
                    pinMode(ioPins[i].pin, INPUT_PULLDOWN);
                    logPrintf("Pin %d (%s) configured as INPUT_PULLDOWN", ioPins[i].pin, ioPins[i].name);
                    break;
                default:
                    pinMode(ioPins[i].pin, INPUT_PULLUP); // Default fallback
                    logPrintf("Pin %d (%s) configured as INPUT_PULLUP (default)", ioPins[i].pin, ioPins[i].name);
                    break;
            }
        } else if (ioPins[i].mode == 2) { // OUTPUT
            pinMode(ioPins[i].pin, OUTPUT);
            digitalWrite(ioPins[i].pin, ioPins[i].defaultState);
            logPrintf("Pin %d (%s) configured as OUTPUT", ioPins[i].pin, ioPins[i].name);
        }
    }
    logMessage("I/O pin modes applied.");
}


// ===== I/O HANDLING (FreeRTOS Task) =====
void handleIOs(void *pvParameters) {
  logMessage("✅ I/O handling task started.");

  for (;;) { // Infinite loop for the task
    for (int i = 0; i < ioPinCount; i++) {
      if (ioPins[i].mode == 1) { // INPUT
        bool currentState = digitalRead(ioPins[i].pin);

        // Détection immédiate du changement d'état (sans debounce)
        if (currentState != ioPins[i].state) {
          ioPins[i].state = currentState;
          logPrintf("Input '%s' (pin %d) changed to %s", ioPins[i].name, ioPins[i].pin, currentState ? "HIGH" : "LOW");
          
          char topic[128];
          snprintf(topic, sizeof(topic), "%s/status/%s", config.deviceName, ioPins[i].name);
          
          // Format JSON avec timestamp (comme pour les commandes)
          struct timeval tv;
          gettimeofday(&tv, NULL);
          uint64_t timeUs = (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
          
          JsonDocument doc;
          doc["state"] = currentState ? 1 : 0;
          doc["timestamp"] = (uint32_t)(timeUs / 1000000ULL);
          doc["us"] = (uint32_t)(timeUs % 1000000ULL);

          char payload[128];
          serializeJson(doc, payload);

          if (mqttEnabled && mqttClient.connected()) {
            publishMQTT(topic, payload);
          }
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1)); // Check inputs every 1ms (réactivité maximale)
  }
}

// ===== MQTT FUNCTIONS =====
// NOTE: MQTT implementation moved to src/mqtt.cpp
// The original implementation has been removed from this file to avoid
// duplicate symbols. See src/mqtt.cpp and include "mqtt.h" for the API.

void blinkStatusLED(int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(STATUS_LED, HIGH);
    delay(delayMs);
    digitalWrite(STATUS_LED, LOW);
    delay(delayMs);
  }
}


// ===== CENTRAL LOGGING IMPLEMENTATION =====
void logMessage(const String& message) {
  // Print to serial
  Serial.println(message);

  // Store in circular buffer
  logBuffer[logBufferIndex] = message;
  logBufferIndex++;
  if (logBufferIndex >= (int)(sizeof(logBuffer) / sizeof(logBuffer[0]))) {
    logBufferIndex = 0;
    logBufferFull = true;
  }

    // Broadcast via WebSocket if available
    // Compose a small JSON payload {type: "sys_log", timestamp: "HH:MM:SS", message: "..."}
    if (ws.count() > 0) {
      JsonDocument doc;
      doc["type"] = "sys_log";

      // add a timestamp so UI can display it
      time_t now;
      time(&now);
      struct tm timeinfo;
      localtime_r(&now, &timeinfo);
      char timeStr[20];
      strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
      doc["timestamp"] = timeStr;

      doc["message"] = message;
      char out[256];
      size_t n = serializeJson(doc, out, sizeof(out));
      ws.textAll(out, n);
    }
}

void logPrintf(const char* fmt, ...) {
  char buf[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  logMessage(String(buf));
}
