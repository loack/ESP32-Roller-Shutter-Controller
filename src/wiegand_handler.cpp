#include "wiegand_handler.h"
#include "access_control.h"
#include "config.h"

// ===== VARIABLES GLOBALES =====
WIEGAND wg;

String keypadBuffer = "";
unsigned long lastKeypadInput = 0;
const unsigned long KEYPAD_TIMEOUT = 10000;  // 10 secondes

// Dépendances externes
extern void activateRelay(bool open);
extern void publishMQTT(const char* topic, const char* payload);

// ===== INITIALISATION =====
void setupWiegand() {
  wg.begin(WIEGAND_D0, WIEGAND_D1);
  Serial.println("✓ Wiegand initialized on pins : D0=" + String(WIEGAND_D0) + ", D1=" + String(WIEGAND_D1));
}

// ===== UTILITAIRES AFFICHAGE =====
const char* getKeypadKeyLabel(uint32_t code) {
  if (code <= 9) {
    static char digit[2];
    digit[0] = '0' + code;
    digit[1] = '\0';
    return digit;
  }
  if (code == 11) return "*";  // Wiegand 4-bit: * = 11
  if (code == 13) return "#";  // Wiegand 4-bit: # = 13
  if (code == 14) return "*";  // Wiegand 4-bit: * alternative = 14
  if (code == 27) return "ESC/CLEAR";  // Wiegand 8-bit: ESC = 27
  return "UNKNOWN";
}

// ===== LED LECTEUR =====
void blinkReaderLED(bool success) {
  if (success) {
    // LED verte - 2 clignotements
    for (int i = 0; i < 2; i++) {
      digitalWrite(READER_LED_GREEN, HIGH);
      delay(200);
      digitalWrite(READER_LED_GREEN, LOW);
      delay(200);
    }
  } else {
    // LED rouge - 3 clignotements rapides
    for (int i = 0; i < 3; i++) {
      digitalWrite(READER_LED_RED, HIGH);
      delay(100);
      digitalWrite(READER_LED_RED, LOW);
      delay(100);
    }
  }
}

// ===== CLAVIER : TRAITEMENT DU CODE SAISI =====
void processKeypadCode() {
  if (keypadBuffer.length() == 0) {
    Serial.println("⚠ Empty keypad buffer");
    return;
  }

  uint32_t code = keypadBuffer.toInt();
  Serial.printf("🔢 Processing keypad code: %lu\n", code);

  // MODE APPRENTISSAGE pour clavier
  if (learningMode && learningType == 0) {
    addNewAccessCode(code, 0, learningName.c_str());
    stopLearningMode();
    blinkReaderLED(true);
    return;
  }

  bool granted = checkAccessCode(code, 0);  // Type 0 = Keypad
  addAccessLog(code, granted, 0);

  if (granted) {
    Serial.println("✓✓✓ Keypad code GRANTED ✓✓✓");
    blinkReaderLED(true);
    activateRelay(true);

    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"code\":%lu,\"granted\":true,\"type\":\"keypad\"}",
             code);
    publishMQTT("access", payload);
  } else {
    Serial.println("✗✗✗ Keypad code DENIED ✗✗✗");
    blinkReaderLED(false);

    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"code\":%lu,\"granted\":false,\"type\":\"keypad\"}",
             code);
    publishMQTT("access", payload);
  }
}

// ===== GESTION TOUCHE CLAVIER (4 BITS) =====
static void handleKeypadEvent(uint32_t code) {
  lastKeypadInput = millis();
  Serial.printf("[WIEGAND][KEYPAD] Key event: raw=%lu, key=%s\n", code, getKeypadKeyLabel(code));

  // Touche # = validation (code 13)
  if (code == 13) {
    Serial.println("[WIEGAND][KEYPAD] Action: VALIDATE (#)");
    Serial.printf("✓ # pressed - Validating code: %s\n", keypadBuffer.c_str());
    processKeypadCode();
    keypadBuffer = "";
  }
  // Touche * = annulation (code 14 ou 11)
  else if (code == 14 || code == 11) {
    Serial.println("[WIEGAND][KEYPAD] Action: CLEAR (*)");
    Serial.println("✗ * pressed - Clearing buffer");
    keypadBuffer = "";
    blinkReaderLED(false);
  }
  // Chiffres 0-9
  else if (code <= 9) {
    keypadBuffer += String(code);
    Serial.printf("[WIEGAND][KEYPAD] Action: APPEND DIGIT, buffer=%s\n", keypadBuffer.c_str());

    // Limite à 10 chiffres
    if (keypadBuffer.length() > 10) {
      keypadBuffer = keypadBuffer.substring(1);
      Serial.printf("[WIEGAND][KEYPAD] Buffer trimmed to 10 digits: %s\n", keypadBuffer.c_str());
    }
  } else {
    Serial.printf("⚠ Unknown keypad code: %lu (key=%s)\n", code, getKeypadKeyLabel(code));
  }
}

// ===== GESTION TOUCHE ESC/CLEAR 8 BITS (code 27) =====
static void handleEscapeEvent(uint32_t code) {
  // Code 27 = ESC envoyé en 8 bits par certains claviers Wiegand
  if (code == 27) {
    Serial.println("[WIEGAND][KEYPAD] Action: ESC/CLEAR (code 27) - Clearing buffer");
    keypadBuffer = "";
    blinkReaderLED(false);
  } else {
    Serial.printf("[WIEGAND][8-bit] Unknown code: %lu (0x%02X)\n", code, code);
  }
}

// ===== GESTION BADGE/EMPREINTE RFID 26 BITS =====
static void handleWiegand26(uint32_t code) {
  // Code < 100 → empreinte digitale validée par le lecteur
  if (code < 100) {
    Serial.printf("👆 FINGERPRINT #%lu validated by reader\n", code);

    if (learningMode && learningType == 2) {
      addNewAccessCode(code, 2, learningName.c_str());
      stopLearningMode();
      blinkReaderLED(true);
      return;
    }

    bool granted = checkAccessCode(code, 2);
    addAccessLog(code, granted, 2);

    if (granted) {
      Serial.println("✓✓✓ Fingerprint GRANTED ✓✓✓");
      blinkReaderLED(true);
      activateRelay(true);

      char payload[128];
      snprintf(payload, sizeof(payload),
               "{\"code\":%lu,\"granted\":true,\"type\":\"fingerprint\",\"bits\":26}",
               code);
      publishMQTT("access", payload);
    } else {
      Serial.println("✗✗✗ Fingerprint DENIED ✗✗✗");
      blinkReaderLED(false);

      char payload[128];
      snprintf(payload, sizeof(payload),
               "{\"code\":%lu,\"granted\":false,\"type\":\"fingerprint\",\"reason\":\"not_authorized\",\"bits\":26}",
               code);
      publishMQTT("access", payload);
    }
  }
  // Code ≥ 100 → badge RFID 26 bits
  else {
    Serial.printf("🔖 RFID badge (26-bit) detected: %lu (0x%06X)\n", code, code);

    if (learningMode && learningType == 1) {
      addNewAccessCode(code, 1, learningName.c_str());
      stopLearningMode();
      blinkReaderLED(true);
      return;
    }

    bool granted = checkAccessCode(code, 1);
    addAccessLog(code, granted, 1);

    if (granted) {
      Serial.println("✓✓✓ RFID GRANTED ✓✓✓");
      blinkReaderLED(true);
      activateRelay(true);

      char payload[128];
      snprintf(payload, sizeof(payload),
               "{\"code\":%lu,\"granted\":true,\"type\":\"rfid\",\"bits\":26}",
               code);
      publishMQTT("access", payload);
    } else {
      Serial.println("✗✗✗ RFID DENIED ✗✗✗");
      blinkReaderLED(false);

      char payload[128];
      snprintf(payload, sizeof(payload),
               "{\"code\":%lu,\"granted\":false,\"type\":\"rfid\",\"bits\":26}",
               code);
      publishMQTT("access", payload);
    }
  }
}

// ===== GESTION BADGE RFID 32+ BITS =====
static void handleWiegandRfid(uint32_t code, uint8_t bitCount) {
  Serial.printf("🔖 RFID badge detected: %lu (0x%X) - %u bits\n", code, code, bitCount);

  if (learningMode && learningType == 1) {
    addNewAccessCode(code, 1, learningName.c_str());
    stopLearningMode();
    blinkReaderLED(true);
    return;
  }

  bool granted = checkAccessCode(code, 1);
  addAccessLog(code, granted, 1);

  if (granted) {
    Serial.println("✓✓✓ RFID GRANTED ✓✓✓");
    blinkReaderLED(true);
    activateRelay(true);

    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"code\":%lu,\"granted\":true,\"type\":\"rfid\",\"bits\":%u}",
             code, bitCount);
    publishMQTT("access", payload);
  } else {
    Serial.println("✗✗✗ RFID DENIED ✗✗✗");
    blinkReaderLED(false);

    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"code\":%lu,\"granted\":false,\"type\":\"rfid\",\"bits\":%u}",
             code, bitCount);
    publishMQTT("access", payload);
  }
}

// ===== POINT D'ENTRÉE PRINCIPAL =====
void handleWiegandInput() {
  // Timeout mode apprentissage
  if (learningMode && (millis() - learningModeStart > LEARNING_TIMEOUT)) {
    Serial.println("⏱ Learning mode timeout");
    stopLearningMode();
  }

  // Timeout buffer clavier
  if (keypadBuffer.length() > 0 && (millis() - lastKeypadInput > KEYPAD_TIMEOUT)) {
    Serial.println("⏱ Keypad timeout - buffer cleared");
    keypadBuffer = "";
  }

  if (!wg.available()) return;

  uint8_t bitCount = wg.getWiegandType();
  uint32_t code = wg.getCode();

  Serial.printf("\n>>> Wiegand input: %u bits, raw code=%lu (0x%X)\n", bitCount, code, code);

  if (bitCount == 4) {
    handleKeypadEvent(code);
  } else if (bitCount == 8) {
    handleEscapeEvent(code);
  } else if (bitCount == 26) {
    handleWiegand26(code);
  } else if (bitCount >= 32) {
    handleWiegandRfid(code, bitCount);
  } else {
    Serial.printf("❓ Unknown Wiegand format: %u bits, code=%lu (0x%X)\n", bitCount, code, code);
  }

  Serial.println();
}
