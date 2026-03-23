#include "wiegand_handler.h"
#include "access_control.h"
#include "config.h"
#include "log_manager.h"

// ===== VARIABLES GLOBALES =====
WIEGAND wg;

String keypadBuffer = "";
unsigned long lastKeypadInput = 0;
const unsigned long KEYPAD_TIMEOUT = 10000;  // 10 secondes

// Dépendances externes
extern Config config;
extern void activateRelay(bool open);
extern void publishMQTT(const char* topic, const char* payload);

// Machine d'état pour mode PIN+RFID (AUTH_MODE_PIN_RFID)
enum PendingAuthState { AUTH_IDLE, AUTH_WAITING_PIN, AUTH_WAITING_RFID };
static PendingAuthState authPendingState = AUTH_IDLE;
static uint32_t     authPendingCode = 0;       // code du 1er facteur
static unsigned long authPendingTime = 0;
const  unsigned long AUTH_PENDING_TIMEOUT = 30000;  // 30 secondes

// Filtre anti-parasite : timestamp de la dernière trame RFID reçue
// Certains lecteurs envoient une trame 4-bit (LED/buzzer) juste après un badge
static unsigned long lastRfidEvent = 0;
const  unsigned long RFID_KEYPAD_INHIBIT = 500;  // ignore clavier 500ms après un badge

// ===== INITIALISATION =====
void setupWiegand() {
  wg.begin(pins.wiegandD0, pins.wiegandD1);
  logMessage("[WG] Wiegand initialisé sur D0=" + String(pins.wiegandD0) + ", D1=" + String(pins.wiegandD1));
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
    // LED verte - 1 clignotement court
    digitalWrite(pins.readerLedGreen, HIGH);
    delay(50);
    digitalWrite(pins.readerLedGreen, LOW);
  } else {
    // LED rouge - 2 clignotements courts
    for (int i = 0; i < 2; i++) {
      digitalWrite(pins.readerLedRed, HIGH);
      delay(50);
      digitalWrite(pins.readerLedRed, LOW);
      delay(50);
    }
  }
}

// ===== CLAVIER : TRAITEMENT DU CODE SAISI =====
void processKeypadCode() {
  if (keypadBuffer.length() == 0) {
    logMessage("[WG][KEYPAD] Buffer vide");
    return;
  }

  uint32_t code = keypadBuffer.toInt();
  logPrintf("[WG][KEYPAD] Traitement code: %lu", code);

  // MODE APPRENTISSAGE pour clavier (prioritaire peu importe le mode auth)
  if (learningMode && learningType == 0) {
    addNewAccessCode(code, 0, learningName.c_str());
    stopLearningMode();
    blinkReaderLED(true);
    return;
  }

  // ===== AUTH MODE 0 : PIN seul =====
  if (config.authMode == AUTH_MODE_PIN_ONLY) {
    bool granted = checkAccessCode(code, 0);
    addAccessLog(code, granted, 0);
    if (granted) {
      logMessage("[WG][KEYPAD] PIN AUTORISE");
      blinkReaderLED(true);
      activateRelay(true);
      char payload[128];
      snprintf(payload, sizeof(payload), "{\"code\":%lu,\"granted\":true,\"type\":\"pin\"}", code);
      publishMQTT("access", payload);
    } else {
      logMessage("[WG][KEYPAD] PIN REFUSE");
      blinkReaderLED(false);
      char payload[128];
      snprintf(payload, sizeof(payload), "{\"code\":%lu,\"granted\":false,\"type\":\"pin\"}", code);
      publishMQTT("access", payload);
    }
    return;
  }

  // ===== AUTH MODE 1 : RFID seul — clavier ignoré =====
  if (config.authMode == AUTH_MODE_RFID_ONLY) {
    logMessage("[WG][KEYPAD] Mode RFID seul - clavier ignoré");
    blinkReaderLED(false);
    return;
  }

  // ===== AUTH MODE 2 : PIN + RFID =====
  if (authPendingState == AUTH_WAITING_PIN && millis() - authPendingTime < AUTH_PENDING_TIMEOUT) {
    // Badge RFID déjà présenté — valider la combinaison
    bool rfidOk = checkAccessCode(authPendingCode, 1);
    bool pinOk  = checkAccessCode(code, 0);
    bool granted = rfidOk && pinOk;
    addAccessLog(code, granted, 0);
    authPendingState = AUTH_IDLE;
    authPendingCode = 0;
    if (granted) {
      logMessage("[WG] PIN+RFID AUTORISE");
      blinkReaderLED(true);
      activateRelay(true);
      char payload[128];
      snprintf(payload, sizeof(payload), "{\"code\":%lu,\"granted\":true,\"type\":\"pin+rfid\"}", code);
      publishMQTT("access", payload);
    } else {
      logMessage("[WG] PIN+RFID REFUSE");
      blinkReaderLED(false);
      char payload[128];
      snprintf(payload, sizeof(payload), "{\"code\":%lu,\"granted\":false,\"type\":\"pin+rfid\"}", code);
      publishMQTT("access", payload);
    }
  } else {
    // Pas de badge en attente : stocker le PIN et attendre le RFID
    authPendingCode = code;
    authPendingTime = millis();
    authPendingState = AUTH_WAITING_RFID;
    blinkReaderLED(true);
    logPrintf("[WG] PIN saisi - présentez votre badge RFID dans %lus", AUTH_PENDING_TIMEOUT / 1000);
  }
}

// ===== GESTION TOUCHE CLAVIER (4 BITS) =====
static void handleKeypadEvent(uint32_t code) {
  // Ignorer les trames clavier qui arrivent juste après un badge (commandes LED/buzzer du lecteur)
  if (millis() - lastRfidEvent < RFID_KEYPAD_INHIBIT) {
    logPrintf("[WG][KEYPAD] Trame 4-bit ignorée (post-RFID inhibit): raw=%lu", code);
    return;
  }
  lastKeypadInput = millis();
  logPrintf("[WG][KEYPAD] Touche: raw=%lu, key=%s", code, getKeypadKeyLabel(code));

  // Touche # = validation (code 13)
  if (code == 13) {
    logPrintf("[WG][KEYPAD] # - Validation code: %s", keypadBuffer.c_str());
    processKeypadCode();
    keypadBuffer = "";
  }
  // Touche * = annulation (code 11, 14 ou 27)
  else if (code == 14 || code == 11 || code == 27) {
    logMessage("[WG][KEYPAD] * - Effacement buffer");
    keypadBuffer = "";
    blinkReaderLED(false);
  }
  // Chiffres 0-9
  else if (code <= 9) {
    keypadBuffer += String(code);
    logPrintf("[WG][KEYPAD] Chiffre ajouté, buffer=%s", keypadBuffer.c_str());

    // Limite à 10 chiffres
    if (keypadBuffer.length() > 10) {
      keypadBuffer = keypadBuffer.substring(1);
      logPrintf("[WG][KEYPAD] Buffer tronqué: %s", keypadBuffer.c_str());
    }
  } else {
    logPrintf("[WG][KEYPAD] Touche inconnue: %lu (key=%s)", code, getKeypadKeyLabel(code));
  }
}

// ===== GESTION TOUCHE ESC/CLEAR 8 BITS (code 27) =====
static void handleEscapeEvent(uint32_t code) {
  // Code 27 = ESC envoyé en 8 bits par certains claviers Wiegand
  if (code == 27) {
    logMessage("[WG][KEYPAD] ESC/CLEAR (code 27) - Buffer effacé");
    keypadBuffer = "";
    blinkReaderLED(false);
  } else {
    logPrintf("[WG][8-bit] Code inconnu: %lu (0x%02X)", code, code);
  }
}

// ===== GESTION BADGE/EMPREINTE RFID 26 BITS =====
static void handleWiegand26(uint32_t code) {
  // Code < 100 → empreinte digitale validée par le lecteur
  if (code < 100) {
    logPrintf("[WG] Empreinte #%lu validée par le lecteur", code);

    if (learningMode && learningType == 2) {
      addNewAccessCode(code, 2, learningName.c_str());
      stopLearningMode();
      blinkReaderLED(true);
      return;
    }

    bool granted = checkAccessCode(code, 2);
    addAccessLog(code, granted, 2);

    if (granted) {
      logMessage("[WG] Empreinte AUTORISEE");
      blinkReaderLED(true);
      activateRelay(true);

      char payload[128];
      snprintf(payload, sizeof(payload),
               "{\"code\":%lu,\"granted\":true,\"type\":\"fingerprint\",\"bits\":26}",
               code);
      publishMQTT("access", payload);
    } else {
      logMessage("[WG] Empreinte REFUSEE");
      blinkReaderLED(false);

      char payload[128];
      snprintf(payload, sizeof(payload),
               "{\"code\":%lu,\"granted\":false,\"type\":\"fingerprint\",\"reason\":\"not_authorized\",\"bits\":26}",
               code);
      publishMQTT("access", payload);
    }
  }
  // Code ≥ 100 → badge RFID WG26
  else {
    logPrintf("[WG] Badge RFID WG26: %lu (0x%06X)", code, code);

    if (learningMode && learningType == 1) {
      addNewAccessCode(code, 1, learningName.c_str());
      stopLearningMode();
      blinkReaderLED(true);
      return;
    }

    // AUTH MODE 0 : PIN seul — badge ignoré
    if (config.authMode == AUTH_MODE_PIN_ONLY) {
      logMessage("[WG] Mode PIN seul - badge ignoré");
      blinkReaderLED(false);
      return;
    }

    // AUTH MODE 1 : RFID seul
    if (config.authMode == AUTH_MODE_RFID_ONLY) {
      bool granted = checkAccessCode(code, 1);
      addAccessLog(code, granted, 1);
      if (granted) {
        logMessage("[WG] RFID WG26 AUTORISE");
        blinkReaderLED(true);
        activateRelay(true);
        char payload[128];
        snprintf(payload, sizeof(payload),
                 "{\"code\":%lu,\"granted\":true,\"type\":\"rfid\",\"bits\":26}", code);
        publishMQTT("access", payload);
      } else {
        logMessage("[WG] RFID WG26 REFUSE");
        blinkReaderLED(false);
        char payload[128];
        snprintf(payload, sizeof(payload),
                 "{\"code\":%lu,\"granted\":false,\"type\":\"rfid\",\"bits\":26}", code);
        publishMQTT("access", payload);
      }
      return;
    }

    // AUTH MODE 2 : PIN + RFID
    if (authPendingState == AUTH_WAITING_RFID && millis() - authPendingTime < AUTH_PENDING_TIMEOUT) {
      bool pinOk  = checkAccessCode(authPendingCode, 0);
      bool rfidOk = checkAccessCode(code, 1);
      bool granted = pinOk && rfidOk;
      addAccessLog(code, granted, 1);
      authPendingState = AUTH_IDLE;
      authPendingCode = 0;
      if (granted) {
        logMessage("[WG] RFID+PIN WG26 AUTORISE");
        blinkReaderLED(true);
        activateRelay(true);
        char payload[128];
        snprintf(payload, sizeof(payload),
                 "{\"code\":%lu,\"granted\":true,\"type\":\"pin+rfid\",\"bits\":26}", code);
        publishMQTT("access", payload);
      } else {
        logMessage("[WG] RFID+PIN WG26 REFUSE");
        blinkReaderLED(false);
        char payload[128];
        snprintf(payload, sizeof(payload),
                 "{\"code\":%lu,\"granted\":false,\"type\":\"pin+rfid\",\"bits\":26}", code);
        publishMQTT("access", payload);
      }
    } else {
      authPendingCode = code;
      authPendingTime = millis();
      authPendingState = AUTH_WAITING_PIN;
      blinkReaderLED(true);
      logPrintf("[WG] Badge détecté (WG26) - entrez votre PIN dans %lus", AUTH_PENDING_TIMEOUT / 1000);
    }
  }
}

// ===== GESTION BADGE RFID 34 BITS (WG34 : 1 + 16 facility + 16 card + 1 parité) =====
static void handleWiegand34(uint32_t code) {
  uint16_t facilityCode = (code >> 16) & 0xFFFF;
  uint16_t cardNumber   = code & 0xFFFF;
  logPrintf("[WG] Badge RFID WG34: UID=%lu (0x%08X) | facility=%u | card=%u",
            code, code, facilityCode, cardNumber);

  if (learningMode && learningType == 1) {
    addNewAccessCode(code, 1, learningName.c_str());
    stopLearningMode();
    blinkReaderLED(true);
    return;
  }

  // AUTH MODE 0 : PIN seul — badge ignoré
  if (config.authMode == AUTH_MODE_PIN_ONLY) {
    logMessage("[WG] Mode PIN seul - badge WG34 ignoré");
    blinkReaderLED(false);
    return;
  }

  // AUTH MODE 1 : RFID seul
  if (config.authMode == AUTH_MODE_RFID_ONLY) {
    bool granted = checkAccessCode(code, 1);
    addAccessLog(code, granted, 1);
    if (granted) {
      logMessage("[WG] RFID WG34 AUTORISE");
      blinkReaderLED(true);
      activateRelay(true);
      char payload[192];
      snprintf(payload, sizeof(payload),
               "{\"uid\":%lu,\"facility_code\":%u,\"card_number\":%u,\"granted\":true,\"type\":\"rfid\",\"bits\":34}",
               code, facilityCode, cardNumber);
      publishMQTT("access", payload);
    } else {
      logMessage("[WG] RFID WG34 REFUSE");
      blinkReaderLED(false);
      char payload[192];
      snprintf(payload, sizeof(payload),
               "{\"uid\":%lu,\"facility_code\":%u,\"card_number\":%u,\"granted\":false,\"type\":\"rfid\",\"bits\":34}",
               code, facilityCode, cardNumber);
      publishMQTT("access", payload);
    }
    return;
  }

  // AUTH MODE 2 : PIN + RFID
  if (authPendingState == AUTH_WAITING_RFID && millis() - authPendingTime < AUTH_PENDING_TIMEOUT) {
    bool pinOk  = checkAccessCode(authPendingCode, 0);
    bool rfidOk = checkAccessCode(code, 1);
    bool granted = pinOk && rfidOk;
    addAccessLog(code, granted, 1);
    authPendingState = AUTH_IDLE;
    authPendingCode = 0;
    if (granted) {
      logMessage("[WG] RFID+PIN WG34 AUTORISE");
      blinkReaderLED(true);
      activateRelay(true);
      char payload[192];
      snprintf(payload, sizeof(payload),
               "{\"uid\":%lu,\"facility_code\":%u,\"card_number\":%u,\"granted\":true,\"type\":\"pin+rfid\",\"bits\":34}",
               code, facilityCode, cardNumber);
      publishMQTT("access", payload);
    } else {
      logMessage("[WG] RFID+PIN WG34 REFUSE");
      blinkReaderLED(false);
      char payload[192];
      snprintf(payload, sizeof(payload),
               "{\"uid\":%lu,\"facility_code\":%u,\"card_number\":%u,\"granted\":false,\"type\":\"pin+rfid\",\"bits\":34}",
               code, facilityCode, cardNumber);
      publishMQTT("access", payload);
    }
  } else {
    authPendingCode = code;
    authPendingTime = millis();
    authPendingState = AUTH_WAITING_PIN;
    blinkReaderLED(true);
    logPrintf("[WG] Badge WG34 détecté (facility=%u, card=%u) - entrez votre PIN dans %lus",
              facilityCode, cardNumber, AUTH_PENDING_TIMEOUT / 1000);
  }
}

// ===== POINT D'ENTREE PRINCIPAL =====
void handleWiegandInput() {
  // Timeout mode apprentissage
  if (learningMode && (millis() - learningModeStart > LEARNING_TIMEOUT)) {
    logMessage("[WG] Timeout mode apprentissage");
    stopLearningMode();
  }

  // Timeout buffer clavier
  if (keypadBuffer.length() > 0 && (millis() - lastKeypadInput > KEYPAD_TIMEOUT)) {
    logMessage("[WG][KEYPAD] Timeout - buffer effacé");
    keypadBuffer = "";
  }

  // Timeout état d'attente PIN+RFID
  if (authPendingState != AUTH_IDLE && (millis() - authPendingTime > AUTH_PENDING_TIMEOUT)) {
    logMessage("[WG] Auth timeout - facteur en attente annulé");
    authPendingState = AUTH_IDLE;
    authPendingCode = 0;
    blinkReaderLED(false);
  }

  if (!wg.available()) return;

  uint8_t bitCount = wg.getWiegandType();
  uint32_t code = wg.getCode();

  // ===== DEBUG RAW =====
  logPrintf("[WG][RAW] bits=%u  dec=%lu  hex=0x%08lX  B3=%02X B2=%02X B1=%02X B0=%02X",
            bitCount, code, code,
            (uint8_t)((code >> 24) & 0xFF),
            (uint8_t)((code >> 16) & 0xFF),
            (uint8_t)((code >>  8) & 0xFF),
            (uint8_t)( code        & 0xFF));

  // Marquer le timestamp pour inhiber les trames clavier parasites post-badge
  if (bitCount >= 24) lastRfidEvent = millis();

  if (bitCount == 4) {
    handleKeypadEvent(code);
  } else if (bitCount == 8) {
    handleEscapeEvent(code);
  } else if (bitCount == 26) {
    handleWiegand26(code);
  } else if (bitCount == 34) {
    handleWiegand34(code);
  } else {
    logPrintf("[WG][ERR] Trame %u bits rejetée (erreur comm) - code=%lu (0x%08lX)", bitCount, code, code);
  }
}
