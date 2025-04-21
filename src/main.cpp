#include <Arduino.h>
#include <EEPROM.h>
#include "EEPROMUtils.h"
#include <Controller.h>
#include <avr/wdt.h>

#define SERIAL_BAUD 115200

#if defined(PLACA_MEGA)
  #define EEPROM_MAX_ENTRIES 64
#else
  #define EEPROM_MAX_ENTRIES 16
#endif

#define EEPROM_MAX_SIZE 1024

struct __attribute__((packed)) PinConfig {
  uint8_t pin;
  uint8_t type; // 0=SWITCH, 1=BUTTON, 2=OUTPUT, 3=POT
  char param[20];
  int minIn, maxIn;
  float minOut, maxOut;
  float suavizado;
  uint8_t modoEnvio;
  uint16_t intervalo;
};

// Instancias
Switch* switches[EEPROM_MAX_ENTRIES];
PushButton* buttons[EEPROM_MAX_ENTRIES];
PotFilter* pots[EEPROM_MAX_ENTRIES];
OutputManager* outputs[EEPROM_MAX_ENTRIES];

const char* switchParams[EEPROM_MAX_ENTRIES];
const char* buttonParams[EEPROM_MAX_ENTRIES];
const char* outputParams[EEPROM_MAX_ENTRIES];
const char* potParams[EEPROM_MAX_ENTRIES];

int switchCount = 0, buttonCount = 0, potCount = 0, outputCount = 0;
bool modoConfig = false;
bool bloqueado = false;

void handleLine(const char* command, const char* value);
SerialInterface serialInterface(Serial, handleLine);

int analogPinFromString(const char* str) {
  if (str[0] == 'A') return A0 + atoi(str + 1);
  return atoi(str);
}

void loadConfigFromEEPROM() {
  uint8_t count = EEPROM.read(0);
  Serial.print("Tamaño de PinConfig: ");
  Serial.println((int)sizeof(PinConfig));

  for (int i = 0; i < count && i < EEPROM_MAX_ENTRIES; i++) {
    PinConfig cfg;
    EEPROM.get(1 + i * sizeof(PinConfig), cfg);
    cfg.param[sizeof(cfg.param) - 1] = '\0';  // Seguridad

    switch (cfg.type) {
      case 0: { // SWITCH
        char* copy = strdup(cfg.param);
        switchParams[switchCount] = copy;
        switches[switchCount] = new Switch(cfg.pin, copy,
            String((int)cfg.minOut).c_str(), String((int)cfg.maxOut).c_str());
        switchCount++;
        break;
      }
      case 1: { // BUTTON
        char* copy = strdup(cfg.param);
        buttonParams[buttonCount] = copy;
        buttons[buttonCount] = new PushButton(cfg.pin, copy,
            String((int)cfg.minOut).c_str(), String((int)cfg.maxOut).c_str());
        buttonCount++;
        break;
      }
      case 2: { // OUTPUT
        char* copy = strdup(cfg.param);
        outputParams[outputCount] = copy;
        outputs[outputCount] = new OutputManager(copy, cfg.pin);
        outputCount++;
        break;
      }
      case 3: { // POT
        if (cfg.modoEnvio > 3) cfg.modoEnvio = 0;
        char* copy = strdup(cfg.param);
        potParams[potCount] = copy;
        pots[potCount] = new PotFilter(cfg.pin, copy, cfg.minIn, cfg.maxIn,
            cfg.minOut, cfg.maxOut, cfg.suavizado, cfg.modoEnvio, cfg.intervalo);
        potCount++;
        break;
      }
      default:
        Serial.print("⚠️ Tipo desconocido en EEPROM en entrada "); Serial.println(i);
        break;
    }

    // Debug por cada entrada
    Serial.print("DEBUG: Entrada "); Serial.print(i);
    Serial.print(" → type="); Serial.print(cfg.type);
    Serial.print(", pin="); Serial.print(cfg.pin);
    Serial.print(", param="); Serial.print(cfg.param);
    Serial.print(", minOut="); Serial.print(cfg.minOut, 3);
    Serial.print(", maxOut="); Serial.print(cfg.maxOut, 3);
    Serial.print(", modo="); Serial.println(cfg.modoEnvio);
  }
}

void savePinConfig(const String& tipo, int pin, const char* param, const char* v1, const char* v2) {
  const size_t structSize = sizeof(PinConfig);
  const uint8_t maxEntriesBySize = (EEPROM_MAX_SIZE - 1) / structSize;  // -1 por el byte de cantidad

  uint8_t count = EEPROM.read(0);
  if (count >= EEPROM_MAX_ENTRIES || count >= maxEntriesBySize) {
    Serial.println("❌ ERROR: EEPROM llena, no se puede guardar más configuraciones.");
    return;
  }

  PinConfig cfg;
  cfg.pin = pin;
  strncpy(cfg.param, param, sizeof(cfg.param));
  cfg.param[sizeof(cfg.param) - 1] = '\0'; // Seguridad
  cfg.minOut = atof(v1);
  cfg.maxOut = atof(v2);
  cfg.minIn = 0;
  cfg.maxIn = 1023;
  cfg.suavizado = 0.2;
  cfg.modoEnvio = 0;
  cfg.intervalo = 100;

  if (tipo == "SWITCH") cfg.type = 0;
  else if (tipo == "BUTTON") cfg.type = 1;
  else if (tipo == "OUTPUT") cfg.type = 2;
  else if (tipo == "POTENCIOMETRO" || tipo == "POT") cfg.type = 3;
  else return;

  // Si ya existe con mismo pin y tipo → actualizar
  for (int i = 0; i < count; i++) {
    PinConfig existing;
    EEPROM.get(1 + i * structSize, existing);
    if (existing.pin == cfg.pin && existing.type == cfg.type) {
      EEPROM.put(1 + i * structSize, cfg);
      Serial.println("ACTUALIZADO PIN " + String(cfg.pin));
      return;
    }
  }

  // Nuevo registro
  EEPROM.put(1 + count * structSize, cfg);
  EEPROM.write(0, count + 1);
  Serial.println("AGREGADO PIN " + String(cfg.pin));
}

void updatePotParam(String cmd) {
  char p1[10], field[10], a[10], b[10], c[10], d[10];
  int args = sscanf(cmd.c_str() + 4, "%s %s %s %s %s %s", p1, field, a, b, c, d);
  int pin = analogPinFromString(p1);
  uint8_t count = EEPROM.read(0);

  for (int i = 0; i < count; i++) {
    PinConfig cfg;
    EEPROM.get(1 + i * sizeof(PinConfig), cfg);
    if (cfg.pin == pin && cfg.type == 3) {
      String f = String(field);
      if (f == "SCALE" && args >= 6) {
        cfg.minIn = atoi(a); cfg.maxIn = atoi(b);
        cfg.minOut = atof(c); cfg.maxOut = atof(d);
      } else if (f == "SMOOTH" && args >= 3) {
        cfg.suavizado = atof(a);
      } else if (f == "MODE" && args >= 3) {
        if (String(a) == "CONTINUO") cfg.modoEnvio = 0;
        else if (String(a) == "CAMBIO") cfg.modoEnvio = 1;
        else if (String(a) == "INTERVALO") {
          cfg.modoEnvio = 2;
          cfg.intervalo = atoi(b);
        } else if (String(a) == "MANUAL") cfg.modoEnvio = 3;
      } else {
        Serial.println("ERROR: CFG inválido");
        return;
      }
      EEPROM.put(1 + i * sizeof(PinConfig), cfg);
      Serial.println("CFG ACTUALIZADO EN PIN " + String(pin));
      return;
    }
  }

  Serial.println("ERROR: Pin no encontrado o no es POT");
}

void handleLine(const char* command, const char* value) {
  if (!command || strlen(command) == 0) return;

  bloqueado = true;

  String cmd = String(command);
  String val = value ? String(value) : "";

  if (cmd == "#CONFIG") {
    modoConfig = true;
    Serial.println("✅ MODO CONFIG ACTIVADO");
    bloqueado = false;
    return;
  }

  if (cmd == "#END") {
    modoConfig = false;
    Serial.println("✅ CONFIGURACIÓN GUARDADA. Reiniciando...");
    delay(300);
    #if defined(__AVR__)
      wdt_enable(WDTO_15MS); while (1);
    #else
      NVIC_SystemReset();
    #endif
    return;
  }

  if (cmd == "#CLEAR") {
    for (int i = 0; i < EEPROM.length(); i++) EEPROM.write(i, 0xFF);
    EEPROM.write(0, 0);
    for (int i = 0; i < switchCount; i++) delete switches[i];
    for (int i = 0; i < buttonCount; i++) delete buttons[i];
    for (int i = 0; i < potCount; i++) delete pots[i];
    for (int i = 0; i < outputCount; i++) delete outputs[i];
    switchCount = buttonCount = potCount = outputCount = 0;
    Serial.println("🧹 EEPROM borrada y memoria liberada.");
    bloqueado = false;
    return;
  }

  if (cmd == "#DUMP") {
    uint8_t count = EEPROM.read(0);
    Serial.println("BEGIN CONFIG");
    for (int i = 0; i < count; i++) {
      PinConfig cfg;
      EEPROM.get(1 + i * sizeof(PinConfig), cfg);
  
      Serial.print("ADD ");
      Serial.print(cfg.type == 0 ? "SWITCH " :
                   cfg.type == 1 ? "BUTTON " :
                   cfg.type == 2 ? "OUTPUT " :
                   "POT ");
      Serial.print(cfg.pin); Serial.print(" ");
      Serial.print(cfg.param); Serial.print(" ");
      Serial.print(cfg.minOut, 3); Serial.print(" ");
      Serial.println(cfg.maxOut, 3);
  
      if (cfg.type == 3) {
        Serial.print("CFG "); Serial.print(cfg.pin);
        Serial.print(" SCALE "); Serial.print(cfg.minIn);
        Serial.print(" "); Serial.print(cfg.maxIn);
        Serial.print(" "); Serial.print(cfg.minOut, 3);
        Serial.print(" "); Serial.println(cfg.maxOut, 3);
  
        Serial.print("CFG "); Serial.print(cfg.pin);
        Serial.print(" SMOOTH "); Serial.println(cfg.suavizado, 3);
  
        Serial.print("CFG "); Serial.print(cfg.pin);
        Serial.print(" MODE ");
        switch (cfg.modoEnvio) {
          case 0: Serial.println("CONTINUO"); break;
          case 1: Serial.println("CAMBIO"); break;
          case 2:
            Serial.print("INTERVALO ");
            Serial.println(cfg.intervalo);
            break;
          case 3: Serial.println("MANUAL"); break;
          default: Serial.println("CONTINUO");
        }
      }
    }
    Serial.println("END CONFIG");
    Serial.print("{\"switches\":"); Serial.print(switchCount);
    Serial.print(",\"buttons\":"); Serial.print(buttonCount);
    Serial.print(",\"outputs\":"); Serial.print(outputCount);
    Serial.print(",\"pots\":"); Serial.print(potCount);
    Serial.println("}");
    bloqueado = false;
    return;
  }
  

  if (modoConfig && cmd.startsWith("ADD")) {
    char tipo[20], pinStr[10], param[20], v1[10], v2[10];
    int args = sscanf(cmd.c_str() + 4, "%s %s %s %s %s", tipo, pinStr, param, v1, v2);
    if (args == 5) {
      savePinConfig(tipo, analogPinFromString(pinStr), param, v1, v2);
    } else {
      Serial.print("❌ ERROR: Formato ADD inválido → "); Serial.println(cmd);
    }
    bloqueado = false;
    return;
  }

  if (modoConfig && cmd.startsWith("CFG")) {
    updatePotParam(cmd);
    bloqueado = false;
    return;
  }

  for (int i = 0; i < potCount; i++) {
    if (pots[i]->sendIfMatches(command)) {
      bloqueado = false;
      return;
    }
  }

  bool handled = false;
  for (int i = 0; i < outputCount; i++) {
    if (outputs[i]->outputDigital(command, value, 1)) handled = true;
  }

  if (handled)
    Serial.println("✅ ACK: " + cmd + " = " + val);
  else
    Serial.println("⚠️ Comando no reconocido: " + cmd);

  bloqueado = false;
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  EEPROM.begin();
  serialInterface.begin();
  loadConfigFromEEPROM();

  for (int i = 0; i < switchCount; i++) { switches[i]->begin(); Serial.print("register("); Serial.print(switchParams[i]); Serial.println(")"); }
  for (int i = 0; i < buttonCount; i++) { buttons[i]->begin(); Serial.print("register("); Serial.print(buttonParams[i]); Serial.println(")"); }
  for (int i = 0; i < outputCount; i++) { outputs[i]->begin(); Serial.print("register("); Serial.print(outputParams[i]); Serial.println(")"); }
  for (int i = 0; i < potCount; i++) { Serial.print("register("); Serial.print(potParams[i]); Serial.println(")"); }

  Serial.println("Listo. v1.0");
}

void loop() {
  serialInterface.update();
  if (!modoConfig && !bloqueado) {
    for (int i = 0; i < switchCount; i++) switches[i]->update();
    for (int i = 0; i < buttonCount; i++) buttons[i]->update();
    for (int i = 0; i < potCount; i++) pots[i]->update();
  }
}
