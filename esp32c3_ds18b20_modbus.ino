/*
 * esp32c3_ds18b20_modbus.ino
 * =====================================================================
 * Esclavo Modbus RTU sobre RS485 para ESP32-C3 SuperMini.
 *
 *   - 4 x DS18B20 identificados por ROM unica de 64 bits (OneWire).
 *   - RS485 TTL de conmutacion automatica: NO usa pines DE/RE.
 *   - 9600 baudios, 8N1, Slave ID 1.
 *   - Holding registers 0..3 = temperatura x10 (int16 con signo).
 *   - Holding registers 4..7 = estado (0 = error/desconectado, 1 = OK).
 *   - Registros 8..13 opcionales de diagnostico (ver config.h).
 *   - Lectura asincrona cada 5 s; Modbus se atiende en cada pasada del loop.
 *   - Watchdog de hardware (TWDT del core) + watchdog software.
 *
 * Modelo de scheduling (ESP32-C3 es MONONUCLEO, no hay reparto de cores):
 *   loop() -> mb.task() -> tempBus.update() -> publishRegisters() -> WDT feed
 * Nunca se usa delay() en el camino caliente; la unica espera larga es la
 * conversion de los DS18B20, que se sondea sin bloquear desde update().
 *
 * La libreria modbus-esp8266 (Alexander Emelianov) se incluye VERSIONADA en
 * src/modbus/ porque en este equipo existe otra libreria distinta que tambien
 * instala "ModbusRTU.h" y gana la resolucion de includes del Arduino IDE.
 * Si se prefiere usar el Gestor de Librerias, desinstalar TODAS las demas
 * librerias ModbusRTU y cambiar el include por #include <ModbusRTU.h>.
 * =====================================================================
 */

#include <Arduino.h>
#include <esp32-hal-bt.h>
#include <esp_system.h>
#include <Preferences.h>

#include "config.h"
#include "sensors.h"
#include "src/modbus/ModbusRTU.h"   /* copia local de modbus-esp8266 4.1.0 */

/* ---------------------------------------------------------------------- */
/* Objetos globales                                                        */
/* ---------------------------------------------------------------------- */

/** OneWire en GPIO3. Los 4 sensores estan a menos de 2 m de la placa. */
OneWire oneWire(PIN_ONEWIRE);
DallasTemperature dallas(&oneWire);
DS18B20Manager tempBus(oneWire, dallas);

/** Pila Modbus esclavo; sin pin DE/RE gracias al modulo automatico. */
ModbusRTU mb;

/** Persistencia de contadores de reinicio (NVS del ESP32). */
Preferences prefs;

/** Evita que el maestro lea una pareja temperatura/estado a medias. */
portMUX_TYPE registerMux = portMUX_INITIALIZER_UNLOCKED;

/* ---------------------------------------------------------------------- */
/* Estado global                                                           */
/* ---------------------------------------------------------------------- */

static uint32_t wdtResets      = 0;   /* reinicios por watchdog (registro 11) */
static uint32_t uptimeSeconds  = 0;   /* uptime (registros 9 y 10) */
static uint32_t lastLoopMark   = 0;   /* marca para el watchdog software */
static uint32_t lastSecondMark = 0;   /* base del contador de uptime */
static uint32_t lastLedMark    = 0;   /* base del parpadeo de corazon */
static bool     ledLevel       = false;

/* ---------------------------------------------------------------------- */
/* Publicacion de registros                                                */
/* ---------------------------------------------------------------------- */

/*
 * Copia la instantanea de sensores a la tabla de holding registers.
 * Se hace dentro de una seccion critica cortisima para que el controlador
 * Modbus (que puede interrumpir en cualquier momento) nunca vea la
 * temperatura nueva con el estado antiguo, o viceversa.
 */
static void publishRegisters() {
    /* Copia rapida fuera de la seccion critica (lecturas de 16 bit atomicas). */
    int16_t temps[SENSOR_COUNT];
    uint8_t states[SENSOR_COUNT];
    for (uint8_t i = 0; i < SENSOR_COUNT; ++i) {
        temps[i]  = tempBus.temperatureRaw[i];
        states[i] = tempBus.status[i];
    }

    portENTER_CRITICAL(&registerMux);

    for (uint8_t i = 0; i < SENSOR_COUNT; ++i) {
        mb.Hreg(i, (uint16_t)temps[i]);           /* 0..3: temperatura x10  */
        mb.Hreg(MODBUS_SENSOR_HREGS + i, states[i]); /* 4..7: estado 0/1 */
    }

#if MODBUS_EXTENDED_REGS
    mb.Hreg(8,  (uint16_t)tempBus.readErrors);              /* errores OneWire */
    mb.Hreg(9,  (uint16_t)(uptimeSeconds & 0xFFFFUL));      /* uptime bajo */
    mb.Hreg(10, (uint16_t)(uptimeSeconds >> 16));           /* uptime alto */
    mb.Hreg(11, (uint16_t)wdtResets);                       /* resets por WDT */
    mb.Hreg(12, tempBus.presentMask());                     /* sensores presentes */
    mb.Hreg(13, FIRMWARE_VERSION);                          /* version FW */
#endif

    portEXIT_CRITICAL(&registerMux);
}

/* ---------------------------------------------------------------------- */
/* Latido visual                                                           */
/* ---------------------------------------------------------------------- */

/*
 * LED azul integrado (GPIO8, activo a nivel bajo):
 *   - parpadeo lento (1 s) = los 4 sensores estan operativos.
 *   - parpadeo rapido (200 ms) = hay al menos un sensor en fallo.
 * Permite diagnosticar la instalacion sin ordenador ni maestro Modbus.
 */
static void updateHeartbeat(uint32_t now) {
    bool allOk = true;
    for (uint8_t i = 0; i < SENSOR_COUNT; ++i) {
        if (tempBus.status[i] != 1) { allOk = false; break; }
    }

    uint32_t period = allOk ? LED_OK_PERIOD_MS : LED_ERROR_PERIOD_MS;
    if ((now - lastLedMark) >= period) {
        lastLedMark = now;
        ledLevel = !ledLevel;
        digitalWrite(PIN_LED, ledLevel ? LOW : HIGH);
    }
}

/* ---------------------------------------------------------------------- */
/* Setup                                                                   */
/* ---------------------------------------------------------------------- */

void setup() {
    Serial.begin(115200);

    /*
     * El modulo se alimenta por los dos pares de 0,22 mm2 a 5 V. No se
     * inicializa WiFi en ningun momento y se apaga Bluetooth; bajar a
     * 160 MHz reduce los picos de corriente y la caida en los 25 m de cable.
     */
    btStop();
    setCpuFrequencyMhz(160);

    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, HIGH);   /* LED apagado (activo a nivel bajo) */

    /*
     * Contador de reinicios por watchdog. La marca "swdt" distingue un
     * reinicio ordenado por nuestro watchdog software de un arranque normal
     * (el motivo ESP_RST_TASK_WDT cubre el watchdog de hardware del core).
     */
    prefs.begin("ds18b20", false);
    esp_reset_reason_t reason = esp_reset_reason();
    bool softwareRestart = prefs.getBool("swdt", false);
    if (reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT || softwareRestart) {
        wdtResets = prefs.getUInt("wdt", 0) + 1;
        prefs.putUInt("wdt", wdtResets);
#if SERIAL_DEBUG
        Serial.print(F("[WDT] Reinicio por watchdog. Total: "));
        Serial.println(wdtResets);
#endif
    } else {
        wdtResets = prefs.getUInt("wdt", 0);
    }
    prefs.putBool("swdt", false);
    prefs.end();

    /* Descubrimiento/validacion de ROM de los sensores. */
    tempBus.begin();

    /*
     * UART1 exclusiva para RS485. Importante configurar primero el puerto
     * (9600 8N1) y despues pasarle el puntero a la libreria Modbus: el
     * intervalo inter-trama se calcula a partir de baudRate().
     */
    Serial1.begin(MODBUS_BAUD, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);
    mb.begin(&Serial1);
    mb.slave(MODBUS_SLAVE_ID);

    for (uint8_t reg = 0; reg < MODBUS_HREG_COUNT; ++reg) {
        mb.addHreg(reg, 0);
    }

    publishRegisters();

    /*
     * Watchdog de hardware del Arduino core. Con CONFIG_AUTOSTART_ARDUINO,
     * enableLoopWDT() suscribe la tarea loop(); hay que alimentarlo con
     * feedLoopWDT() al final de cada iteracion.
     */
    enableLoopWDT();

    lastLoopMark   = millis();
    lastSecondMark = millis();
    lastLedMark    = millis();

#if SERIAL_DEBUG
    Serial.println(F("[INIT] Esclavo listo. ID=1, 9600 8N1, regs 0..7 (+diag 8..13)."));
#endif
}

/* ---------------------------------------------------------------------- */
/* Loop                                                                    */
/* ---------------------------------------------------------------------- */

void loop() {
    uint32_t now = millis();

    /*
     * Watchdog software: si una llamada se queda colgada mas de 2 s
     * (por ejemplo un OneWire bloqueado), forcamos reinicio controlado.
     * Se comprueba ANTES de alimentar el watchdog de hardware.
     */
    if ((now - lastLoopMark) > SOFTWARE_WDT_MS) {
#if SERIAL_DEBUG
        Serial.println(F("[WDT] Loop bloqueado: reiniciando..."));
        Serial.flush();
#endif
        prefs.begin("ds18b20", false);
        prefs.putBool("swdt", true);
        prefs.end();
        esp_restart();
    }

    /*
     * ORDEN CRITICO: atender Modbus primero. La libreria solo acumula bytes
     * en el buffer de HardwareSerial, asi que incluso si la maquina de
     * estados de sensores tarda unos ms, no se pierde ninguna trama.
     */
    mb.task();
    tempBus.update();
    publishRegisters();

    /* Uptime de 32 bits, derivado de millis() con aritmetica modular. */
    if ((now - lastSecondMark) >= 1000UL) {
        lastSecondMark += 1000UL;
        ++uptimeSeconds;

#if SERIAL_DEBUG
        if ((uptimeSeconds % 5UL) == 0UL) {
            Serial.print(F("[TEMP] "));
            for (uint8_t i = 0; i < SENSOR_COUNT; ++i) {
                int16_t raw = tempBus.temperatureRaw[i];
                Serial.print(F("S"));
                Serial.print(i + 1);
                Serial.print(F("="));
                if (raw == TEMP_INVALID_RAW) {
                    Serial.print(F("ERR"));
                } else {
                    Serial.print(raw / 10);
                    Serial.print(F("."));
                    Serial.print((uint16_t)(raw < 0 ? -raw : raw) % 10);
                }
                Serial.print(F("/"));
                Serial.print(tempBus.status[i]);
                Serial.print(F("  "));
            }
            Serial.print(F("err="));
            Serial.print((uint32_t)tempBus.readErrors);
            Serial.print(F(" up="));
            Serial.println(uptimeSeconds);
        }
#endif
    }

    updateHeartbeat(now);

    lastLoopMark = millis();
    feedLoopWDT();
}
