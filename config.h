/*
 * config.h - Configuracion central del esclavo Modbus RTU
 * -------------------------------------------------------
 * Todas las constantes de hardware, protocolo y temporizacion viven aqui.
 * Para cambiar el cableado o el ID Modbus no hace falta tocar el .ino.
 *
 * ESP32-C3 SuperMini: modulo de 13 GPIO disponibles. Se evitan deliberadamente
 * GPIO2/8/9 (strapping: se leen en el reset) y GPIO20/21 (UART0 de arranque).
 * GPIO8 se usa solo como LED de la placa; es strapping, pero el LED ya esta
 * cableado por el fabricante y no impide el arranque.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

/* ------------------------------------------------------------------ */
/* 1. Pines                                                            */
/* ------------------------------------------------------------------ */

/** Bus OneWire local (4 DS18B20 a menos de 2 m del modulo). */
static const uint8_t PIN_ONEWIRE = 3;

/** UART1 dedicated al modulo TTL-RS485 de conmutacion automatica. */
static const uint8_t PIN_RS485_RX = 7;   /* RO del modulo -> GPIO7 (RX) */
static const uint8_t PIN_RS485_TX = 6;   /* DI del modulo <- GPIO6 (TX) */

/** LED azul integrado (activo a nivel bajo en la SuperMini). */
static const uint8_t PIN_LED = 8;

/* ------------------------------------------------------------------ */
/* 2. Parametros Modbus RTU                                            */
/* ------------------------------------------------------------------ */

static const uint8_t  MODBUS_SLAVE_ID = 1;      /* ID exigido por el maestro */
static const uint32_t MODBUS_BAUD     = 9600UL; /* 9600 baudios, 8N1 */
static const uint8_t  MODBUS_SENSOR_HREGS = 8;  /* Registros 0..7 del enunciado */

/*
 * Ampliacion opcional de diagnostico (registros 8..13). No altera el mapa
 * obligatorio 0..7; la A6 puede seguir leyendo solo 8 registros.
 *   8  = errores acumulados de lectura OneWire
 *   9  = uptime en segundos (palabra baja)
 *  10  = uptime en segundos (palabra alta)
 *  11  = reinicios por watchdog
 *  12  = mascara de sensores detectados (bit i = sensor i+1 presente)
 *  13  = version de firmware (101 = 1.01)
 * Poner a 0 para exponer exclusivamente los 8 registros del enunciado.
 */
#define MODBUS_EXTENDED_REGS 1

#if MODBUS_EXTENDED_REGS
static const uint8_t MODBUS_HREG_COUNT = 14;
#else
static const uint8_t MODBUS_HREG_COUNT = MODBUS_SENSOR_HREGS;
#endif

static const uint16_t FIRMWARE_VERSION = 101;

/* ------------------------------------------------------------------ */
/* 3. Sensores y temporizacion                                         */
/* ------------------------------------------------------------------ */

static const uint8_t SENSOR_COUNT = 4;

/** Ciclo de medicion: una conversion cada 5 s, tal como pide el enunciado. */
static const uint32_t SENSOR_PERIOD_MS = 5000UL;

/** DS18B20 a 12 bits tarda como maximo 750 ms; 900 ms de margen. */
static const uint32_t CONVERSION_TIMEOUT_MS = 900UL;

/** No sondear el flag de conversion antes de 100 ms (evita leer dato viejo). */
static const uint32_t CONVERSION_MIN_WAIT_MS = 100UL;

/** Resolucion 12 bits = 0,0625 C; el registro x10 tiene 0,1 C de paso. */
static const uint8_t DS18B20_RESOLUTION = 12;

/** Cada minuto se reintentan los huecos libres (sensor que se reconecta). */
static const uint32_t SENSOR_RESCAN_MS = 60000UL;

/** Debounce: evita que un lectura aislada haga parpadear el registro de estado. */
static const uint8_t FAILS_TO_OFFLINE = 3;  /* 3 lecturas malas -> estado 0 */
static const uint8_t OKS_TO_ONLINE    = 2;  /* 2 lecturas buenas -> estado 1 */

/** Valor senalizado como "sensor invalido" (mismo criterio que KCS de KinCony). */
static const int16_t TEMP_INVALID_RAW = (int16_t)0x7FFF;

/** Rango fisico del DS18B20 (-55..125 C) en unidades de 0,1 C. */
static const int16_t TEMP_MIN_RAW = -550;
static const int16_t TEMP_MAX_RAW = 1250;

/*
 * ROM de 64 bits de cada sensor, en orden fisico.
 * Dejar TODOS a cero para que el firmware descubra automaticamente los
 * dispositivos en el primer arranque. El sketch tools/scan_onewire imprime
 * las ROM exactas; pegarlas aqui fija el sensor a su registro (recomendado
 * en instalaciones industriales: asi cambiar un sensor no reordena los canales).
 * Formato: {family, 6 bytes de direccion, CRC}.
 */
struct SensorRom {
    uint8_t rom[8];
};

static const SensorRom SENSOR_ROMS[SENSOR_COUNT] = {
    {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},  /* Sensor 1 */
    {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},  /* Sensor 2 */
    {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},  /* Sensor 3 */
    {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}   /* Sensor 4 */
};

/* ------------------------------------------------------------------ */
/* 4. Watchdogs y diagnostico                                          */
/* ------------------------------------------------------------------ */

/** Si el loop tarda mas que esto en volver, se reinicia el micro. */
static const uint32_t SOFTWARE_WDT_MS = 2000UL;

/** Parpadeo del LED: 1 s con todos los sensores bien, 200 ms si hay fallo. */
static const uint32_t LED_OK_PERIOD_MS = 1000UL;
static const uint32_t LED_ERROR_PERIOD_MS = 200UL;

/** Imprimir eventos por el puerto USB (no interfiere con Serial1/RS485). */
#define SERIAL_DEBUG 1

#endif /* CONFIG_H */
