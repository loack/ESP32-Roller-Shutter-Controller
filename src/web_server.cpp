#include "web_server.h"
#include "config.h"
#include "log_manager.h"
#include <ElegantOTA.h>
#include <PubSubClient.h>

extern Config config;
extern AccessCode accessCodes[];
extern AccessLog accessLogs[];
extern int accessCodeCount;
extern int logIndex;
extern PubSubClient mqttClient;
extern bool relayActive;
extern bool learningMode;
extern uint8_t learningType;

extern void saveConfig();
extern void saveAccessCodes();
extern void activateRelay(bool open);
extern void deactivateRelay();
extern bool deleteAccessCode(int index);
extern void startLearningMode(uint8_t type, const char* name);
extern void stopLearningMode();
extern PinConfig pins;
extern void savePinConfig();
extern void resetWifiAndRestart();

AsyncWebSocket ws("/ws");

void loopWebSocket() {
  ws.cleanupClients();
  flushLogBroadcasts();  // diffuse les logs mis en file depuis les tasks async
}

void setupWebServer() {
  // Page principale
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });
  
  // API - Statut système
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
    JsonDocument doc;
    doc["mqtt"] = mqttClient.connected();
    doc["barrier"] = digitalRead(pins.photoBarrier);
    doc["wifi"] = WiFi.status() == WL_CONNECTED;
    doc["ip"] = WiFi.localIP().toString();
    doc["ssid"] = WiFi.SSID();
    doc["relayActive"] = relayActive;
    doc["learning"] = learningMode;
    doc["learningType"] = learningType;
    doc["authMode"] = config.authMode;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  // API - Démarrer mode apprentissage
  server.on("/api/learn", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, (const char*)data);
      if (error) {
        request->send(400, "application/json", "{\"error\":\"JSON invalide\"}");
        return;
      }
      uint8_t type = doc["type"] | 0;
      const char* name = doc["name"] | "Inconnu";
      if (type > 2) {
        request->send(400, "application/json", "{\"error\":\"Type invalide (0-2)\"}");
        return;
      }
      startLearningMode(type, name);
      request->send(200, "application/json", "{\"message\":\"Mode apprentissage d\u00e9marr\u00e9\"}");
    }
  );

  // API - Arrêter mode apprentissage
  server.on("/api/learn/stop", HTTP_POST, [](AsyncWebServerRequest *request){
    stopLearningMode();
    request->send(200, "application/json", "{\"message\":\"Mode apprentissage arr\u00eat\u00e9\"}");
  });
  
  // API - Contrôle relais
  server.on("/api/relay", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      JsonDocument doc;
      deserializeJson(doc, (const char*)data);
      
      String action = doc["action"].as<String>();
      
      if (action == "open") {
        activateRelay(true);
        request->send(200, "application/json", "{\"message\":\"Ouverture en cours\"}");
      } else if (action == "close") {
        activateRelay(false);
        request->send(200, "application/json", "{\"message\":\"Fermeture en cours\"}");
      } else if (action == "stop") {
        deactivateRelay();
        request->send(200, "application/json", "{\"message\":\"Arrêt du relais\"}");
      } else {
        request->send(400, "application/json", "{\"error\":\"Action invalide\"}");
      }
    }
  );
  
  // API - Récupérer les codes
  server.on("/api/codes", HTTP_GET, [](AsyncWebServerRequest *request){
    JsonDocument doc;
    JsonArray codes = doc["codes"].to<JsonArray>();
    
    for (int i = 0; i < accessCodeCount; i++) {
      JsonObject code = codes.add<JsonObject>();
      code["code"] = accessCodes[i].code;
      code["type"] = accessCodes[i].type;
      code["name"] = accessCodes[i].name;
      code["active"] = accessCodes[i].active;
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API - Ajouter un code
  server.on("/api/codes", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      if (accessCodeCount >= 50) {
        request->send(400, "application/json", "{\"error\":\"Limite de codes atteinte\"}");
        return;
      }
      
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, (const char*)data);
      
      if (error) {
        request->send(400, "application/json", "{\"error\":\"JSON invalide\"}");
        return;
      }
      
      // Validation des champs requis
      if (!doc["code"].is<uint32_t>() || !doc["type"].is<uint8_t>() || !doc["name"].is<const char*>()) {
        request->send(400, "application/json", "{\"error\":\"Champs manquants ou invalides\"}");
        return;
      }
      
      uint32_t code = doc["code"];
      uint8_t type = doc["type"];
      const char* name = doc["name"];
      
      // Validation des valeurs
      if (code == 0) {
        request->send(400, "application/json", "{\"error\":\"Code ne peut pas être 0\"}");
        return;
      }
      
      if (type > 2) {
        request->send(400, "application/json", "{\"error\":\"Type invalide (0-2)\"}");
        return;
      }
      
      if (strlen(name) == 0 || strlen(name) > 31) {
        request->send(400, "application/json", "{\"error\":\"Nom invalide (1-31 caractères)\"}");
        return;
      }
      
      // Vérifier si le code existe déjà
      for (int i = 0; i < accessCodeCount; i++) {
        if (accessCodes[i].code == code && accessCodes[i].type == type) {
          request->send(400, "application/json", "{\"error\":\"Ce code existe déjà\"}");
          return;
        }
      }
      
      // Ajouter le code
      accessCodes[accessCodeCount].code = code;
      accessCodes[accessCodeCount].type = type;
      strlcpy(accessCodes[accessCodeCount].name, name, 32);
      accessCodes[accessCodeCount].active = true;
      
      accessCodeCount++;
      saveAccessCodes();
      
      logPrintf("[WEB] Code ajouté: %s (code=%lu, type=%d)", name, code, type);
      
      request->send(200, "application/json", "{\"message\":\"Code ajout\u00e9\"}");
    }
  );
  
  // API - Supprimer un code (DELETE /api/codes?index=X)
  server.on("/api/codes", HTTP_DELETE, [](AsyncWebServerRequest *request){
    if (!request->hasParam("index")) {
      request->send(400, "application/json", "{\"error\":\"Param\\u00e8tre index manquant\"}");
      return;
    }

    String idxStr = request->getParam("index")->value();
    // toInt() retourne 0 pour les entrées non-numériques — vérifier que c'est bien un nombre
    bool valid = idxStr.length() > 0;
    for (unsigned int k = 0; k < idxStr.length(); k++) {
      if (!isDigit(idxStr[k])) { valid = false; break; }
    }
    if (!valid) {
      request->send(400, "application/json", "{\"error\":\"Index invalide\"}");
      return;
    }

    int idx = idxStr.toInt();
    if (deleteAccessCode(idx)) {
      request->send(200, "application/json", "{\"message\":\"Code supprim\\u00e9\"}");
    } else {
      request->send(400, "application/json", "{\"error\":\"Index invalide ou erreur lors de la suppression\"}");
    }
  });
  
  // API - Récupérer les logs
  server.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest *request){
    JsonDocument doc;
    JsonArray logs = doc["logs"].to<JsonArray>();
    
    for (int i = 0; i < 100; i++) {
      if (accessLogs[i].timestamp > 0) {
        JsonObject log = logs.add<JsonObject>();
        log["timestamp"] = accessLogs[i].timestamp;
        log["code"] = accessLogs[i].code;
        log["granted"] = accessLogs[i].granted;
        log["type"] = accessLogs[i].type;
      }
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API - Récupérer la configuration
  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request){
    JsonDocument doc;
    doc["relayDuration"] = config.relayDuration;
    doc["photoEnabled"] = config.photoBarrierEnabled;
    doc["mqttServer"] = config.mqttServer;
    doc["mqttPort"] = config.mqttPort;
    doc["mqttUser"] = config.mqttUser;
    doc["mqttTopic"] = config.mqttTopic;
    doc["authMode"] = config.authMode;
    doc["useStaticIP"] = config.useStaticIP;
    doc["staticIP"] = config.staticIP;
    doc["staticGateway"] = config.staticGateway;
    doc["staticSubnet"] = config.staticSubnet;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API - Enregistrer la configuration
  server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      JsonDocument doc;
      deserializeJson(doc, (const char*)data);
      
      config.relayDuration = doc["relayDuration"] | 5000;
      config.photoBarrierEnabled = doc["photoEnabled"] | true;
      config.mqttPort = doc["mqttPort"] | 1883;
      
      if (doc.containsKey("mqttServer")) 
        strlcpy(config.mqttServer, doc["mqttServer"], 64);
      if (doc.containsKey("mqttUser")) 
        strlcpy(config.mqttUser, doc["mqttUser"], 32);
      if (doc.containsKey("mqttPassword")) 
        strlcpy(config.mqttPassword, doc["mqttPassword"], 32);
      if (doc.containsKey("mqttTopic")) 
        strlcpy(config.mqttTopic, doc["mqttTopic"], 64);
      if (doc.containsKey("adminPassword")) 
        strlcpy(config.adminPassword, doc["adminPassword"], 32);
      
      if (doc["authMode"].is<uint8_t>()) {
        uint8_t mode = doc["authMode"];
        if (mode <= 2) config.authMode = mode;
      }

      if (doc["useStaticIP"].is<bool>())
        config.useStaticIP = doc["useStaticIP"];
      if (doc.containsKey("staticIP"))
        strlcpy(config.staticIP, doc["staticIP"] | "", 16);
      if (doc.containsKey("staticGateway"))
        strlcpy(config.staticGateway, doc["staticGateway"] | "", 16);
      if (doc.containsKey("staticSubnet"))
        strlcpy(config.staticSubnet, doc["staticSubnet"] | "", 16);

      saveConfig();

      request->send(200, "application/json", "{\"message\":\"Configuration enregistrée\", \"restart\":true}");
    }
  );
  
  // API - Lire la configuration des broches GPIO
  server.on("/api/pins", HTTP_GET, [](AsyncWebServerRequest *request){
    JsonDocument doc;
    doc["wiegandD0"]    = pins.wiegandD0;
    doc["wiegandD1"]    = pins.wiegandD1;
    doc["relayOpen"]    = pins.relayOpen;
    doc["relayClose"]   = pins.relayClose;
    doc["photoBarrier"] = pins.photoBarrier;
    doc["statusLed"]    = pins.statusLed;
    doc["readerLedRed"]   = pins.readerLedRed;
    doc["readerLedGreen"] = pins.readerLedGreen;
    doc["pinUpSwitch"]    = pins.pinUpSwitch;
    doc["pinDownSwitch"]  = pins.pinDownSwitch;
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  // API - Enregistrer la configuration des broches GPIO
  server.on("/api/pins", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      JsonDocument doc;
      if (deserializeJson(doc, (const char*)data)) {
        request->send(400, "application/json", "{\"error\":\"JSON invalide\"}");
        return;
      }
      // Valider et appliquer chaque broche (0-39)
      auto setPin = [&](uint8_t &dest, const char* key) -> bool {
        if (!doc[key].is<uint8_t>()) return false;
        uint8_t v = doc[key];
        if (v > 39) return false;
        dest = v;
        return true;
      };
      if (!setPin(pins.wiegandD0,    "wiegandD0")    ||
          !setPin(pins.wiegandD1,    "wiegandD1")    ||
          !setPin(pins.relayOpen,    "relayOpen")    ||
          !setPin(pins.relayClose,   "relayClose")   ||
          !setPin(pins.photoBarrier, "photoBarrier") ||
          !setPin(pins.statusLed,    "statusLed")    ||
          !setPin(pins.readerLedRed,   "readerLedRed")   ||
          !setPin(pins.readerLedGreen, "readerLedGreen") ||
          !setPin(pins.pinUpSwitch,    "pinUpSwitch")    ||
          !setPin(pins.pinDownSwitch,  "pinDownSwitch")) {
        request->send(400, "application/json", "{\"error\":\"Valeur de broche invalide (0-39)\"}");
        return;
      }
      savePinConfig();
      request->send(200, "application/json", "{\"message\":\"Broches enregistrées\", \"restart\":true}");
    }
  );

  // API - Réinitialiser les identifiants WiFi (passe en mode AP)
  server.on("/api/wifi/reset", HTTP_POST, [](AsyncWebServerRequest *request){
    request->send(200, "application/json", "{\"message\":\"Réinitialisation WiFi en cours...\"}" );
    delay(500);
    resetWifiAndRestart();
  });

  // API - Redémarrer l'ESP32
  server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest *request){
    request->send(200, "application/json", "{\"message\":\"Redémarrage en cours...\"}");
    delay(1000);
    ESP.restart();
  });

  // ElegantOTA pour les mises à jour
  ElegantOTA.begin(&server);

  // WebSocket
  ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client,
                AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
      // Envoyer l'historique des logs au nouveau client
      sendLogHistory([client](const String& json) {
        client->text(json);
      });
    }
  });
  server.addHandler(&ws);

  // Enregistrer le callback de diffusion des logs vers WebSocket
  setLogBroadcastCallback([](const String& json) {
    ws.textAll(json);
  });

  logMessage("[WEB] Serveur HTTP + WebSocket prêt");
}
