#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include <Arduino.h>
#include <functional>

#define LOG_BUFFER_SIZE 200

void initLogManager();
void setLogBroadcastCallback(std::function<void(const String&)> cb);
void logMessage(const String& message);
void logPrintf(const char* fmt, ...);
void sendLogHistory(std::function<void(const String&)> sender);

// À appeler depuis loop() — diffuse les logs en attente vers WebSocket
void flushLogBroadcasts();

#endif
