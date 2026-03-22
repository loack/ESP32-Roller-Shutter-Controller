#include "serial_manager.h"
#include "config.h"
#include "mqtt.h"
#include <ESPAsyncWebServer.h>
#include <time.h>
#include <ArduinoJson.h>

extern Config config;
extern bool mqttEnabled;
extern void logMessage(const String& message);
extern void logPrintf(const char* fmt, ...);

SerialManager serialManager;

SerialManager::SerialManager() {
    _serial = &Serial2; // Use Serial2 for external communication
}

void SerialManager::begin() {
    if (config.useSerialBridge) {
        // Use configured pins instead of hardcoded ones
        const int rxPin = config.serialRxPin;
        const int txPin = config.serialTxPin;
        long baud = config.serialBaudRate > 0 ? config.serialBaudRate : 9600;

        _serial->begin(baud, SERIAL_8N1, rxPin, txPin);
        logPrintf("Serial Bridge started on RX:%d, TX:%d at %ld baud", rxPin, txPin, baud);
    } else {
        _serial->end();
        logMessage("Serial Bridge disabled, pins released");
    }
}

void SerialManager::loop() {
    if (!config.useSerialBridge) return;

    if (_serial->available()) {
        String msg = _serial->readStringUntil('\n');
        msg.trim();

        if (msg.length() > 0) {
            addLog("RX", msg);
            logMessage("Serial Bridge RX: " + msg);
            publish(msg);
        }
    }
}

void SerialManager::send(String message) {
    if (!config.useSerialBridge) return;

    _serial->println(message);
    addLog("TX", message);
    logMessage("Serial Bridge TX: " + message);
}

void SerialManager::publish(String message) {
    if (!config.useSerialBridge) return;

    // Publish received message to MQTT
    if (mqttEnabled && mqttClient.connected()) {
        char topic[128];
        snprintf(topic, sizeof(topic), "%s/serial/receive", config.deviceName);

        JsonDocument doc;
        doc["message"] = message;
        
        time_t now;
        time(&now);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        char timeStr[25];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
        doc["timestamp"] = timeStr;

        char payload[256];
        serializeJson(doc, payload);
        
        logPrintf("Publishing serial RX to topic [%s]: %s", topic, payload);
        publishMQTT(topic, payload);
    } else {
        logMessage("MQTT not connected or disabled - serial message not published");
    }
}

void SerialManager::addLog(String direction, String message) {
    SerialLog log;
    
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char timeStr[20];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
    
    log.timestamp = String(timeStr);
    log.direction = direction;
    log.message = message;
    
    _logs.push_back(log);
    
    // Keep only last 50 logs
    if (_logs.size() > 50) {
        _logs.erase(_logs.begin());
    }

    // Also stream the new log to any connected WebSocket clients (real-time)
    extern AsyncWebSocket ws; // declared in web_server.cpp
    JsonDocument doc;
    doc["type"] = "serial_log";
    doc["timestamp"] = log.timestamp;
    doc["direction"] = log.direction;
    doc["message"] = log.message;
    char buf[256];
    size_t n = serializeJson(doc, buf);
    if (ws.count() > 0) {
        ws.textAll(buf, n);
    }
}

std::vector<SerialLog> SerialManager::getLogs() {
    return _logs;
}

void SerialManager::clearLogs() {
    _logs.clear();
}
