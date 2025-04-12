#include <Arduino.h>
#include <EEPROM.h>
#include "EEPROMUtils.h"
#include <Controller.h>

#define SERIAL_BAUD 115200

#if defined(PLACA_MEGA)
  #define EEPROM_MAX_ENTRIES 64
#elif defined(PLACA_NANO) || defined(PLACA_UNO)
  #define EEPROM_MAX_ENTRIES 16
#else
  #define EEPROM_MAX_ENTRIES 16
#endif

#define EEPROM_MAX_SIZE 1024  // Asegura compatibilidad con EEPROM.begin()

struct PinConfig {
  uint8_t pin;
  uint8_t type; // 0=SWITCH, 1=BUTTON, 2=OUTPUT, 3=POT
  char param[20];
  char value1[4];
  char value2[4];
};

Switch* switches[EEPROM_MAX_ENTRIES];
PushButton* buttons[EEPROM_MAX_ENTRIES];
PotFilter* pots[EEPROM_MAX_ENTRIES];
OutputManager* outputs[EEPROM_MAX_ENTRIES];

int switchCount = 0, buttonCount = 0, potCount = 0, outputCount = 0;
bool modoConfig = false;
bool borrarEEPROMPendiente = false;
bool borrarPinPendiente = false;
String borrarValorPendiente = "";
String renombrarDesde = "";
String renombrarHacia = "";
bool renombrarPendiente = false;

void handleLine(const char* command, const char* value);
SerialInterface serialInterface(Serial, handleLine);

int analogPinFromString(const char* str) {
  if (str[0] == 'A') {
    return A0 + atoi(str + 1);
  }
  return atoi(str);
}

bool isSameConfig(const PinConfig& a, const PinConfig& b) {
  return a.pin == b.pin && a.type == b.type &&
        strncmp(a.param, b.param, 20) == 0 &&
        strncmp(a.value1, b.value1, 4) == 0 &&
        strncmp(a.value2, b.value2, 4) == 0;
}

void loadConfigFromEEPROM() {
  uint8_t count = EEPROM.read(0);
  for (int i = 0; i < count && i < EEPROM_MAX_ENTRIES; i++) {
    PinConfig cfg;
    EEPROM.get(1 + i * sizeof(PinConfig), cfg);

    switch (cfg.type) {
      case 0:
        switches[switchCount++] = new Switch(cfg.pin, cfg.param, cfg.value1, cfg.value2);
        Serial.print("register("); Serial.print(cfg.param); Serial.print(") on SWITCH PIN "); Serial.println(cfg.pin);
        break;
      case 1:
        buttons[buttonCount++] = new PushButton(cfg.pin, cfg.param, cfg.value1, cfg.value2);
        Serial.print("register("); Serial.print(cfg.param); Serial.print(") on BUTTON PIN "); Serial.println(cfg.pin);
        break;
      case 2:
        outputs[outputCount++] = new OutputManager(cfg.param, cfg.pin);
        Serial.print("register("); Serial.print(cfg.param); Serial.print(") on OUTPUT PIN "); Serial.println(cfg.pin);
        break;
      case 3:
        pots[potCount++] = new PotFilter(cfg.pin, 0, 1023, atoi(cfg.value2), atof(cfg.value1), atof(cfg.value2), 10);
        Serial.print("register("); Serial.print(cfg.param); Serial.print(") on POT PIN "); Serial.println(cfg.pin);
        break;
    }
  }
}

void savePinConfig(const String& tipo, int pin, const char* param, const char* v1, const char* v2) {
  uint8_t count = EEPROM.read(0);
  PinConfig cfg;
  cfg.pin = pin;
  strncpy(cfg.param, param, sizeof(cfg.param));
  strncpy(cfg.value1, v1, sizeof(cfg.value1));
  strncpy(cfg.value2, v2, sizeof(cfg.value2));

  if (tipo == "SWITCH") cfg.type = 0;
  else if (tipo == "BUTTON") cfg.type = 1;
  else if (tipo == "OUTPUT") cfg.type = 2;
  else if (tipo == "POT") cfg.type = 3;
  else return;

  for (int i = 0; i < count; i++) {
    PinConfig existing;
    EEPROM.get(1 + i * sizeof(PinConfig), existing);
    if (existing.pin == cfg.pin && existing.type == cfg.type) {
      if (!isSameConfig(existing, cfg)) {
        EEPROM.put(1 + i * sizeof(PinConfig), cfg);
        Serial.print("ACTUALIZADO PIN ");
        Serial.println(cfg.pin);
      } else {
        Serial.print("SIN CAMBIOS EN PIN ");
        Serial.println(cfg.pin);
      }
      return;
    }
  }

  if (count < EEPROM_MAX_ENTRIES) {
    EEPROM.put(1 + count * sizeof(PinConfig), cfg);
    EEPROM.write(0, count + 1);
    Serial.print("AGREGADO PIN ");
    Serial.println(cfg.pin);
  }
}

void removePinConfig(uint8_t pin) {
  uint8_t count = EEPROM.read(0);
  for (int i = 0; i < count; i++) {
    PinConfig cfg;
    EEPROM.get(1 + i * sizeof(PinConfig), cfg);
    if (cfg.pin == pin) {
      for (int j = i; j < count - 1; j++) {
        PinConfig next;
        EEPROM.get(1 + (j + 1) * sizeof(PinConfig), next);
        EEPROM.put(1 + j * sizeof(PinConfig), next);
      }
      EEPROM.write(0, count - 1);
      Serial.print("PIN ");
      Serial.print(pin);
      Serial.println(" ELIMINADO");
      return;
    }
  }
  Serial.print("PIN ");
  Serial.print(pin);
  Serial.println(" NO ENCONTRADO");
}

void removeParamConfig(const String& param) {
  uint8_t count = EEPROM.read(0);
  for (int i = 0; i < count; i++) {
    PinConfig cfg;
    EEPROM.get(1 + i * sizeof(PinConfig), cfg);
    if (String(cfg.param) == param) {
      for (int j = i; j < count - 1; j++) {
        PinConfig next;
        EEPROM.get(1 + (j + 1) * sizeof(PinConfig), next);
        EEPROM.put(1 + j * sizeof(PinConfig), next);
      }
      EEPROM.write(0, count - 1);
      Serial.print("PARAMETRO '");
      Serial.print(param);
      Serial.println("' ELIMINADO");
      return;
    }
  }
  Serial.print("PARAMETRO '");
  Serial.print(param);
  Serial.println("' NO ENCONTRADO");
}

void renameParam(const String& from, const String& to) {
  uint8_t count = EEPROM.read(0);
  bool updated = false;
  for (int i = 0; i < count; i++) {
    PinConfig cfg;
    EEPROM.get(1 + i * sizeof(PinConfig), cfg);
    if (String(cfg.param) == from) {
      strncpy(cfg.param, to.c_str(), sizeof(cfg.param));
      EEPROM.put(1 + i * sizeof(PinConfig), cfg);
      updated = true;
    }
  }
  if (updated) {
    Serial.print("PARAMETRO '");
    Serial.print(from);
    Serial.print("' RENOMBRADO A '");
    Serial.print(to);
    Serial.println("'");

    switchCount = buttonCount = outputCount = potCount = 0;
    for (int i = 0; i < EEPROM_MAX_ENTRIES; i++) {
      if (switches[i]) delete switches[i];
      if (buttons[i]) delete buttons[i];
      if (outputs[i]) delete outputs[i];
      if (pots[i]) delete pots[i];
      switches[i] = nullptr;
      buttons[i] = nullptr;
      outputs[i] = nullptr;
      pots[i] = nullptr;
    }
    loadConfigFromEEPROM();
    for (int i = 0; i < switchCount; i++) switches[i]->begin();
    for (int i = 0; i < buttonCount; i++) buttons[i]->begin();
    for (int i = 0; i < outputCount; i++) outputs[i]->begin();
  } else {
    Serial.print("PARAMETRO '");
    Serial.print(from);
    Serial.println("' NO ENCONTRADO");
  }
}

void parseAddLine(const String& line) {
  char tipo[10], p1[20], p2[20], p3[20], p4[20];
  int args = sscanf(line.c_str(), "%s %s %s %s %s", tipo, p1, p2, p3, p4);
  String tipoStr = String(tipo);
  tipoStr.toUpperCase();

  if (tipoStr == "SWITCH" && args == 5 && switchCount < EEPROM_MAX_ENTRIES) {
    switches[switchCount++] = new Switch(atoi(p1), p2, p3, p4);
  } else if (tipoStr == "BUTTON" && args == 5 && buttonCount < EEPROM_MAX_ENTRIES) {
    buttons[buttonCount++] = new PushButton(atoi(p1), p2, p3, p4);
  } else if (tipoStr == "POT" && args >= 4 && potCount < EEPROM_MAX_ENTRIES) {
    pots[potCount++] = new PotFilter(analogPinFromString(p1), 0, 1023, atoi(p4), atof(p2), atof(p3), 10);
  } else if (tipoStr == "OUTPUT" && args == 5 && outputCount < EEPROM_MAX_ENTRIES) {
    outputs[outputCount++] = new OutputManager(p2, atoi(p1));
  } else {
    Serial.println("ERROR: ADD inválido o fuera de límite");
  }
}
void handleLine(const char* command, const char* value) {
  String cmd = String(command);
  String val = value ? String(value) : "";

  if (cmd == "#CONFIG") {
    modoConfig = true;
    Serial.println("OK CONFIG MODE");
    return;
  }

  if (cmd == "#END") {
    modoConfig = false;
    Serial.println("OK CONFIG SAVED");
    Serial.println("Reiniciando...");
    delay(500);
    asm volatile("jmp 0");
    return;
  }

  if (cmd == "#CLEAR") {
    borrarEEPROMPendiente = true;
    Serial.println("¿Estás seguro de borrar la configuración?");
    Serial.println("Escribe #CONFIRM para proceder.");
    return;
  }

  if (cmd == "#CONFIRM" && borrarEEPROMPendiente) {
    for (int i = 0; i < EEPROM_MAX_SIZE; i++) EEPROM.write(i, 0xFF);
    EEPROM.write(0, 0);
    borrarEEPROMPendiente = false;
    Serial.println("EEPROM borrada.");
    return;
  }

  if (cmd == "#CONFIRM" && borrarPinPendiente && borrarValorPendiente.length()) {
    if (borrarValorPendiente.toInt() > 0 || borrarValorPendiente == "0") {
      removePinConfig(borrarValorPendiente.toInt());
    } else {
      removeParamConfig(borrarValorPendiente);
    }
    borrarPinPendiente = false;
    borrarValorPendiente = "";
    return;
  }

  if (cmd == "#DUMP") {
    uint8_t count = EEPROM.read(0);
    Serial.println("BEGIN CONFIG");
    int sw = 0, bt = 0, pt = 0, out = 0;
    for (int i = 0; i < count; i++) {
      PinConfig cfg;
      EEPROM.get(1 + i * sizeof(PinConfig), cfg);
      Serial.print("ADD ");
      if (cfg.type == 0) { Serial.print("SWITCH "); sw++; }
      else if (cfg.type == 1) { Serial.print("BUTTON "); bt++; }
      else if (cfg.type == 2) { Serial.print("OUTPUT "); out++; }
      else if (cfg.type == 3) { Serial.print("POT "); pt++; }
      Serial.print(cfg.pin); Serial.print(" ");
      Serial.print(cfg.param); Serial.print(" ");
      Serial.print(cfg.value1); Serial.print(" ");
      Serial.println(cfg.value2);
    }
    Serial.println("END CONFIG");
    Serial.print("{\"switches\":"); Serial.print(sw);
    Serial.print(",\"buttons\":"); Serial.print(bt);
    Serial.print(",\"pots\":"); Serial.print(pt);
    Serial.print(",\"outputs\":"); 
    Serial.print(out);
    Serial.println("}");
    return;
  }

  if (cmd == "#REMOVE") {
    borrarPinPendiente = true;
    borrarValorPendiente = val;
    Serial.print("¿Confirmar eliminación de '");
    Serial.print(val);
    Serial.println("'? Escribe #CONFIRM para proceder.");
    return;
  }

  if (cmd == "#RENAME") {
    int sep = val.indexOf(':');
    if (sep > 0) {
      renombrarDesde = val.substring(0, sep);
      renombrarHacia = val.substring(sep + 1);
      renombrarPendiente = true;
      Serial.print("¿Confirmar renombrado de '");
      Serial.print(renombrarDesde);
      Serial.print("' a '");
      Serial.print(renombrarHacia);
      Serial.println("'? Escribe #CONFIRM para proceder.");
    } else {
      Serial.println("ERROR: Formato debe ser #RENAME=ANTERIOR:NUEVO");
    }
    return;
  }

  if (cmd == "#CONFIRM" && renombrarPendiente) {
    renameParam(renombrarDesde, renombrarHacia);
    renombrarDesde = "";
    renombrarHacia = "";
    renombrarPendiente = false;
    return;
  }

  if (modoConfig && cmd.startsWith("ADD")) {
    char tipo[10], p1[20], p2[20], p3[20], p4[20];
    int args = sscanf(cmd.c_str() + 4, "%s %s %s %s %s", tipo, p1, p2, p3, p4);
    if (args == 5) {
      savePinConfig(tipo, atoi(p1), p2, p3, p4);
      Serial.print("OK ADD ");
      Serial.println(cmd.substring(4));
    } else {
      Serial.println("ERROR: Formato ADD incorrecto");
    }
    return;
  }

  for (int i = 0; i < outputCount; i++) {
    outputs[i]->outputDigital(command, value, 1);
  }

  Serial.print("ACK ");
  Serial.print(command);
  Serial.print(" = ");
  Serial.println(value);
}

// Como ya las tienes completas en tu versión anterior, simplemente inclúyelas
// en este archivo y elimina cualquier duplicación de definiciones o variables.

// setup y loop

void setup() {
  Serial.begin(SERIAL_BAUD);
  EEPROM.begin();
  serialInterface.begin();
  loadConfigFromEEPROM();

  for (int i = 0; i < switchCount; i++) switches[i]->begin();
  for (int i = 0; i < buttonCount; i++) buttons[i]->begin();
  for (int i = 0; i < outputCount; i++) outputs[i]->begin();
  Serial.println("Listo.");
}

void loop() {
  serialInterface.update();
  if (!modoConfig) {
    for (int i = 0; i < switchCount; i++) switches[i]->update();
    for (int i = 0; i < buttonCount; i++) buttons[i]->update();
    for (int i = 0; i < potCount; i++) pots[i]->update();
  }
}
