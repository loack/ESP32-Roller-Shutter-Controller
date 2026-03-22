#ifndef WIEGAND_HANDLER_H
#define WIEGAND_HANDLER_H

#include <Arduino.h>
#include <Wiegand.h>
#include "config.h"

// ===== WIEGAND & CONTRÔLE D'ACCÈS =====
void setupWiegand();
void handleWiegandInput();
void processKeypadCode();
void blinkReaderLED(bool success);
const char* getKeypadKeyLabel(uint32_t code);

// Variables de keypad exposées
extern String keypadBuffer;
extern unsigned long lastKeypadInput;
extern const unsigned long KEYPAD_TIMEOUT;
extern WIEGAND wg;

#endif
