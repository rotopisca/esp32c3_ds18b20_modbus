/*
 * sensors.h - Gestion de los 4 DS18B20 por direccion ROM unica
 * -----------------------------------------------------------
 * Diseno no bloqueante: el modulo nunca espera 750 ms dentro de una llamada.
 * requestTemperaturesByAddress() lanza la conversion, isConversionComplete()
 * sondea el bus y getTempC() recoge el resultado, todo repartido entre
 * multiples pasadas por loop(). Asi mb.task() se llama cada pocos milisegundos
 * y el esclavo Modbus no pierde tramas por estar ocupado leyendo sensores.
 */

#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "config.h"

class DS18B20Manager {
public:
    DS18B20Manager(OneWire &oneWire, DallasTemperature &dt);

    /* Descubre/valida las ROM y deja el bus listo. Llamar una vez en setup(). */
    bool begin();

    /* Maquina de estados: llamar en cada iteracion de loop(). */
    void update();

    /* Fuerza el arranque de un ciclo (util tras recuperar el bus). */
    void startCycle();

    /* Resultados publicados en los holding registers 0..7. */
    volatile int16_t temperatureRaw[SENSOR_COUNT];  /* x10, 0x7FFF si invalido */
    volatile uint8_t  status[SENSOR_COUNT];         /* 0 = error, 1 = operativo */

    /* Contadores de diagnostico (registro 8 y log USB). */
    volatile uint32_t readErrors;
    volatile uint32_t successfulReads;

    /* Devuelve un bit por sensor presente (registro 12). */
    uint16_t presentMask() const;

    bool isPresent(uint8_t index) const { return present[index]; }

    /* Numero de sensores validos tras la validacion de ROM. */
    uint8_t detectedCount() const { return detected; }

private:
    enum State : uint8_t { STATE_IDLE, STATE_WAIT_CONVERSION };

    OneWire           &_ow;
    DallasTemperature &_dt;
    uint8_t  _rom[SENSOR_COUNT][8];
    bool     present[SENSOR_COUNT];
    uint8_t  consecutiveOk[SENSOR_COUNT];
    uint8_t  consecutiveFail[SENSOR_COUNT];
    uint8_t  detected;
    State    state;
    uint32_t conversionStarted;
    uint32_t lastCycleStarted;
    uint8_t  busResetFailures;

    /* Reintenta el descubrimiento de huecos libres cada cierto tiempo. */
    void scanMissing();

    /* Comprueba CRC y familia 0x28 del DS18B20 antes de aceptar una ROM. */
    bool validRom(const uint8_t *rom) const;

    /* Comprueba si alguna ROM configurada coincide con la recien escaneada. */
    bool alreadyConfigured(const uint8_t *rom) const;

    /* Bus colgados: emite un pulso de reset por GPIO antes de seguir. */
    void recoverBus();
};

#endif /* SENSORS_H */
