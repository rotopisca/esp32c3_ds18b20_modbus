/*
 * scan_onewire.ino - Descubrimiento de direcciones ROM de los DS18B20
 * =====================================================================
 * Uso:
 *   1) Conectar los 4 DS18B20 al GPIO3 con pull-up de 4,7 kΩ a 3V3.
 *   2) Grabar este sketch en la ESP32-C3 SuperMini.
 *   3) Abrir el monitor serie a 115200 baudios.
 *   4) Anotar las 4 ROM y pegarlas en config.h (SENSOR_ROMS), en el orden
 *      fisico deseado: Sensor 1 -> registro 0, Sensor 2 -> registro 1, ...
 *
 * Para identificar cual ROM corresponde a cual sensor fisico, tocar uno
 * de ellos con los dedos: su temperatura subira en la linea correspondiente.
 *
 * Se recomienda fijar las ROM en config.h en lugar de dejar el descubrimiento
 * automatico: asi, si un sensor se sustituye por otro con ROM distinta, el
 * canal no se reordena silenciosamente.
 * =====================================================================
 */

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

static const uint8_t PIN_ONEWIRE = 3;

OneWire oneWire(PIN_ONEWIRE);
DallasTemperature sensors(&oneWire);

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) { /* espera al monitor serie */ }

    Serial.println(F("\n=== Escaneo OneWire / DS18B20 ==="));
    sensors.begin();
    sensors.setWaitForConversion(false);
}

void loop() {
    uint8_t rom[8];
    uint8_t found = 0;

    Serial.println(F("\n--- Dispositivos en el bus ---"));

    oneWire.reset_search();
    while (oneWire.search(rom)) {
        ++found;

        Serial.print(F("  ROM "));
        Serial.print(found);
        Serial.print(F(" = {"));
        for (uint8_t i = 0; i < 8; ++i) {
            Serial.print(F("0x"));
            if (rom[i] < 0x10) Serial.print('0');
            Serial.print(rom[i], HEX);
            if (i < 7) Serial.print(F(", "));
        }
        Serial.println(F("}"));

        Serial.print(F("      Familia: 0x"));
        Serial.print(rom[0], HEX);
        Serial.print(F(" | CRC "));
        Serial.println(oneWire.crc8(rom, 7) == rom[7] ? F("OK") : F("FALLO"));

        if (rom[0] == 0x28) {
            sensors.requestTemperaturesByAddress(rom);
            delay(800);   /* aqui si podemos esperar: es un sketch de prueba */
            float t = sensors.getTempC(rom);
            Serial.print(F("      Temperatura: "));
            if (isnan(t) || t == DEVICE_DISCONNECTED_C) {
                Serial.println(F("sin lectura"));
            } else {
                Serial.print(t, 2);
                Serial.println(F(" C"));
            }
        }
    }

    if (found == 0) {
        Serial.println(F("  No se encontro ningun dispositivo."));
        Serial.println(F("  Revisar: pull-up 4,7 kΩ, VDD a 3V3, GND comun y GPIO3."));
    }

    delay(5000);
}
