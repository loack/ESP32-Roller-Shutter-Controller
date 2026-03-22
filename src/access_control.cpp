#include "access_control.h"
#include <Preferences.h>
#include "config.h"

// ===== VARIABLES GLOBALES =====
AccessCode accessCodes[50];
AccessLog accessLogs[100];
int accessCodeCount = 0;
int logIndex = 0;

bool learningMode = false;
unsigned long learningModeStart = 0;
const unsigned long LEARNING_TIMEOUT = 60000;  // 60 secondes
uint8_t learningType = 0;  // 0=Keypad, 1=RFID, 2=Fingerprint
String learningName = "";

// Dépendances externes
extern Preferences preferences;
extern void publishMQTT(const char* topic, const char* payload);

// ===== PERSISTANCE =====
void loadAccessCodes() {
  accessCodeCount = preferences.getInt("codeCount", 0);
  if (accessCodeCount > 50) accessCodeCount = 0;

  for (int i = 0; i < accessCodeCount; i++) {
    String key = "code" + String(i);
    preferences.getBytes(key.c_str(), &accessCodes[i], sizeof(AccessCode));
  }

  Serial.printf("✓ Loaded %d access codes from flash\n", accessCodeCount);
}

void saveAccessCodes() {
  preferences.putInt("codeCount", accessCodeCount);

  for (int i = 0; i < accessCodeCount; i++) {
    String key = "code" + String(i);
    preferences.putBytes(key.c_str(), &accessCodes[i], sizeof(AccessCode));
  }

  Serial.printf("✓ Saved %d access codes to flash\n", accessCodeCount);
}

// ===== VÉRIFICATION CODE =====
bool checkAccessCode(uint32_t code, uint8_t type) {
  for (int i = 0; i < accessCodeCount; i++) {
    if (accessCodes[i].active &&
        accessCodes[i].code == code &&
        accessCodes[i].type == type) {
      Serial.printf("✓ Code match found: %s (index %d)\n", accessCodes[i].name, i);
      return true;
    }
  }
  return false;
}

// ===== LOG D'ACCÈS =====
void addAccessLog(uint32_t code, bool granted, uint8_t type) {
  accessLogs[logIndex].timestamp = millis();
  accessLogs[logIndex].code = code;
  accessLogs[logIndex].granted = granted;
  accessLogs[logIndex].type = type;

  logIndex = (logIndex + 1) % 100;

  Serial.printf("Access log: code=%lu, granted=%d, type=%d\n", code, granted, type);
}

// ===== AJOUT / SUPPRESSION DE CODES =====
bool addNewAccessCode(uint32_t code, uint8_t type, const char* name) {
  for (int i = 0; i < accessCodeCount; i++) {
    if (accessCodes[i].code == code && accessCodes[i].type == type) {
      Serial.printf("⚠ Code already exists: %lu (type %d)\n", code, type);
      return false;
    }
  }

  if (accessCodeCount >= 50) {
    Serial.println("✗ Access codes list full (max 50)");
    return false;
  }

  accessCodes[accessCodeCount].code = code;
  accessCodes[accessCodeCount].type = type;
  strncpy(accessCodes[accessCodeCount].name, name, sizeof(accessCodes[accessCodeCount].name) - 1);
  accessCodes[accessCodeCount].name[sizeof(accessCodes[accessCodeCount].name) - 1] = '\0';
  accessCodes[accessCodeCount].active = true;

  accessCodeCount++;
  saveAccessCodes();

  Serial.printf("✓ New access code added: %s (code=%lu, type=%d)\n", name, code, type);

  char payload[256];
  snprintf(payload, sizeof(payload),
           "{\"action\":\"added\",\"code\":%lu,\"type\":%d,\"name\":\"%s\",\"total\":%d}",
           code, type, name, accessCodeCount);
  publishMQTT("codes", payload);

  return true;
}

bool removeAccessCode(uint32_t code, uint8_t type) {
  int foundIndex = -1;
  for (int i = 0; i < accessCodeCount; i++) {
    if (accessCodes[i].code == code && accessCodes[i].type == type) {
      foundIndex = i;
      break;
    }
  }

  if (foundIndex == -1) {
    Serial.printf("⚠ Code not found: %lu (type %d)\n", code, type);
    return false;
  }

  char removedName[32];
  strncpy(removedName, accessCodes[foundIndex].name, sizeof(removedName));

  for (int i = foundIndex; i < accessCodeCount - 1; i++) {
    accessCodes[i] = accessCodes[i + 1];
  }

  accessCodeCount--;
  saveAccessCodes();

  Serial.printf("✓ Access code removed: %s (code=%lu, type=%d)\n", removedName, code, type);

  char payload[256];
  snprintf(payload, sizeof(payload),
           "{\"action\":\"removed\",\"code\":%lu,\"type\":%d,\"name\":\"%s\",\"total\":%d}",
           code, type, removedName, accessCodeCount);
  publishMQTT("codes", payload);

  return true;
}

bool deleteAccessCode(int index) {
  if (index < 0 || index >= accessCodeCount) {
    Serial.printf("⚠ Invalid index for deletion: %d\n", index);
    return false;
  }

  char removedName[32];
  strncpy(removedName, accessCodes[index].name, sizeof(removedName));
  uint32_t removedCode = accessCodes[index].code;
  uint8_t removedType = accessCodes[index].type;

  for (int i = index; i < accessCodeCount - 1; i++) {
    accessCodes[i] = accessCodes[i + 1];
  }

  accessCodeCount--;
  saveAccessCodes();

  Serial.printf("✓ Access code removed at index %d: %s (code=%lu, type=%d)\n",
                index, removedName, removedCode, removedType);

  char payload[256];
  snprintf(payload, sizeof(payload),
           "{\"action\":\"removed\",\"code\":%lu,\"type\":%d,\"name\":\"%s\",\"total\":%d}",
           removedCode, removedType, removedName, accessCodeCount);
  publishMQTT("codes", payload);

  return true;
}

// ===== MODE APPRENTISSAGE =====
void startLearningMode(uint8_t type, const char* name) {
  learningMode = true;
  learningModeStart = millis();
  learningType = type;
  learningName = String(name);

  const char* typeNames[] = {"Keypad", "RFID", "Fingerprint"};
  Serial.printf("\n🎓 LEARNING MODE activated for %s\n", typeNames[type]);
  Serial.printf("Name: %s\n", name);
  Serial.println("Waiting for input... (60 seconds)");

  // Clignoter STATUS_LED pour signaler le mode apprentissage
  for (int i = 0; i < 5; i++) {
    digitalWrite(STATUS_LED, HIGH);
    delay(100);
    digitalWrite(STATUS_LED, LOW);
    delay(100);
  }

  char payload[256];
  snprintf(payload, sizeof(payload),
           "{\"learning\":true,\"type\":%d,\"name\":\"%s\",\"timeout\":60}",
           type, name);
  publishMQTT("status", payload);
}

void stopLearningMode() {
  if (learningMode) {
    learningMode = false;
    Serial.println("🎓 LEARNING MODE deactivated\n");
    publishMQTT("status", "{\"learning\":false}");
  }
}
