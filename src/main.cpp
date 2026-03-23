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
#include "log_manager.h"

// Bouton pour reset WiFi (bouton BOOT sur ESP32)
#define RESET_WIFI_BUTTON 0

// ===== OBJETS GLOBAUX =====
AsyncWebServer server(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);
Preferences preferences;
WiFiManager wifiManager;

Config config;
PinConfig pins;
unsigned long relayStartTime = 0;
bool relayActive = false;
bool manualRelayActive = false;  // true = relais commandé par interrupteur manuel (sans temporisation)
unsigned long lastMqttReconnect = 0;

// ===== PROTOTYPES =====
void loadConfig();
void saveConfig();
void loadPinConfig();
void savePinConfig();
void resetWifiAndRestart();
void activateRelay(bool open);
void deactivateRelay();
bool checkTriplePress();
void handleManualSwitches();

// Fonctions externes (web + MQTT)
void setupWebServer();
void loopWebSocket();
void setupMQTT();
void reconnectMQTT();
void publishMQTT(const char* topic, const char* payload);

// ===== FONCTION RESET WiFi =====
bool checkTriplePress() {
  int pressCount = 0;
  unsigned long startTime = millis();
  bool lastState = HIGH;
  
  logMessage("[WIFI] Vérification reset (10s) - appuyez 3x sur BOOT pour effacer les identifiants");
  
  while (millis() - startTime < 10000) {  // 10 secondes
    bool currentState = digitalRead(RESET_WIFI_BUTTON);
    
    // Détection front descendant (appui)
    if (lastState == HIGH && currentState == LOW) {
      pressCount++;
      logPrintf("[WIFI] Appui %d/3 détecté", pressCount);
      
      if (pressCount >= 3) {
        logMessage("[WIFI] Triple appui détecté !");
        return true;
      }
      
      delay(50);  // Anti-rebond
    }
    
    lastState = currentState;
    delay(10);
  }
  
  if (pressCount > 0) {
    logPrintf("[WIFI] %d appui(s) détecté(s), reset annulé", pressCount);
  }
  return false;
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  delay(1000);  // Attendre la stabilisation du port série
  
  initLogManager();

  logMessage("\n=== ESP32 Roller Shutter Controller ===");
  logMessage("Version 1.0 - With Wiegand, RFID & Fingerprint");
  logPrintf("[SYS] Chip ID: %08X", (uint32_t)ESP.getEfuseMac());
  logPrintf("[SYS] SDK: %s", ESP.getSdkVersion());

  // ===== CHARGEMENT ANTICIPÉ DE LA CONFIG (nécéssaire avant WiFi) =====
  preferences.begin("roller", false);
  loadPinConfig();
  loadConfig();

  // Configuration des pins (numéros chargés depuis la flash)
  pinMode(pins.relayOpen, OUTPUT);
  pinMode(pins.relayClose, OUTPUT);
  pinMode(pins.photoBarrier, INPUT_PULLUP);
  pinMode(pins.statusLed, OUTPUT);
  pinMode(RESET_WIFI_BUTTON, INPUT_PULLUP);
  pinMode(pins.readerLedRed, OUTPUT);
  pinMode(pins.readerLedGreen, OUTPUT);

  digitalWrite(pins.relayOpen, LOW);
  digitalWrite(pins.relayClose, LOW);
  digitalWrite(pins.statusLed, LOW);
  digitalWrite(pins.readerLedRed, LOW);
  digitalWrite(pins.readerLedGreen, LOW);

  // Initialisation des interrupteurs manuels
  pinMode(pins.pinUpSwitch, INPUT_PULLUP);
  pinMode(pins.pinDownSwitch, INPUT_PULLUP);
  
  // ===== CONFIGURATION WiFi EN PREMIER =====
  // Configuration WiFiManager (AVANT les paramètres WiFi)
  wifiManager.setConfigPortalTimeout(180);  // 3 minutes pour configurer
  wifiManager.setConnectTimeout(30);        // 30 secondes pour se connecter
  wifiManager.setConnectRetries(3);         // 3 tentatives de connexion
  wifiManager.setDebugOutput(true);         // Activer le debug
  
  // Vérifier triple appui pour reset WiFi
  if (checkTriplePress()) {
    logMessage("[WIFI] Réinitialisation des identifiants WiFi...");
    wifiManager.resetSettings();
    delay(2000);
    ESP.restart();
  }
  logMessage("[WIFI] Démarrage de la connexion... (AP: ESP32-Roller-Setup si pas de config)");

  // Appliquer l'IP fixe avant connexion (si configurée)
  if (config.useStaticIP && strlen(config.staticIP) > 0) {
    IPAddress ip, gw, sn;
    if (ip.fromString(config.staticIP) &&
        gw.fromString(config.staticGateway) &&
        sn.fromString(config.staticSubnet)) {
      WiFi.config(ip, gw, sn);
      logPrintf("[WIFI] IP fixe: %s / GW: %s / Masque: %s",
               config.staticIP, config.staticGateway, config.staticSubnet);
    } else {
      logMessage("[WIFI] IP fixe invalide — utilisation DHCP");
    }
  }

  // Configuration WiFi pour compatibilité Freebox (juste avant autoConnect)
  WiFi.setTxPower(WIFI_POWER_19_5dBm);  // Réduire la puissance pour éviter les timeouts
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  
  digitalWrite(pins.statusLed, HIGH);
  
  if (!wifiManager.autoConnect("ESP32-Roller-Setup")) {
    logMessage("[WIFI] Connexion échouée, redémarrage...");
    digitalWrite(pins.statusLed, LOW);
    delay(5000);
    ESP.restart();
  }

  // Connexion réussie
  logMessage("[WIFI] Connecté !");
  logPrintf("[WIFI] IP: %s", WiFi.localIP().toString().c_str());
  logPrintf("[WIFI] Passerelle: %s", WiFi.gatewayIP().toString().c_str());
  logPrintf("[WIFI] RSSI: %d dBm", WiFi.RSSI());
  logPrintf("[WIFI] SSID: %s", WiFi.SSID().c_str());
  digitalWrite(pins.statusLed, LOW);
  
  // Arrêter le serveur de configuration WiFiManager pour libérer le port 80
  wifiManager.stopConfigPortal();
  delay(500);  // Attendre la libération du port
  
  // ===== INITIALISATION DES AUTRES COMPOSANTS =====
  setupWiegand();

  // preferences déjà ouvertes (appelées en début de setup)
  loadAccessCodes();

  setupWebServer();
  setupMQTT();

  server.begin();
  logMessage("[WEB] Serveur démarré sur http://" + WiFi.localIP().toString());

  for (int i = 0; i < 3; i++) {
    digitalWrite(pins.statusLed, HIGH);
    delay(200);
    digitalWrite(pins.statusLed, LOW);
    delay(200);
  }
}

// ===== INTERRUPTEURS MANUELS =====
// Comportement : maintenir = relais actif, relâcher = relais coupé (pas de temporisation)
void handleManualSwitches() {
  static bool lastUpState   = HIGH;
  static bool lastDownState = HIGH;
  static unsigned long lastChangeTime = 0;
  const unsigned long debounceDelay = 50;

  if (millis() - lastChangeTime < debounceDelay) return;

  bool upPressed   = (digitalRead(pins.pinUpSwitch)   == LOW);
  bool downPressed = (digitalRead(pins.pinDownSwitch) == LOW);

  bool upChanged   = (upPressed   != (lastUpState   == LOW));
  bool downChanged = (downPressed != (lastDownState == LOW));

  if (!upChanged && !downChanged) return;
  lastChangeTime = millis();

  // Sécurité : si les deux sont actifs simultanément, tout arrêter
  if (upPressed && downPressed) {
    digitalWrite(pins.relayOpen,  LOW);
    digitalWrite(pins.relayClose, LOW);
    if (manualRelayActive) {
      manualRelayActive = false;
      relayActive = false;
      logMessage("[SW] Conflit interrupteurs - arrêt sécurité");
      publishMQTT("relay", "{\"action\":\"stopped\"}");
    }
    lastUpState   = LOW;
    lastDownState = LOW;
    return;
  }

  if (upChanged) {
    if (upPressed) {
      // Montée : s'assurer que la descente est inactive, puis activer la montée
      digitalWrite(pins.relayClose, LOW);
      delay(50);
      digitalWrite(pins.relayOpen, HIGH);
      manualRelayActive = true;
      relayActive = true;
      logMessage("[SW] Interrupteur manuel: OUVRIR (maintenu)");
      publishMQTT("relay", "{\"action\":\"open\",\"source\":\"switch\"}");
    } else {
      // Relâché : couper le relais montée
      digitalWrite(pins.relayOpen, LOW);
      manualRelayActive = false;
      relayActive = false;
      logMessage("[SW] Interrupteur manuel: ARRÊT ouverture");
      publishMQTT("relay", "{\"action\":\"stopped\"}");
    }
    lastUpState = upPressed ? LOW : HIGH;
  }

  if (downChanged) {
    if (downPressed) {
      // Descente : s'assurer que la montée est inactive, puis activer la descente
      digitalWrite(pins.relayOpen, LOW);
      delay(50);
      digitalWrite(pins.relayClose, HIGH);
      manualRelayActive = true;
      relayActive = true;
      logMessage("[SW] Interrupteur manuel: FERMER (maintenu)");
      publishMQTT("relay", "{\"action\":\"close\",\"source\":\"switch\"}");
    } else {
      // Relâché : couper le relais descente
      digitalWrite(pins.relayClose, LOW);
      manualRelayActive = false;
      relayActive = false;
      logMessage("[SW] Interrupteur manuel: ARRÊT fermeture");
      publishMQTT("relay", "{\"action\":\"stopped\"}");
    }
    lastDownState = downPressed ? LOW : HIGH;
  }
}

// ===== LOOP =====
void loop() {
  loopWebSocket();

  // Vérification connexion WiFi
  static unsigned long lastWiFiCheck = 0;
  if (millis() - lastWiFiCheck > 30000) {  // Toutes les 30 secondes
    lastWiFiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      logMessage("[WIFI] Déconnecté, tentative de reconnexion...");
      WiFi.reconnect();
    }
  }
  
  // Gestion Wiegand
  handleWiegandInput();
  
  // Gestion relais avec temporisation (désactivée si contrôlé par interrupteur manuel)
  if (!manualRelayActive && relayActive && (millis() - relayStartTime >= config.relayDuration)) {
    deactivateRelay();
  }
  
  // Vérification barrière photoélectrique
  if (config.photoBarrierEnabled && relayActive) {
    if (digitalRead(pins.photoBarrier) == LOW) {  // Barrière coupée
      logMessage("[RELAY] Barrière photo déclenchée, arrêt du relais");
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
  config.authMode = preferences.getUChar("authMode", AUTH_MODE_RFID_ONLY);
  config.useStaticIP = preferences.getBool("useStaticIP", false);
  preferences.getString("staticIP",  config.staticIP,      sizeof(config.staticIP));
  preferences.getString("staticGW",  config.staticGateway, sizeof(config.staticGateway));
  preferences.getString("staticSN",  config.staticSubnet,  sizeof(config.staticSubnet));
  
  if (strlen(config.mqttTopic) == 0) strcpy(config.mqttTopic, "roller");
  if (strlen(config.adminPassword) == 0) strcpy(config.adminPassword, "admin");
  if (strlen(config.staticSubnet) == 0) strcpy(config.staticSubnet, "255.255.255.0");
  if (config.authMode > 2) config.authMode = AUTH_MODE_RFID_ONLY;
  
  config.initialized = preferences.getBool("init", false);
  
  logPrintf("[CFG] adminPassword: %d caractères chargés", strlen(config.adminPassword));
  logPrintf("[CFG] Chargée: Relais=%lums, MQTT=%s:%d",
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
  preferences.putUChar("authMode", config.authMode);
  preferences.putBool("useStaticIP", config.useStaticIP);
  preferences.putString("staticIP",  config.staticIP);
  preferences.putString("staticGW",  config.staticGateway);
  preferences.putString("staticSN",  config.staticSubnet);
  preferences.putBool("init", true);
  
  const char* authNames[] = {"PIN seul", "RFID seul", "PIN+RFID"};
  logPrintf("[CFG] Enregistrée en flash (mode auth: %s)", authNames[config.authMode]);
}

// ===== FONCTIONS RELAIS =====
void activateRelay(bool open) {
  // SÉCURITÉ 1: Ne jamais activer les 2 relais simultanément !
  digitalWrite(pins.relayOpen, LOW);
  digitalWrite(pins.relayClose, LOW);
  delay(100);  // Pause de sécurité
  
  // SÉCURITÉ 2: Vérifier que l'autre relais est bien OFF
  if (open) {
    if (digitalRead(pins.relayClose) == HIGH) {
      logMessage("[RELAY] ERREUR sécurité: RELAY_CLOSE encore actif!");
      return;
    }
  } else {
    if (digitalRead(pins.relayOpen) == HIGH) {
      logMessage("[RELAY] ERREUR sécurité: RELAY_OPEN encore actif!");
      return;
    }
  }
  
  if (relayActive) {
    deactivateRelay();
    delay(100);
  }
  
  digitalWrite(open ? pins.relayOpen : pins.relayClose, HIGH);
  relayStartTime = millis();
  relayActive = true;
  
  logPrintf("[RELAY] Activé: %s pour %lums", open ? "OUVRIR" : "FERMER", config.relayDuration);
  
  char payload[128];
  snprintf(payload, sizeof(payload), 
           "{\"action\":\"%s\",\"duration\":%lu}", 
           open ? "open" : "close", config.relayDuration);
  publishMQTT("relay", payload);
}

void deactivateRelay() {
  digitalWrite(pins.relayOpen, LOW);
  digitalWrite(pins.relayClose, LOW);
  relayActive = false;
  manualRelayActive = false;  // Réinitialiser aussi l'état manuel (barrière, MQTT, etc.)
  
  logMessage("[RELAY] Désactivé");
  publishMQTT("relay", "{\"action\":\"stopped\"}");
}

// ===== CONFIGURATION DES BROCHES =====
void loadPinConfig() {
  pins.wiegandD0      = preferences.getUChar("pin_wgD0", DEFAULT_WIEGAND_D0);
  pins.wiegandD1      = preferences.getUChar("pin_wgD1", DEFAULT_WIEGAND_D1);
  pins.relayOpen      = preferences.getUChar("pin_rlOp", DEFAULT_RELAY_OPEN);
  pins.relayClose     = preferences.getUChar("pin_rlCl", DEFAULT_RELAY_CLOSE);
  pins.photoBarrier   = preferences.getUChar("pin_phBr", DEFAULT_PHOTO_BARRIER);
  pins.statusLed      = preferences.getUChar("pin_stLd", DEFAULT_STATUS_LED);
  pins.readerLedRed   = preferences.getUChar("pin_rlR",  DEFAULT_READER_LED_RED);
  pins.readerLedGreen = preferences.getUChar("pin_rlG",  DEFAULT_READER_LED_GREEN);
  pins.pinUpSwitch    = preferences.getUChar("pin_upSw", DEFAULT_PIN_UP_SWITCH);
  pins.pinDownSwitch  = preferences.getUChar("pin_dwSw", DEFAULT_PIN_DOWN_SWITCH);

  logPrintf("[PIN] WG(%d,%d) Relay(%d,%d) Photo:%d LED:%d R:%d G:%d SW(%d,%d)",
            pins.wiegandD0, pins.wiegandD1, pins.relayOpen, pins.relayClose,
            pins.photoBarrier, pins.statusLed, pins.readerLedRed, pins.readerLedGreen,
            pins.pinUpSwitch, pins.pinDownSwitch);
}

void savePinConfig() {
  preferences.putUChar("pin_wgD0", pins.wiegandD0);
  preferences.putUChar("pin_wgD1", pins.wiegandD1);
  preferences.putUChar("pin_rlOp", pins.relayOpen);
  preferences.putUChar("pin_rlCl", pins.relayClose);
  preferences.putUChar("pin_phBr", pins.photoBarrier);
  preferences.putUChar("pin_stLd", pins.statusLed);
  preferences.putUChar("pin_rlR",  pins.readerLedRed);
  preferences.putUChar("pin_rlG",  pins.readerLedGreen);
  preferences.putUChar("pin_upSw", pins.pinUpSwitch);
  preferences.putUChar("pin_dwSw", pins.pinDownSwitch);
  logMessage("[PIN] Configuration des broches enregistrée (red\u00e9marrage requis)");
}

// ===== RESET WIFI =====
void resetWifiAndRestart() {
  logMessage("[WIFI] Réinitialisation des identifiants WiFi...");
  config.useStaticIP = false;
  config.staticIP[0] = '\0';
  config.staticGateway[0] = '\0';
  config.staticSubnet[0] = '\0';
  saveConfig();
  wifiManager.resetSettings();
  delay(1000);
  ESP.restart();
}

