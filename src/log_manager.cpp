#include "log_manager.h"
#include <ArduinoJson.h>
#include <stdarg.h>
#include <freertos/FreeRTOS.h>

static std::function<void(const String&)> _broadcastCb = nullptr;
static String _logBuffer[LOG_BUFFER_SIZE];
static int _head = 0;
static int _count = 0;

// File de diffusion WebSocket — alimentée depuis n'importe quel task,
// vidée exclusivement depuis loop() pour éviter les accès concurrents.
#define BCAST_QUEUE_SIZE 16
static String _bcastQueue[BCAST_QUEUE_SIZE];
static volatile int _bqWrite = 0;  // écrit par tous tasks
static volatile int _bqRead  = 0;  // lu uniquement par loop()
static portMUX_TYPE _bqMux = portMUX_INITIALIZER_UNLOCKED;

void initLogManager() { _head = 0; _count = 0; _bqWrite = 0; _bqRead = 0; }

void setLogBroadcastCallback(std::function<void(const String&)> cb) { _broadcastCb = cb; }

static void addToBuffer(const String& message) {
  _logBuffer[_head] = message;
  _head = (_head + 1) % LOG_BUFFER_SIZE;
  if (_count < LOG_BUFFER_SIZE) _count++;
}

static String buildJson(const String& message) {
  JsonDocument doc;
  doc["log"] = message;
  String json;
  serializeJson(doc, json);
  return json;
}

void logMessage(const String& message) {
  Serial.println(message);
  addToBuffer(message);
  if (_broadcastCb) {
    // Mettre en file pour diffusion depuis loop() — jamais en direct
    String json = buildJson(message);
    portENTER_CRITICAL(&_bqMux);
    int next = (_bqWrite + 1) % BCAST_QUEUE_SIZE;
    if (next != _bqRead) {  // file non pleine
      _bcastQueue[_bqWrite] = json;
      _bqWrite = next;
    }
    portEXIT_CRITICAL(&_bqMux);
  }
}

void flushLogBroadcasts() {
  while (_bqRead != _bqWrite) {
    portENTER_CRITICAL(&_bqMux);
    String json = _bcastQueue[_bqRead];
    _bqRead = (_bqRead + 1) % BCAST_QUEUE_SIZE;
    portEXIT_CRITICAL(&_bqMux);
    if (_broadcastCb) _broadcastCb(json);
  }
}

void logPrintf(const char* fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  logMessage(String(buf));
}

void sendLogHistory(std::function<void(const String&)> sender) {
  int start = (_count < LOG_BUFFER_SIZE) ? 0 : _head;
  for (int i = 0; i < _count; i++) {
    int idx = (start + i) % LOG_BUFFER_SIZE;
    sender(buildJson(_logBuffer[idx]));
  }
}
