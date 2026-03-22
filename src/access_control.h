#ifndef ACCESS_CONTROL_H
#define ACCESS_CONTROL_H

#include <Arduino.h>
#include "config.h"

// ===== GESTION DES CODES D'ACCÈS =====
bool checkAccessCode(uint32_t code, uint8_t type);
void addAccessLog(uint32_t code, bool granted, uint8_t type);
bool addNewAccessCode(uint32_t code, uint8_t type, const char* name);
bool removeAccessCode(uint32_t code, uint8_t type);
bool deleteAccessCode(int index);
void loadAccessCodes();
void saveAccessCodes();

// ===== MODE APPRENTISSAGE =====
void startLearningMode(uint8_t type, const char* name);
void stopLearningMode();

// Variables globales exposées
extern AccessCode accessCodes[50];
extern AccessLog accessLogs[100];
extern int accessCodeCount;
extern int logIndex;
extern bool learningMode;
extern unsigned long learningModeStart;
extern uint8_t learningType;
extern String learningName;
extern const unsigned long LEARNING_TIMEOUT;

#endif
