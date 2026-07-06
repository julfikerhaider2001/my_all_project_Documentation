#include <EEPROM.h>
#include <SHA256.h>
#include <SoftwareSerial.h>
#include <avr/pgmspace.h>
#include "boards/rev-a/config.h"
#include "provisioning.h"

SoftwareSerial sim800(SIM800_RX_PIN, SIM800_TX_PIN);

const unsigned long PAIR_WINDOW_MS = 180000UL;
const uint32_t STATE_MAGIC = 0x42463131UL;

struct StoredState {
  uint32_t magic;
  uint8_t paired;
  uint8_t commandKey[32];
  uint32_t lastCounter;
  char ownerNumber[20];
  char pairingAppNonce[17];
  char pairingDeviceNonce[17];
  uint16_t checksum;
};

StoredState state;
SHA256 sha256;
char lineBuffer[190];
uint8_t lineLength = 0;
bool awaitingSmsBody = false;
char smsSender[20];
unsigned long pairingUntil = 0;
unsigned long buttonPressedAt = 0;
bool relayOn = false;

uint16_t crc16(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFF;
  while (length--) {
    crc ^= *data++;
    for (uint8_t i = 0; i < 8; ++i) {
      crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
  }
  return crc;
}

void clearSensitive(void *data, size_t length) {
  volatile uint8_t *bytes = static_cast<volatile uint8_t *>(data);
  while (length--) *bytes++ = 0;
}

void saveState() {
  state.magic = STATE_MAGIC;
  state.checksum = crc16(reinterpret_cast<const uint8_t *>(&state), sizeof(state) - sizeof(state.checksum));
  EEPROM.put(0, state);
}

void loadState() {
  EEPROM.get(0, state);
  const uint16_t expected = crc16(
    reinterpret_cast<const uint8_t *>(&state),
    sizeof(state) - sizeof(state.checksum)
  );
  if (state.magic != STATE_MAGIC || state.checksum != expected || state.paired != 1) {
    memset(&state, 0, sizeof(state));
  }
}

void factoryReset() {
  memset(&state, 0, sizeof(state));
  saveState();
  pairingUntil = 0;
  relayOn = false;
  digitalWrite(RELAY_PIN, RELAY_INACTIVE_LEVEL);
}

bool decodeHex(const char *hex, uint8_t *output, size_t outputLength) {
  if (strlen(hex) != outputLength * 2) return false;
  for (size_t i = 0; i < outputLength; ++i) {
    char pair[3] = { hex[i * 2], hex[i * 2 + 1], '\0' };
    char *end = nullptr;
    const long value = strtol(pair, &end, 16);
    if (end == nullptr || *end != '\0') return false;
    output[i] = static_cast<uint8_t>(value);
  }
  return true;
}

void bytesToHex(const uint8_t *input, size_t length, char *output) {
  const char alphabet[] = "0123456789ABCDEF";
  for (size_t i = 0; i < length; ++i) {
    output[i * 2] = alphabet[input[i] >> 4];
    output[i * 2 + 1] = alphabet[input[i] & 0x0F];
  }
  output[length * 2] = '\0';
}

void hmacSha256(const uint8_t *key, size_t keyLength, const char *message, uint8_t *output, size_t outputLength) {
  sha256.resetHMAC(key, keyLength);
  sha256.update(message, strlen(message));
  sha256.finalizeHMAC(key, keyLength, output, outputLength);
}

bool constantTimeEquals(const char *left, const char *right) {
  if (strlen(left) != strlen(right)) return false;
  uint8_t difference = 0;
  for (size_t i = 0; left[i] != '\0'; ++i) {
    difference |= static_cast<uint8_t>(left[i] ^ right[i]);
  }
  return difference == 0;
}

void proofHex(const uint8_t *key, size_t keyLength, const char *message, char output[17]) {
  uint8_t proof[8];
  hmacSha256(key, keyLength, message, proof, sizeof(proof));
  bytesToHex(proof, sizeof(proof), output);
  clearSensitive(proof, sizeof(proof));
}

bool samePhone(const char *left, const char *right) {
  char a[16] = {0};
  char b[16] = {0};
  uint8_t ai = 0;
  uint8_t bi = 0;
  for (size_t i = 0; left[i] && ai < sizeof(a) - 1; ++i) if (isDigit(left[i])) a[ai++] = left[i];
  for (size_t i = 0; right[i] && bi < sizeof(b) - 1; ++i) if (isDigit(right[i])) b[bi++] = right[i];
  if (ai < 10 || bi < 10) return strcmp(a, b) == 0;
  return strcmp(a + ai - 10, b + bi - 10) == 0;
}

bool pairingWindowOpen() {
  return pairingUntil != 0 && static_cast<long>(pairingUntil - millis()) > 0;
}

void createDeviceNonce(char output[17]) {
  uint8_t bytes[8];
  for (uint8_t i = 0; i < sizeof(bytes); ++i) {
    unsigned long mixed = micros() ^ (static_cast<unsigned long>(analogRead(A0)) << 10) ^ random(0x7FFFFFFF);
    bytes[i] = static_cast<uint8_t>(mixed >> ((i & 3) * 8));
    delay(3);
  }
  bytesToHex(bytes, sizeof(bytes), output);
  clearSensitive(bytes, sizeof(bytes));
}

void sendSms(const char *number, const char *body) {
  sim800.print(F("AT+CMGS=\""));
  sim800.print(number);
  sim800.println(F("\""));
  delay(400);
  sim800.print(body);
  sim800.write(26);
  delay(1200);
}

void rejectMessage(const __FlashStringHelper *reason) {
  Serial.print(F("Rejected SMS: "));
  Serial.println(reason);
}

void sendPairingAck(const char *number, const char *appNonce, const char *deviceNonce) {
  char ackMessage[96];
  snprintf(ackMessage, sizeof(ackMessage), "P2|%s|%s|%s|OK", DEVICE_ID, appNonce, deviceNonce);
  char ackProof[17];
  proofHex(state.commandKey, sizeof(state.commandKey), ackMessage, ackProof);
  char sms[150];
  snprintf(sms, sizeof(sms), "BF1 P2 %s %s %s OK %s", DEVICE_ID, appNonce, deviceNonce, ackProof);
  sendSms(number, sms);
}

void handlePairRequest(char *tokens[], uint8_t count, const char *sender) {
  if (count != 5 || strcmp(tokens[2], DEVICE_ID) != 0) return;
  if (state.paired == 1) {
    if (samePhone(sender, state.ownerNumber) && strcmp(tokens[3], state.pairingAppNonce) == 0) {
      sendPairingAck(sender, state.pairingAppNonce, state.pairingDeviceNonce);
      Serial.println(F("Pairing acknowledgement repeated"));
    } else {
      rejectMessage(F("already paired"));
    }
    return;
  }
  if (!pairingWindowOpen()) {
    rejectMessage(F("pairing window closed"));
    return;
  }
  if (strlen(tokens[3]) != 16 || strlen(tokens[4]) != 16) {
    rejectMessage(F("invalid pairing fields"));
    return;
  }

  char claimHex[33];
  strcpy_P(claimHex, CLAIM_CODE_HEX);
  uint8_t claimKey[16];
  if (!decodeHex(claimHex, claimKey, sizeof(claimKey))) return;

  char proofMessage[64];
  snprintf(proofMessage, sizeof(proofMessage), "P1|%s|%s", DEVICE_ID, tokens[3]);
  char expectedProof[17];
  proofHex(claimKey, sizeof(claimKey), proofMessage, expectedProof);
  if (!constantTimeEquals(expectedProof, tokens[4])) {
    rejectMessage(F("invalid pairing proof"));
    clearSensitive(claimKey, sizeof(claimKey));
    clearSensitive(claimHex, sizeof(claimHex));
    return;
  }

  char deviceNonce[17];
  createDeviceNonce(deviceNonce);
  char keyMessage[80];
  snprintf(keyMessage, sizeof(keyMessage), "KEY|%s|%s|%s", DEVICE_ID, tokens[3], deviceNonce);
  hmacSha256(claimKey, sizeof(claimKey), keyMessage, state.commandKey, sizeof(state.commandKey));

  state.paired = 1;
  state.lastCounter = 0;
  strncpy(state.ownerNumber, sender, sizeof(state.ownerNumber) - 1);
  state.ownerNumber[sizeof(state.ownerNumber) - 1] = '\0';
  strncpy(state.pairingAppNonce, tokens[3], sizeof(state.pairingAppNonce) - 1);
  state.pairingAppNonce[sizeof(state.pairingAppNonce) - 1] = '\0';
  strncpy(state.pairingDeviceNonce, deviceNonce, sizeof(state.pairingDeviceNonce) - 1);
  state.pairingDeviceNonce[sizeof(state.pairingDeviceNonce) - 1] = '\0';
  saveState();
  pairingUntil = 0;

  sendPairingAck(sender, tokens[3], deviceNonce);

  clearSensitive(claimKey, sizeof(claimKey));
  clearSensitive(claimHex, sizeof(claimHex));
  Serial.println(F("Pairing completed"));
}

void sendCommandAck(const char *sender, const char *command, uint32_t counter, const char *status) {
  char message[80];
  snprintf(message, sizeof(message), "ACK|%s|%s|%lu|%s", DEVICE_ID, command, counter, status);
  char proof[17];
  proofHex(state.commandKey, sizeof(state.commandKey), message, proof);
  char sms[140];
  snprintf(sms, sizeof(sms), "BF1 ACK %s %s %lu %s %s", DEVICE_ID, command, counter, status, proof);
  sendSms(sender, sms);
}

void handleCommand(char *tokens[], uint8_t count, const char *sender) {
  if (count != 6 || strcmp(tokens[2], DEVICE_ID) != 0) return;
  if (state.paired != 1 || !samePhone(sender, state.ownerNumber)) {
    rejectMessage(F("unpaired or wrong sender"));
    return;
  }
  const bool supported = strcmp(tokens[3], "ON") == 0 || strcmp(tokens[3], "OFF") == 0 || strcmp(tokens[3], "STATUS") == 0;
  if (!supported) {
    rejectMessage(F("unsupported command"));
    return;
  }
  char *counterEnd = nullptr;
  const unsigned long counter = strtoul(tokens[4], &counterEnd, 10);
  if (counterEnd == nullptr || *counterEnd != '\0' || counter <= state.lastCounter || strlen(tokens[5]) != 16) {
    rejectMessage(F("invalid or replayed counter"));
    return;
  }

  char proofMessage[72];
  snprintf(proofMessage, sizeof(proofMessage), "CMD|%s|%s|%lu", DEVICE_ID, tokens[3], counter);
  char expectedProof[17];
  proofHex(state.commandKey, sizeof(state.commandKey), proofMessage, expectedProof);
  if (!constantTimeEquals(expectedProof, tokens[5])) {
    rejectMessage(F("invalid command proof"));
    return;
  }

  state.lastCounter = counter;
  saveState();
  if (strcmp(tokens[3], "ON") == 0) relayOn = true;
  if (strcmp(tokens[3], "OFF") == 0) relayOn = false;
  digitalWrite(RELAY_PIN, relayOn ? RELAY_ACTIVE_LEVEL : RELAY_INACTIVE_LEVEL);
  sendCommandAck(sender, tokens[3], counter, "OK");
  Serial.println(F("Authenticated command executed"));
}

void processSms(char *body, const char *sender) {
  char *tokens[8];
  uint8_t count = 0;
  char *token = strtok(body, " ");
  while (token != nullptr && count < 8) {
    tokens[count++] = token;
    token = strtok(nullptr, " ");
  }
  if (count < 2 || strcmp(tokens[0], "BF1") != 0) return;
  if (strcmp(tokens[1], "P1") == 0) handlePairRequest(tokens, count, sender);
  if (strcmp(tokens[1], "CMD") == 0) handleCommand(tokens, count, sender);
}

void processModemLine(char *line) {
  if (awaitingSmsBody) {
    awaitingSmsBody = false;
    processSms(line, smsSender);
    return;
  }
  if (strncmp(line, "+CMT:", 5) == 0) {
    const char *firstQuote = strchr(line, '"');
    const char *secondQuote = firstQuote ? strchr(firstQuote + 1, '"') : nullptr;
    if (firstQuote && secondQuote) {
      const size_t length = min(static_cast<size_t>(secondQuote - firstQuote - 1), sizeof(smsSender) - 1);
      memcpy(smsSender, firstQuote + 1, length);
      smsSender[length] = '\0';
      awaitingSmsBody = true;
    }
  }
}

void readModem() {
  while (sim800.available()) {
    const char value = static_cast<char>(sim800.read());
    if (value == '\r') continue;
    if (value == '\n') {
      if (lineLength > 0) {
        lineBuffer[lineLength] = '\0';
        processModemLine(lineBuffer);
        lineLength = 0;
      }
    } else if (lineLength < sizeof(lineBuffer) - 1) {
      lineBuffer[lineLength++] = value;
    } else {
      lineLength = 0;
    }
  }
}

void updatePairButton() {
  const bool pressed = digitalRead(PAIR_BUTTON_PIN) == LOW;
  if (pressed && buttonPressedAt == 0) buttonPressedAt = millis();
  if (!pressed) {
    if (buttonPressedAt != 0 && state.paired != 1 && millis() - buttonPressedAt >= 2000UL) {
      pairingUntil = millis() + PAIR_WINDOW_MS;
      Serial.println(F("Pairing window open for 3 minutes"));
    }
    buttonPressedAt = 0;
  }
  digitalWrite(STATUS_LED_PIN, pairingWindowOpen() ? ((millis() / 250) & 1) : state.paired);
}

void checkFactoryResetAtBoot() {
  if (digitalRead(PAIR_BUTTON_PIN) != LOW) return;
  const unsigned long started = millis();
  while (digitalRead(PAIR_BUTTON_PIN) == LOW && millis() - started < 8000UL) {
    digitalWrite(STATUS_LED_PIN, (millis() / 150) & 1);
    delay(10);
  }
  if (millis() - started >= 8000UL) {
    factoryReset();
    Serial.println(F("Factory pairing state cleared"));
  }
}

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(PAIR_BUTTON_PIN, INPUT_PULLUP);
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_INACTIVE_LEVEL);
  Serial.begin(9600);
  loadState();
  checkFactoryResetAtBoot();

  randomSeed(analogRead(A0) ^ micros());
  sim800.begin(SIM800_BAUD);
  delay(2500);
  sim800.println(F("AT"));
  delay(300);
  sim800.println(F("AT+CMGF=1"));
  delay(300);
  sim800.println(F("AT+CNMI=2,2,0,0,0"));
  delay(300);
  Serial.println(state.paired == 1 ? F("Borgo controller ready: paired") : F("Borgo controller ready: unclaimed"));
}

void loop() {
  readModem();
  updatePairButton();
}
