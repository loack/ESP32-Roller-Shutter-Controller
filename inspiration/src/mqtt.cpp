#include <Arduino.h>
#include <PubSubClient.h>
#include "mqtt.h"
#include "serial_manager.h"
#include <ArduinoJson.h>
#include <time.h>
#include <sys/time.h>

#include <WiFi.h>

// define the network client and the MQTT client here (single place)
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
// MQTT active flag (default disabled so web server can be debugged first)
bool mqttEnabled = false;

extern void logMessage(const String& message);
extern void logPrintf(const char* fmt, ...);

// Variables pour synchronisation temporelle précise
static uint32_t lastSyncSeconds = 0;
static uint32_t lastSyncMicros = 0;  // micros() au moment de la sync
static uint32_t timeOffsetUs = 0;    // Offset en microsecondes

// Statistiques de synchronisation (simplifiées - juste pour affichage)
struct SyncStats {
    uint32_t sync_count = 0;
    uint32_t estimated_latency_us = 0;  // Compensation reçue du PC
    uint32_t last_sync_timestamp = 0;
} syncStats;

// Obtenir le temps actuel avec précision microseconde
uint64_t getCurrentTimeMicros() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

// MQTT callback and helpers moved out of main.cpp

// Helper to get current time as string
String getFormattedTime() {
  char timeStr[20];
  time_t now = time(nullptr);
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", localtime(&now));
  return String(timeStr);
}

void executeCommand(int pin, int state) {
  digitalWrite(pin, state);
  for (int i = 0; i < ioPinCount; i++) {
    if (ioPins[i].pin == pin) {
      ioPins[i].state = state;
      break;
    }
  }

  // Publish status
  char topic[128];
  int pinIndex = -1;
  for(int i=0; i<ioPinCount; i++) {
    if(ioPins[i].pin == pin) {
      pinIndex = i;
      break;
    }
  }
  if(pinIndex != -1) {
    snprintf(topic, sizeof(topic), "%s/status/%s", config.deviceName, ioPins[pinIndex].name);
    
    // Obtenir le temps avec précision microseconde
    uint64_t timeUs = getCurrentTimeMicros();
    uint32_t seconds = timeUs / 1000000ULL;
    uint32_t us = timeUs % 1000000ULL;
    
    JsonDocument doc;
    doc["state"] = state;
    doc["timestamp"] = seconds;
    doc["us"] = us;  // Microsecondes
    
    char payload[128];
    serializeJson(doc, payload);

    if (mqttEnabled && mqttClient.connected()) {
      publishMQTT(topic, payload);
    }
  }
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
    String topicStr = String(topic);
    String baseTopic = String(config.deviceName);

    // Convert payload to string for logging and parsing
    char message[length + 1];
    memcpy(message, payload, length);
    message[length] = '\0';

    logPrintf("[%s] MQTT message arrived on topic [%s]: %s", getFormattedTime().c_str(), topic, message);

    // Handle time synchronization first, as it's a critical service
    // Le topic de temps est commun à tous les appareils
    if (topicStr.equals("esp32/time/sync")) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload, length);
        
        if (!error && doc["seconds"].is<uint32_t>()) {
            uint32_t master_sec = doc["seconds"];
            uint32_t master_us = doc["us"] | 0;
            
            // Lire la compensation pour NOTRE device (si disponible)
            if (doc["compensations"].is<JsonObject>()) {
                JsonObject compensations = doc["compensations"];
                String deviceName = String(config.deviceName);
                
                // Utiliser la méthode moderne is<T>() au lieu de containsKey (deprecated)
                if (compensations[deviceName].is<uint32_t>()) {
                    syncStats.estimated_latency_us = compensations[deviceName];
                }
            }
            
            // Calculer le temps maître en microsecondes
            uint64_t master_time_us = (uint64_t)master_sec * 1000000ULL + master_us;
            
            // Appliquer la compensation (si disponible)
            if (syncStats.estimated_latency_us > 0) {
                master_time_us += syncStats.estimated_latency_us;
            }
            
            // Synchroniser l'horloge
            struct timeval tv;
            tv.tv_sec = master_time_us / 1000000ULL;
            tv.tv_usec = master_time_us % 1000000ULL;
            settimeofday(&tv, NULL);
            
            // Mettre à jour les statistiques
            syncStats.sync_count++;
            syncStats.last_sync_timestamp = master_sec;
            lastSyncSeconds = master_sec;
            lastSyncMicros = micros();
            
            // Affichage simplifié
            if (syncStats.sync_count <= 2) {
                logPrintf("⏰ Time sync #%u: %u.%06u (initializing)", 
                             syncStats.sync_count, tv.tv_sec, tv.tv_usec);
            } else {
                logPrintf("⏰ Time sync #%u: %u.%06u", 
                             syncStats.sync_count, tv.tv_sec, tv.tv_usec);
                
                if (syncStats.estimated_latency_us > 0) {
                    logPrintf(" | Comp: +%.2f ms", 
                                 syncStats.estimated_latency_us / 1000.0f);
                }
                logMessage("");
            }
            
        } else {
            // Ancienne méthode (compatibilité)
            unsigned long unix_time = atol(message);
            if (unix_time > 1000000000) {
                struct timeval tv;
                tv.tv_sec = unix_time;
                tv.tv_usec = 0;
                settimeofday(&tv, NULL);
                logPrintf("Time synchronized: %lu (legacy mode)", unix_time);
            }
        }
        // Publier l'état de toutes les E/S immédiatement après la synchronisation
        struct timeval current_tv;
        gettimeofday(&current_tv, NULL);

            for (int i = 0; i < ioPinCount; i++) {
                JsonDocument stateDoc;
                // Utiliser 1/0 pour l'état (format unifié)
                stateDoc["state"] = ioPins[i].state ? 1 : 0;
                
                // Utiliser le timestamp synchronisé
                stateDoc["timestamp"] = (uint32_t)current_tv.tv_sec;
                stateDoc["us"] = (uint32_t)current_tv.tv_usec;

                char statePayload[128];
                serializeJson(stateDoc, statePayload);

                char stateTopic[128];
                snprintf(stateTopic, sizeof(stateTopic), "%s/status/%s", config.deviceName, ioPins[i].name);
                 // On ne met pas retained=true ici car c'est une mise à jour périodique/événementielle
                publishMQTT(stateTopic, statePayload);
            }
        return;
    }
    
    // Topic pour mesurer la latence réseau (ping/pong) - géré par le PC
    if (topicStr.equals(String(config.deviceName) + "/ping")) {
        // Répondre immédiatement avec pong
        char pongTopic[128];
        snprintf(pongTopic, sizeof(pongTopic), "%s/pong", config.deviceName);
        
        // Renvoyer le payload reçu pour que le PC puisse mesurer le RTT
        JsonDocument pongDoc;
        pongDoc["ping_payload"] = String(message);
        
        char pongPayload[128];
        serializeJson(pongDoc, pongPayload);
        publishMQTT(pongTopic, pongPayload);
        return;
    }

    // Handle Serial Bridge commands
    if (topicStr.equals(baseTopic + "/serial/send")) {
        if (config.useSerialBridge) {
            char message[length + 1];
            memcpy(message, payload, length);
            message[length] = '\0';
            
            logPrintf("MQTT to Serial command received: %s", message);
            serialManager.send(String(message));
        } else {
            // This case should not happen if not subscribed, but as a safeguard:
            logMessage("-> WARNING: Received serial message but bridge is disabled.");
        }
        return;
    }

    // Check if it's a control topic for a pin
    String controlTopicPrefix = baseTopic + "/control/";
    if (!topicStr.startsWith(controlTopicPrefix) || !topicStr.endsWith("/set")) {
        return; // Not a command for us
    }

    // Extract pin name
    String pinName = topicStr.substring(controlTopicPrefix.length(), topicStr.length() - 4);

    // Find the IO pin by name
    for (int i = 0; i < ioPinCount; i++) {
        if (String(ioPins[i].name) == pinName) {
            if (ioPins[i].mode == 2) { // OUTPUT
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, payload, length);

                if (error) {
                    logMessage(String("deserializeJson() failed: ") + error.c_str());
                    // Fallback for simple "0" or "1" commands
                    int state = atoi(message);
                    executeCommand(ioPins[i].pin, state);
                    return;
                }

                int state = doc["state"];
                uint32_t exec_at_sec = doc["exec_at"] | 0;
                uint32_t exec_at_us = doc["exec_at_us"] | 0;

                if (exec_at_sec > 0) {
                    // Schedule command avec précision microseconde
                    bool scheduled = false;
                    for (int j = 0; j < MAX_SCHEDULED_COMMANDS; j++) {
                        if (!scheduledCommands[j].active) {
                            scheduledCommands[j].pin = ioPins[i].pin;
                            scheduledCommands[j].state = state;
                            scheduledCommands[j].exec_at_sec = exec_at_sec;
                            scheduledCommands[j].exec_at_us = exec_at_us;
                            scheduledCommands[j].active = true;
                            scheduled = true;
                            logPrintf("⏰ Command for pin %d scheduled at %u.%06u", ioPins[i].pin, exec_at_sec, exec_at_us);
                            break;
                        }
                    }
                    if (!scheduled) {
                        logMessage("⚠️ Scheduled command queue is full!");
                    }
                } else {
                    // Execute immediately
                    executeCommand(ioPins[i].pin, state);
                }

            } else {
                    logPrintf("Received command for non-output pin '%s'", pinName.c_str());
            }
            return; // Command handled for this pin
        }
    }

    logPrintf("Received command for unknown pin '%s'", pinName.c_str());
}

void setupMQTT() {
  mqttClient.setServer(config.mqttServer, config.mqttPort);
  mqttClient.setCallback(mqtt_callback);
    logMessage("MQTT setup.");
}

void reconnectMQTT() {
    logMessage("Attempting MQTT connection...");
  String clientId = "ESP32-IO-Controller-";
  clientId += String(random(0xffff), HEX);
  if (mqttClient.connect(clientId.c_str(), config.mqttUser, config.mqttPassword)) {
    logMessage("connected");
    blinkStatusLED(2, 100);  // Signal de connexion MQTT réussie
    logMessage("");
    logMessage("========================================");
    logMessage("✓ Client MQTT connecté au broker");
    
    // Publish availability
    char availabilityTopic[128];
    snprintf(availabilityTopic, sizeof(availabilityTopic), "%s/availability", config.deviceName);
    publishMQTT(availabilityTopic, "online", true);

    // Subscribe to control topics
    String controlTopic = String(config.deviceName) + "/control/#";
    mqttClient.subscribe(controlTopic.c_str());
    logPrintf("✓ Abonné à: %s", controlTopic.c_str());

    // Subscribe to time sync topic (commun à tous les ESP32)
    mqttClient.subscribe("esp32/time/sync");
    logPrintf("✓ Abonné à: esp32/time/sync");
    
    // Subscribe to ping topic for latency measurement (géré par le PC)
    String pingTopic = String(config.deviceName) + "/ping";
    mqttClient.subscribe(pingTopic.c_str());
    logPrintf("✓ Abonné à: %s", pingTopic.c_str());

    // Subscribe to serial bridge topic
    if (config.useSerialBridge) {
        String serialTopic = String(config.deviceName) + "/serial/send";
        mqttClient.subscribe(serialTopic.c_str());
        logPrintf("✓ Abonné à: %s", serialTopic.c_str());
    }
    
    logMessage("========================================");
    logMessage("");

    // Publish current state of all pins as retained messages
    struct timeval tv;
    gettimeofday(&tv, NULL);

    for (int i = 0; i < ioPinCount; i++) {
        JsonDocument doc;
        doc["state"] = ioPins[i].state ? 1 : 0;
        doc["timestamp"] = (uint32_t)tv.tv_sec;
        doc["us"] = (uint32_t)tv.tv_usec;

        char jsonBuffer[128];
        serializeJson(doc, jsonBuffer);

        char statusTopic[128];
        snprintf(statusTopic, sizeof(statusTopic), "%s/status/%s", config.deviceName, ioPins[i].name);
        publishMQTT(statusTopic, jsonBuffer, true);
    }

  } else {
    logMessage(String("failed, rc=") + String(mqttClient.state()) + String(" try again in 5 seconds"));
  }
}

void publishMQTT(const char* topic, const char* payload, boolean retained) {
    if (mqttClient.connected()) {
        if (mqttClient.publish(topic, payload, retained)) {
            logPrintf("[%s] MQTT message published to [%s]: %s", getFormattedTime().c_str(), topic, payload);
        } else {
            logPrintf("[%s] MQTT publish failed to [%s]", getFormattedTime().c_str(), topic);
        }
    }
}
