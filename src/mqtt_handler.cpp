#include <Arduino.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "log_manager.h"

extern Config config;
extern PubSubClient mqttClient;
extern void activateRelay(bool open);
extern void deactivateRelay();
extern bool addNewAccessCode(uint32_t code, uint8_t type, const char* name);
extern bool removeAccessCode(uint32_t code, uint8_t type);
extern void startLearningMode(uint8_t type, const char* name);
extern void stopLearningMode();

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  logPrintf("[MQTT] Message reçu sur: %s", topic);
  
  // Conversion du payload en string
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';
  
  String topicStr = String(topic);
  String baseTopic = String(config.mqttTopic);
  
  // Topic: roller/cmd - Commandes relais
  if (topicStr == baseTopic + "/cmd") {
    String cmd = String(message);
    
    if (cmd == "open") {
      logMessage("[MQTT] Commande: OUVRIR");
      activateRelay(true);
    } else if (cmd == "close") {
      logMessage("[MQTT] Commande: FERMER");
      activateRelay(false);
    } else if (cmd == "stop") {
      logMessage("[MQTT] Commande: STOP");
      deactivateRelay();
    } else {
      logPrintf("[MQTT] Commande inconnue: %s", cmd.c_str());
    }
  }
  
  // Topic: roller/codes/add - Ajouter un code
  else if (topicStr == baseTopic + "/codes/add") {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
      logPrintf("[MQTT] Erreur JSON parse (codes/add): %s", error.c_str());
      return;
    }
    
    if (doc["code"].is<uint32_t>() && doc["type"].is<uint8_t>() && doc["name"].is<const char*>()) {
      uint32_t code = doc["code"];
      uint8_t type = doc["type"];
      const char* name = doc["name"];
      
      logPrintf("[MQTT] Ajout code: %lu type=%d nom=%s", code, type, name);
      addNewAccessCode(code, type, name);
    } else {
      logMessage("[MQTT] Format codes/add invalide. Attendu: {\"code\":123,\"type\":0,\"name\":\"Nom\"}");
    }
  }
  
  // Topic: roller/codes/remove - Supprimer un code
  else if (topicStr == baseTopic + "/codes/remove") {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
      logPrintf("[MQTT] Erreur JSON parse (codes/remove): %s", error.c_str());
      return;
    }
    
    if (doc["code"].is<uint32_t>() && doc["type"].is<uint8_t>()) {
      uint32_t code = doc["code"];
      uint8_t type = doc["type"];
      
      logPrintf("[MQTT] Suppression code: %lu type=%d", code, type);
      removeAccessCode(code, type);
    } else {
      logMessage("[MQTT] Format codes/remove invalide. Attendu: {\"code\":123,\"type\":0}");
    }
  }
  
  // Topic: roller/learn - Activer mode apprentissage
  else if (topicStr == baseTopic + "/learn") {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
      logPrintf("[MQTT] Erreur JSON parse (learn): %s", error.c_str());
      return;
    }
    
    if (doc["type"].is<uint8_t>() && doc["name"].is<const char*>()) {
      uint8_t type = doc["type"];
      const char* name = doc["name"];
      
      logPrintf("[MQTT] Démarrage apprentissage - type=%d nom=%s", type, name);
      startLearningMode(type, name);
    } else {
      logMessage("[MQTT] Format learn invalide. Attendu: {\"type\":1,\"name\":\"Nom\"}");
    }
  }
  
  // Topic: roller/learn/stop - Arrêter mode apprentissage
  else if (topicStr == baseTopic + "/learn/stop") {
    logMessage("[MQTT] Arrêt mode apprentissage");
    stopLearningMode();
  }
}

void setupMQTT() {
  if (strlen(config.mqttServer) > 0) {
    mqttClient.setServer(config.mqttServer, config.mqttPort);
    mqttClient.setCallback(mqttCallback);
    logPrintf("[MQTT] Configuré: %s:%d", config.mqttServer, config.mqttPort);
  } else {
    logMessage("[MQTT] Non configuré (aucun serveur)");
  }
}

void reconnectMQTT() {
  if (strlen(config.mqttServer) == 0) return;
  
  if (!mqttClient.connected()) {
    logMessage("[MQTT] Tentative de connexion...");
    
    String clientId = "ESP32-Roller-" + String(random(0xffff), HEX);
    
    bool connected = false;
    if (strlen(config.mqttUser) > 0) {
      connected = mqttClient.connect(clientId.c_str(), 
                                     config.mqttUser, 
                                     config.mqttPassword);
    } else {
      connected = mqttClient.connect(clientId.c_str());
    }
    
    if (connected) {
      logMessage("[MQTT] Connecté !");
      
      // Souscription aux topics de commande
      String baseTopic = String(config.mqttTopic);
      
      mqttClient.subscribe((baseTopic + "/cmd").c_str());
      mqttClient.subscribe((baseTopic + "/codes/add").c_str());
      mqttClient.subscribe((baseTopic + "/codes/remove").c_str());
      mqttClient.subscribe((baseTopic + "/learn").c_str());
      mqttClient.subscribe((baseTopic + "/learn/stop").c_str());
      
      // Publication du statut de connexion
      mqttClient.publish((baseTopic + "/status").c_str(), "{\"state\":\"online\"}");
      
      logPrintf("[MQTT] Souscrit aux topics: %s/cmd, %s/codes/add, %s/codes/remove, %s/learn",
                baseTopic.c_str(), baseTopic.c_str(), baseTopic.c_str(), baseTopic.c_str());
    } else {
      logPrintf("[MQTT] Connexion échouée, rc=%d", mqttClient.state());
    }
  }
}

void publishMQTT(const char* subtopic, const char* payload) {
  if (!mqttClient.connected()) return;
  
  String fullTopic = String(config.mqttTopic) + "/" + String(subtopic);
  
  if (mqttClient.publish(fullTopic.c_str(), payload)) {
    logPrintf("[MQTT] Publié sur %s: %s", fullTopic.c_str(), payload);
  } else {
    logMessage("[MQTT] Erreur de publication");
  }
}
