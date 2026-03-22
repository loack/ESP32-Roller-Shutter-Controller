#include "web_server.h"
#include "config.h"
#include "mqtt.h"
#include "serial_manager.h"
#include <ElegantOTA.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#include <ETH.h>

extern AsyncWebServer server;
extern Config config;
extern IOPin ioPins[];
extern int ioPinCount;
extern bool mqttEnabled;
extern bool ethConnected;

extern void logMessage(const String& message);

extern void saveConfig();
extern void saveIOs();
extern void applyIOPinModes();

// WebSocket for real-time logs/events
AsyncWebSocket ws("/ws");

// Fallback server on 8080 in case port 80 bind fails
static AsyncWebServer server8080(8080);

void setupWebServer() {
  // Servir le fichier index.html depuis SPIFFS
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", "text/html");
  });
  
  // API pour le statut système complet
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
    JsonDocument doc;
    doc["deviceName"] = config.deviceName;
    doc["useEthernet"] = config.useEthernet;
    
    if (config.useEthernet) {
      doc["network"] = ethConnected;
      doc["ip"] = ethConnected ? ETH.localIP().toString() : "Not connected";
      doc["networkType"] = "Ethernet";
    } else {
      doc["network"] = WiFi.status() == WL_CONNECTED;
      doc["ip"] = WiFi.localIP().toString();
      doc["networkType"] = "WiFi";
      doc["rssi"] = WiFi.RSSI();
    }
    
    doc["mqtt"] = mqttClient.connected();
    
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char timeStr[20];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    doc["time"] = timeStr;
    
    JsonArray ios = doc["ios"].to<JsonArray>();
    for (int i = 0; i < ioPinCount; i++) {
      JsonObject io = ios.add<JsonObject>();
      io["name"] = ioPins[i].name;
      io["pin"] = ioPins[i].pin;
      io["mode"] = ioPins[i].mode;
      io["state"] = digitalRead(ioPins[i].pin);
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API pour contrôler une sortie
  server.on("/api/io/set", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      JsonDocument doc;
      if (deserializeJson(doc, (const char*)data) != DeserializationError::Ok) {
        request->send(400, "application/json", "{\"success\":false, \"message\":\"Invalid JSON\"}");
        return;
      }
      
      const char* ioName = doc["name"];
      bool state = doc["state"];

      for (int i = 0; i < ioPinCount; i++) {
        if (strcmp(ioPins[i].name, ioName) == 0) {
          if (ioPins[i].mode == 2) { // OUTPUT
            executeCommand(ioPins[i].pin, state);
            request->send(200, "application/json", "{\"success\":true, \"message\":\"IO mis à jour\"}");
          } else {
            request->send(400, "application/json", "{\"success\":false, \"message\":\"Cet IO n'est pas une sortie\"}");
            return;
          }
          return;
        }
      }
      request->send(404, "application/json", "{\"success\":false, \"message\":\"IO non trouvé\"}");
    }
  );

  // API pour récupérer la config des IOs
  server.on("/api/ios", HTTP_GET, [](AsyncWebServerRequest *request){
    JsonDocument doc;
    JsonArray ios = doc["ios"].to<JsonArray>();
    for (int i = 0; i < ioPinCount; i++) {
      JsonObject io = ios.add<JsonObject>();
      io["name"] = ioPins[i].name;
      io["pin"] = ioPins[i].pin;
      io["mode"] = ioPins[i].mode;
      io["inputType"] = ioPins[i].inputType;
      io["defaultState"] = ioPins[i].defaultState;
    }
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  // API pour enregistrer la config des IOs
  server.on("/api/ios", HTTP_POST, 
    [](AsyncWebServerRequest *request){},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
    JsonDocument doc;
    if (deserializeJson(doc, (const char*)data) != DeserializationError::Ok) {
        request->send(400, "application/json", "{\"success\":false, \"message\":\"Invalid JSON\"}");
        return;
    }
    JsonArray newIOs = doc["ios"];
    ioPinCount = 0;
    for (JsonObject ioData : newIOs) {
        if (ioPinCount < MAX_IOS) {
            strlcpy(ioPins[ioPinCount].name, ioData["name"], sizeof(ioPins[ioPinCount].name));
            ioPins[ioPinCount].pin = ioData["pin"];
            ioPins[ioPinCount].mode = ioData["mode"];
            ioPins[ioPinCount].inputType = ioData["inputType"] | 1; // Default to PULLUP if not specified
            ioPins[ioPinCount].defaultState = ioData["defaultState"];
            ioPinCount++;
        }
    }
    saveIOs();
    applyIOPinModes();
    request->send(200, "application/json", "{\"success\":true, \"message\":\"Configuration I/O enregistrée.\"}");
  });
  
  // API pour récupérer la configuration système
  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request){
    JsonDocument doc;
    doc["deviceName"] = config.deviceName;
    doc["useEthernet"] = config.useEthernet;
    doc["ethernetType"] = config.ethernetType;
    doc["useStaticIP"] = config.useStaticIP;
    doc["staticIP"] = config.staticIP;
    doc["staticGateway"] = config.staticGateway;
    doc["staticSubnet"] = config.staticSubnet;
    doc["mqttServer"] = config.mqttServer;
    doc["mqttPort"] = config.mqttPort;
    doc["mqttUser"] = config.mqttUser;
    doc["mqttTopic"] = config.mqttTopic;

    doc["useSerialBridge"] = config.useSerialBridge;
    doc["serialRxPin"] = config.serialRxPin;
    doc["serialTxPin"] = config.serialTxPin;
    doc["serialBaudRate"] = config.serialBaudRate;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API pour enregistrer la configuration système
  server.on("/api/config", HTTP_POST, 
    [](AsyncWebServerRequest *request){},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      JsonDocument doc;
      if (deserializeJson(doc, (const char*)data) != DeserializationError::Ok) {
        request->send(400, "application/json", "{\"success\":false, \"message\":\"Invalid JSON\"}");
        return;
      }
    
      if (doc["deviceName"]) strlcpy(config.deviceName, doc["deviceName"], sizeof(config.deviceName));
      
      // Network settings
      if (doc["useEthernet"].is<bool>()) config.useEthernet = doc["useEthernet"];
      if (doc["ethernetType"]) strlcpy(config.ethernetType, doc["ethernetType"], sizeof(config.ethernetType));
      
      config.useStaticIP = doc["useStaticIP"];
      if (doc["staticIP"]) strlcpy(config.staticIP, doc["staticIP"], sizeof(config.staticIP));
      if (doc["staticGateway"]) strlcpy(config.staticGateway, doc["staticGateway"], sizeof(config.staticGateway));
      if (doc["staticSubnet"]) strlcpy(config.staticSubnet, doc["staticSubnet"], sizeof(config.staticSubnet));

      if (doc["mqttServer"]) strlcpy(config.mqttServer, doc["mqttServer"], sizeof(config.mqttServer));
      if (doc["mqttPort"]) config.mqttPort = doc["mqttPort"];
      if (doc["mqttUser"]) strlcpy(config.mqttUser, doc["mqttUser"], sizeof(config.mqttUser));
      if (doc["mqttPassword"] && !doc["mqttPassword"].isNull() && strlen(doc["mqttPassword"]) > 0) {
        strlcpy(config.mqttPassword, doc["mqttPassword"], sizeof(config.mqttPassword));
      }
      if (doc["mqttTopic"]) strlcpy(config.mqttTopic, doc["mqttTopic"], sizeof(config.mqttTopic));
      
      if (doc["useSerialBridge"].is<bool>()) config.useSerialBridge = doc["useSerialBridge"];
      if (doc["serialRxPin"]) config.serialRxPin = doc["serialRxPin"];
      if (doc["serialTxPin"]) config.serialTxPin = doc["serialTxPin"];
      if (doc["serialBaudRate"]) config.serialBaudRate = doc["serialBaudRate"];

      saveConfig();
      
      request->send(200, "application/json", "{\"success\":true, \"message\":\"Configuration enregistrée, redémarrage...\"}");
      delay(1000);
      ESP.restart();
    }
  );

  // API pour contrôler la connexion MQTT
  server.on("/api/mqtt/connect", HTTP_POST, [](AsyncWebServerRequest *request){
    mqttEnabled = true;
    reconnectMQTT();
    request->send(200, "application/json", "{\"success\":true, \"message\":\"Tentative de connexion MQTT lancée.\"}");
  });

  server.on("/api/mqtt/disconnect", HTTP_POST, [](AsyncWebServerRequest *request){
    mqttEnabled = false;
    mqttClient.disconnect();
    request->send(200, "application/json", "{\"success\":true, \"message\":\"MQTT déconnecté.\"}");
  });

  // API pour envoyer un message série
  server.on("/api/serial/send", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      JsonDocument doc;
      if (deserializeJson(doc, (const char*)data) != DeserializationError::Ok) {
        request->send(400, "application/json", "{\"success\":false, \"message\":\"Invalid JSON\"}");
        return;
      }
      
      if (doc["message"]) {
        String msg = doc["message"].as<String>();
        serialManager.send(msg);
        request->send(200, "application/json", "{\"success\":true, \"message\":\"Message sent\"}");
      } else {
        request->send(400, "application/json", "{\"success\":false, \"message\":\"Missing message\"}");
      }
    }
  );

  // API pour simuler un message RX série et le publier sur MQTT
  server.on("/api/serial/simulate-rx", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      JsonDocument doc;
      if (deserializeJson(doc, (const char*)data) != DeserializationError::Ok) {
        request->send(400, "application/json", "{\"success\":false, \"message\":\"Invalid JSON\"}");
        return;
      }
      
      if (doc["message"]) {
        String msg = doc["message"].as<String>();
        serialManager.publish(msg); // Utilise la nouvelle fonction pour publier
        serialManager.addLog("RX (Sim)", msg); // Ajoute au log local comme une simulation
        request->send(200, "application/json", "{\"success\":true, \"message\":\"Simulated RX message published to MQTT\"}");
      } else {
        request->send(400, "application/json", "{\"success\":false, \"message\":\"Missing message\"}");
      }
    }
  );

  // API pour récupérer les logs série
  server.on("/api/serial/logs", HTTP_GET, [](AsyncWebServerRequest *request){
    JsonDocument doc;
    JsonArray logs = doc.to<JsonArray>();
    
    std::vector<SerialLog> serialLogs = serialManager.getLogs();
    for (const auto& log : serialLogs) {
      JsonObject l = logs.add<JsonObject>();
      l["timestamp"] = log.timestamp;
      l["direction"] = log.direction;
      l["message"] = log.message;
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  // ElegantOTA pour les mises à jour
  ElegantOTA.begin(&server);
  
  // Attach WebSocket to both servers so clients can connect whether port 80
  // succeeded or not.
  server.addHandler(&ws);
  server8080.addHandler(&ws);

  // WebSocket event handling. On connect we send an initial small message
  // so the client knows the socket is ready. Real-time logs will be pushed
  // from code that generates them (see notes below).
  ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len){
    if (type == WS_EVT_CONNECT) {
      server->text(client->id(), "{\"type\":\"ws_connected\",\"message\":\"ws ready\"}");
    }
  });

  server.begin();
  logMessage("Web server started with new architecture.");

  // Start fallback server on 8080 as a lightweight mirror of the main routes.
  // This helps when port 80 is occupied by another service (bind error).
  server8080.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", "text/html");
  });
  server8080.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
    JsonDocument doc;
    doc["deviceName"] = config.deviceName;
    doc["useEthernet"] = config.useEthernet;
    if (config.useEthernet) {
      doc["network"] = ethConnected;
      doc["ip"] = ethConnected ? ETH.localIP().toString() : "Not connected";
      doc["networkType"] = "Ethernet";
    } else {
      doc["network"] = WiFi.status() == WL_CONNECTED;
      doc["ip"] = WiFi.localIP().toString();
      doc["networkType"] = "WiFi";
      doc["rssi"] = WiFi.RSSI();
    }
    doc["mqtt"] = mqttClient.connected();
    time_t now; time(&now);
    struct tm timeinfo; localtime_r(&now, &timeinfo);
    char timeStr[20]; strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    doc["time"] = timeStr;
    String response; serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  server8080.begin();
  logMessage("Fallback web server started on port 8080.");
}
