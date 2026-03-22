#ifndef MQTT_H
#define MQTT_H

#include <WiFi.h>
#include <PubSubClient.h>
#include "config.h"

// externs provided by other translation units
extern WiFiClient wifiClient;
extern PubSubClient mqttClient;
extern Config config;
extern IOPin ioPins[];
extern int ioPinCount;
extern ScheduledCommand scheduledCommands[];
// Control whether MQTT subsystem should be active (can be toggled at runtime)
extern bool mqttEnabled;

// Fonction pour faire clignoter la LED (définie dans main.cpp)
void blinkStatusLED(int times, int delayMs);

// Fonction pour obtenir le temps avec précision microseconde
uint64_t getCurrentTimeMicros();

// MQTT API
void setupMQTT();
void reconnectMQTT();
void publishMQTT(const char* sub_topic, const char* payload, boolean retained = false);
void mqtt_callback(char* topic, byte* payload, unsigned int length);
void executeCommand(int pin, int state);

#endif // MQTT_H
