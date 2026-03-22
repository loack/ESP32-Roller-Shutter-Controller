#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "config.h"
#include "access_control.h"
#include "wiegand_handler.h"

// Bouton pour reset WiFi (bouton BOOT sur ESP32)
#define RESET_WIFI_BUTTON 0

// ===== OBJETS GLOBAUX =====
AsyncWebServer server(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);
Preferences preferences;
WiFiManager wifiManager;

Config config;
unsigned long relayStartTime = 0;
bool relayActive = false;
unsigned long lastMqttReconnect = 0;

// ===== PROTOTYPES =====
void loadConfig();
void saveConfig();
void activateRelay(bool open);
void deactivateRelay();
bool checkTriplePress();
void handleManualSwitches();

// Fonctions externes (web + MQTT)
void setupWebServer();
void setupMQTT();
void reconnectMQTT();
void publishMQTT(const char* topic, const char* payload);

// ===== FONCTION RESET WiFi =====
bool checkTriplePress() {
  int pressCount = 0;
  unsigned long startTime = millis();
  bool lastState = HIGH;
  
  Serial.println("\n⏱ WiFi Reset Check (10 seconds window)...");
  Serial.println("Press BOOT button 3 times to reset WiFi credentials");
  
  while (millis() - startTime < 10000) {  // 10 secondes
    bool currentState = digitalRead(RESET_WIFI_BUTTON);
    
    // Détection front descendant (appui)
    if (lastState == HIGH && currentState == LOW) {
      pressCount++;
      Serial.printf("✓ Press %d/3 detected\n", pressCount);
      
      if (pressCount >= 3) {
        Serial.println("\n🔥 Triple press detected!");
        return true;
      }
      
      delay(50);  // Anti-rebond
    }
    
    lastState = currentState;
    delay(10);
  }
  
  if (pressCount > 0) {
    Serial.printf("Only %d press(es) detected. Reset cancelled.\n", pressCount);
  }
  Serial.println("No reset requested. Continuing...\n");
  return false;
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  delay(1000);  // Attendre la stabilisation du port série
  
  Serial.println("\n\n=== ESP32 Roller Shutter Controller ===");
  Serial.println("Version 1.0 - With Wiegand, RFID & Fingerprint");
  Serial.println("Chip ID: " + String((uint32_t)ESP.getEfuseMac(), HEX));
  Serial.println("SDK Version: " + String(ESP.getSdkVersion()));
  
  // Configuration des pins
  pinMode(RELAY_OPEN, OUTPUT);
  pinMode(RELAY_CLOSE, OUTPUT);
  pinMode(PHOTO_BARRIER, INPUT_PULLUP);
  pinMode(STATUS_LED, OUTPUT);
  pinMode(RESET_WIFI_BUTTON, INPUT_PULLUP);
  pinMode(READER_LED_RED, OUTPUT);
  pinMode(READER_LED_GREEN, OUTPUT);
  
  digitalWrite(RELAY_OPEN, LOW);
  digitalWrite(RELAY_CLOSE, LOW);
  digitalWrite(STATUS_LED, LOW);
  digitalWrite(READER_LED_RED, LOW);
  digitalWrite(READER_LED_GREEN, LOW);

    // Initialisation des interrupteurs manuels
  pinMode(PIN_UP_SWITCH, INPUT_PULLUP);
  pinMode(PIN_DOWN_SWITCH, INPUT_PULLUP);
  
  // ===== CONFIGURATION WiFi EN PREMIER =====
  // Configuration WiFiManager (AVANT les paramètres WiFi)
  wifiManager.setConfigPortalTimeout(180);  // 3 minutes pour configurer
  wifiManager.setConnectTimeout(30);        // 30 secondes pour se connecter
  wifiManager.setConnectRetries(3);         // 3 tentatives de connexion
  wifiManager.setDebugOutput(true);         // Activer le debug
  
  // Vérifier triple appui pour reset WiFi
  if (checkTriplePress()) {
    Serial.println("\n⚠⚠⚠ RESETTING WiFi credentials ⚠⚠⚠");
    wifiManager.resetSettings();
    delay(1000);
    Serial.println("Credentials erased. Restarting...");
    delay(2000);
    ESP.restart();
  }
  
  // Tentative de connexion WiFi
  Serial.println("\n⏱ Starting WiFi configuration...");
  Serial.println("If no saved credentials, access point will start:");
  Serial.println("SSID: ESP32-Roller-Setup");
  Serial.println("No password required");
  Serial.println("Connect and configure WiFi at: http://192.168.4.1\n");
  
  // Configuration WiFi pour compatibilité Freebox (juste avant autoConnect)
  WiFi.setTxPower(WIFI_POWER_19_5dBm);  // Réduire la puissance pour éviter les timeouts
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  
  digitalWrite(STATUS_LED, HIGH);
  
  if (!wifiManager.autoConnect("ESP32-Roller-Setup")) {
    Serial.println("\n✗✗✗ WiFiManager failed to connect ✗✗✗");
    Serial.println("Restarting in 5 seconds...");
    digitalWrite(STATUS_LED, LOW);
    delay(5000);
    ESP.restart();
  }
  
  // Connexion réussie
  Serial.println("\n✓✓✓ WiFi CONNECTED ✓✓✓");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Gateway: ");
  Serial.println(WiFi.gatewayIP());
  Serial.print("RSSI: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
  digitalWrite(STATUS_LED, LOW);
  
  // Arrêter le serveur de configuration WiFiManager pour libérer le port 80
  wifiManager.stopConfigPortal();
  delay(500);  // Attendre la libération du port
  
  // ===== INITIALISATION DES AUTRES COMPOSANTS =====
  setupWiegand();

  preferences.begin("roller", false);
  loadConfig();
  loadAccessCodes();

  setupWebServer();
  setupMQTT();

  server.begin();
  Serial.println("✓ Web server started");
  Serial.println("\n========================================");
  Serial.println("Access the web interface at:");
  Serial.print("http://"); Serial.println(WiFi.localIP());
  Serial.println("========================================\n");

  for (int i = 0; i < 3; i++) {
    digitalWrite(STATUS_LED, HIGH);
    delay(200);
    digitalWrite(STATUS_LED, LOW);
    delay(200);
  }
}

// ===== INTERRUPTEURS MANUELS =====
void handleManualSwitches() {
  static unsigned long lastPressTime = 0;
  const unsigned long debounceDelay = 200;

  if (millis() - lastPressTime > debounceDelay) {
    if (digitalRead(PIN_UP_SWITCH) == LOW) {
      Serial.println("Manual switch: OPEN");
      activateRelay(true);
      lastPressTime = millis();
    } else if (digitalRead(PIN_DOWN_SWITCH) == LOW) {
      Serial.println("Manual switch: CLOSE");
      activateRelay(false);
      lastPressTime = millis();
    }
  }
}

// ===== LOOP =====
void loop() {
  // Vérification connexion WiFi
  static unsigned long lastWiFiCheck = 0;
  if (millis() - lastWiFiCheck > 30000) {  // Toutes les 30 secondes
    lastWiFiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("⚠ WiFi disconnected! Reconnecting...");
      WiFi.reconnect();
    }
  }
  
  // Gestion Wiegand
  handleWiegandInput();
  
  // Gestion relais avec temporisation
  if (relayActive && (millis() - relayStartTime >= config.relayDuration)) {
    deactivateRelay();
  }
  
  // Vérification barrière photoélectrique
  if (config.photoBarrierEnabled && relayActive) {
    if (digitalRead(PHOTO_BARRIER) == LOW) {  // Barrière coupée
      Serial.println("⚠ Photo barrier triggered! Stopping relay.");
      deactivateRelay();
      publishMQTT("status", "{\"event\":\"barrier_triggered\"}");
    }
  }
  
  // Reconnexion MQTT si nécessaire
  if (!mqttClient.connected() && millis() - lastMqttReconnect > 5000) {
    reconnectMQTT();
    lastMqttReconnect = millis();
  }
  
  if (mqttClient.connected()) {
    mqttClient.loop();
  }
  
  handleManualSwitches();

  delay(10);
}

// ===== FONCTIONS CONFIGURATION =====
void loadConfig() {
  config.relayDuration = preferences.getULong("relayDur", 5000);
  config.photoBarrierEnabled = preferences.getBool("photoEn", true);
  config.mqttPort = preferences.getInt("mqttPort", 1883);
  
  preferences.getString("mqttSrv", config.mqttServer, sizeof(config.mqttServer));
  preferences.getString("mqttUser", config.mqttUser, sizeof(config.mqttUser));
  preferences.getString("mqttPass", config.mqttPassword, sizeof(config.mqttPassword));
  preferences.getString("mqttTop", config.mqttTopic, sizeof(config.mqttTopic));
  preferences.getString("adminPw", config.adminPassword, sizeof(config.adminPassword));
  
  if (strlen(config.mqttTopic) == 0) strcpy(config.mqttTopic, "roller");
  if (strlen(config.adminPassword) == 0) strcpy(config.adminPassword, "admin");
  
  config.initialized = preferences.getBool("init", false);
  
  Serial.printf("✓ Config loaded: Relay=%lums, MQTT=%s:%d\n", 
                config.relayDuration, config.mqttServer, config.mqttPort);
}

void saveConfig() {
  preferences.putULong("relayDur", config.relayDuration);
  preferences.putBool("photoEn", config.photoBarrierEnabled);
  preferences.putInt("mqttPort", config.mqttPort);
  preferences.putString("mqttSrv", config.mqttServer);
  preferences.putString("mqttUser", config.mqttUser);
  preferences.putString("mqttPass", config.mqttPassword);
  preferences.putString("mqttTop", config.mqttTopic);
  preferences.putString("adminPw", config.adminPassword);
  preferences.putBool("init", true);
  
  Serial.println("✓ Config saved to flash");
}

// ===== FONCTIONS RELAIS =====
void activateRelay(bool open) {
  // SÉCURITÉ 1: Ne jamais activer les 2 relais simultanément !
  digitalWrite(RELAY_OPEN, LOW);
  digitalWrite(RELAY_CLOSE, LOW);
  delay(100);  // Pause de sécurité
  
  // SÉCURITÉ 2: Vérifier que l'autre relais est bien OFF
  if (open) {
    if (digitalRead(RELAY_CLOSE) == HIGH) {
      Serial.println("⚠ ERREUR: RELAY_CLOSE encore actif!");
      return;
    }
  } else {
    if (digitalRead(RELAY_OPEN) == HIGH) {
      Serial.println("⚠ ERREUR: RELAY_OPEN encore actif!");
      return;
    }
  }
  
  if (relayActive) {
    deactivateRelay();
    delay(100);
  }
  
  digitalWrite(open ? RELAY_OPEN : RELAY_CLOSE, HIGH);
  relayStartTime = millis();
  relayActive = true;
  
  Serial.printf("⚡ Relay activated: %s for %lums\n", 
                open ? "OPEN" : "CLOSE", config.relayDuration);
  
  char payload[128];
  snprintf(payload, sizeof(payload), 
           "{\"action\":\"%s\",\"duration\":%lu}", 
           open ? "open" : "close", config.relayDuration);
  publishMQTT("relay", payload);
}

void deactivateRelay() {
  digitalWrite(RELAY_OPEN, LOW);
  digitalWrite(RELAY_CLOSE, LOW);
  relayActive = false;
  
  Serial.println("⚡ Relay deactivated");
  publishMQTT("relay", "{\"action\":\"stopped\"}");
}

