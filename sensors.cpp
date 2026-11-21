/*
 * sensors.cpp - Implementacion del bus OneWire no bloqueante
 * ----------------------------------------------------------
 * Una lectura completa consta de tres fases repartidas en el tiempo:
 *   1) startCycle():  lanza la conversion de los 4 sensores (~5 ms).
 *   2) update():      comprueba isConversionComplete() sin dormir.
 *   3) readAll():     recoge los scratchpads (~20 ms) y actualiza registros.
 * Ninguna fase supera el tiempo de una trama Modbus completa, por lo que
 * mb.task() sigue atendiendo al maestro con latencia de unos milisegundos.
 */

#include "sensors.h"

namespace {

/** ROM de 8 bytes completamente a cero = hueco sin configurar. */
bool romIsZero(const uint8_t *rom) {
    for (uint8_t i = 0; i < 8; ++i) {
        if (rom[i] != 0x00) return false;
    }
    return true;
}

/** Dos ROM de 8 bytes identicas. */
bool romEquals(const uint8_t *a, const uint8_t *b) {
    for (uint8_t i = 0; i < 8; ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

} // namespace

DS18B20Manager::DS18B20Manager(OneWire &oneWire, DallasTemperature &dt)
    : readErrors(0), successfulReads(0), _ow(oneWire), _dt(dt),
      detected(0), state(STATE_IDLE), conversionStarted(0),
      lastCycleStarted(0), busResetFailures(0)
{
    for (uint8_t i = 0; i < SENSOR_COUNT; ++i) {
        memset(_rom[i], 0x00, 8);
        present[i] = false;
        consecutiveOk[i] = 0;
        consecutiveFail[i] = 0;
        temperatureRaw[i] = TEMP_INVALID_RAW;
        status[i] = 0;
    }
}

bool DS18B20Manager::validRom(const uint8_t *rom) const {
    /* 0x28 = DS18B20. Ademas se valida el CRC de los 7 primeros bytes. */
    if (rom[0] != 0x28) return false;
    return (_ow.crc8(rom, 7) == rom[7]);
}

bool DS18B20Manager::alreadyConfigured(const uint8_t *rom) const {
    for (uint8_t i = 0; i < SENSOR_COUNT; ++i) {
        if (!romIsZero(_rom[i]) && romEquals(_rom[i], rom)) return true;
    }
    return false;
}

void DS18B20Manager::scanMissing() {
    /*
     * Solo rellena huecos configured a cero. Las ROM escritas a mano en
     * config.h nunca se sobreescriben: eso garantiza que el sensor fisico
     * siga asociado al mismo holding register tras sustituir un dispositivo.
     */
    bool hasEmptySlot = false;
    for (uint8_t i = 0; i < SENSOR_COUNT; ++i) {
        if (romIsZero(_rom[i])) hasEmptySlot = true;
    }

    if (hasEmptySlot) {
        _ow.reset_search();
        uint8_t rom[8];
        while (_ow.search(rom)) {
            if (!validRom(rom)) continue;   /* descarta 0x00 y CRC erroneos */
            if (alreadyConfigured(rom)) continue;
            for (uint8_t i = 0; i < SENSOR_COUNT; ++i) {
                if (romIsZero(_rom[i])) {
                    memcpy(_rom[i], rom, 8);
#if SERIAL_DEBUG
                    Serial.print(F("[ONEWIRE] Sensor "));
                    Serial.print(i + 1);
                    Serial.print(F(" -> {"));
                    for (uint8_t b = 0; b < 8; ++b) {
                        Serial.print(F("0x"));
                        if (rom[b] < 0x10) Serial.print('0');
                        Serial.print(rom[b], HEX);
                        if (b < 7) Serial.print(F(", "));
                    }
                    Serial.println(F("}"));
#endif
                    break;
                }
            }
        }
    }

    /* Revalida TODOS los slots: un sensor configured ausente puede volver. */
    detected = 0;
    for (uint8_t i = 0; i < SENSOR_COUNT; ++i) {
        present[i] = (!romIsZero(_rom[i]) && validRom(_rom[i]) && _dt.isConnected(_rom[i]));
        if (present[i]) ++detected;
    }
}

void DS18B20Manager::recoverBus() {
    /*
     * Un DS18B20 alimentado de forma parasitaria o con interferencia puede
     * dejar el bus a nivel bajo mas de 1 s. Forzamos un pulso alto en el pin
     * y reinicializamos OneWire para desbloquearlo sin reiniciar el ESP32.
     */
    pinMode(PIN_ONEWIRE, OUTPUT);
    digitalWrite(PIN_ONEWIRE, HIGH);
    delay(2);
    pinMode(PIN_ONEWIRE, INPUT);
    _ow.begin(PIN_ONEWIRE);
    busResetFailures = 0;
}

bool DS18B20Manager::begin() {
    _dt.begin();
    _dt.setWaitForConversion(false);       /* clave: conversion asincrona */
    _dt.setResolution(DS18B20_RESOLUTION);

    for (uint8_t i = 0; i < SENSOR_COUNT; ++i) {
        memcpy(_rom[i], SENSOR_ROMS[i].rom, 8);
    }

    scanMissing();

#if SERIAL_DEBUG
    Serial.print(F("[ONEWIRE] Sensores detectados: "));
    Serial.println(detected);
    if (detected < SENSOR_COUNT) {
        Serial.println(F("[ONEWIRE] Faltan sensores; se reintentara cada 60 s."));
    }
#endif

    /* Arrancamos la primera medicion de inmediato (sin esperar 5 s). */
    lastCycleStarted = millis() - SENSOR_PERIOD_MS;
    return detected > 0;
}

void DS18B20Manager::startCycle() {
    if (_ow.reset() == 0) {
        ++busResetFailures;
        if (busResetFailures >= 3) recoverBus();
    } else {
        busResetFailures = 0;
    }

    for (uint8_t i = 0; i < SENSOR_COUNT; ++i) {
        if (present[i]) {
            /* No bloquea: solo escribe el comando 0x44 en cada dispositivo. */
            _dt.requestTemperaturesByAddress(_rom[i]);
        }
    }
    conversionStarted = millis();
    lastCycleStarted = millis();
    state = STATE_WAIT_CONVERSION;
}

void DS18B20Manager::update() {
    uint32_t now = millis();

    if (state == STATE_IDLE) {
        static uint32_t lastRescan = 0;
        if (detected < SENSOR_COUNT && (now - lastRescan) >= SENSOR_RESCAN_MS) {
            lastRescan = now;
            scanMissing();
        }
        if ((now - lastCycleStarted) >= SENSOR_PERIOD_MS) startCycle();
        return;
    }

    /* Espera no bloqueante a que terminen TODAS las conversiones. */
    if ((now - conversionStarted) < CONVERSION_MIN_WAIT_MS) return;

    bool finished = _dt.isConversionComplete();
    if (!finished && (now - conversionStarted) < CONVERSION_TIMEOUT_MS) return;

    /* Leemos los 4 scratchpads y aplicamos debounce a cada sensor. */
    for (uint8_t i = 0; i < SENSOR_COUNT; ++i) {
        if (!present[i]) continue;

        float temp = _dt.getTempC(_rom[i]);
        bool valid = !isnan(temp) &&
                     (temp != DEVICE_DISCONNECTED_C) &&
                     (temp >= -55.0f) && (temp <= 125.0f);

        if (valid) {
            int16_t raw = (int16_t)lroundf(temp * 10.0f);
            if (raw < TEMP_MIN_RAW) raw = TEMP_MIN_RAW;
            if (raw > TEMP_MAX_RAW) raw = TEMP_MAX_RAW;

            successfulReads = successfulReads + 1U;
            if (consecutiveOk[i] < 255) ++consecutiveOk[i];
            consecutiveFail[i] = 0;
            temperatureRaw[i] = raw;
            if (consecutiveOk[i] >= OKS_TO_ONLINE) status[i] = 1;
        } else {
            readErrors = readErrors + 1U;
            consecutiveOk[i] = 0;
            if (consecutiveFail[i] < 255) ++consecutiveFail[i];
            if (consecutiveFail[i] >= FAILS_TO_OFFLINE) {
                /* Solo tras 3 fallos se marca caida y se publica 0x7FFF. */
                status[i] = 0;
                temperatureRaw[i] = TEMP_INVALID_RAW;
#if SERIAL_DEBUG
                Serial.print(F("[ONEWIRE] Sensor "));
                Serial.print(i + 1);
                Serial.println(F(" desconectado / lectura invalida"));
#endif
            }
        }
    }

    state = STATE_IDLE;
}

uint16_t DS18B20Manager::presentMask() const {
    uint16_t mask = 0;
    for (uint8_t i = 0; i < SENSOR_COUNT; ++i) {
        if (present[i]) mask |= (uint16_t)(1U << i);
    }
    return mask;
}
