# Configuración del maestro KinCony KC868-A6

> Se asume que "A6" = **KC868-A6**. Su puerto RS485 usa **GPIO27 (TX)** y
> **GPIO14 (RX)** y el hardware ya gestiona la dirección (no hay pin DE/RE).

## Mapa que debe leer el maestro

| Registro | Contenido | Tipo | Escala |
|---|---|---|---|
| 0 | Temperatura sensor 1 | `int16` | ×0,1 °C |
| 1 | Temperatura sensor 2 | `int16` | ×0,1 °C |
| 2 | Temperatura sensor 3 | `int16` | ×0,1 °C |
| 3 | Temperatura sensor 4 | `int16` | ×0,1 °C |
| 4 | Estado sensor 1 | `uint16` | 0 = error, 1 = OK |
| 5 | Estado sensor 2 | `uint16` | 0 = error, 1 = OK |
| 6 | Estado sensor 3 | `uint16` | 0 = error, 1 = OK |
| 7 | Estado sensor 4 | `uint16` | 0 = error, 1 = OK |

Un sensor en fallo devuelve `0x7FFF` en su registro de temperatura y `0` en
el de estado. Trama de sondeo típica (FC 0x03, 8 registros):

```
01 03 00 00 00 08 CRC_LO CRC_HI
```

Parámetros: **Slave ID 1, 9600 baudios, 8 datos, sin paridad, 1 stop**.

## Opción A — Firmware KCS de fábrica (la elegida)

1. Acceder a la interfaz web de la A6 e ir a los ajustes RS485.
2. Activar RS485 y seleccionar protocolo **`Modbus_RTU-v2`** (es el modo
   Modbus RTU estándar; el modo heredado de KCS no lo es).
3. Configurar **9600 8N1** y una dirección local para la A6 distinta de 1
   (por ejemplo 2), para que no colisione con el esclavo.
4. En la sección de *modbus pull* / sondeo, crear una entrada:
   - Dirección de esclavo remoto: **1**
   - Función: **0x03 Read Holding Registers**
   - Dirección de inicio: **0** (KCS usa direcciones 0-based)
   - Cantidad: **8**
   - Periodo: **5 s**
5. Verificar en la UI/MQTT de KCS que aparecen los 8 valores.

### Advertencia importante sobre KCS

El soporte de KinCony indica que **el firmware KCS no lee sensores RS485
ajenos de forma nativa** y que *modbus pull* parece estar pensado para
sondear el propio espacio de registros de la placa. Por eso el paso 5 es
obligatorio: no dar por buena la configuración sin comprobar datos reales.

Procedimiento de verificación:

1. Conectar un adaptador USB-RS485 en paralelo en A/B y abrir Modbus Poll
   (o `modpoll`) con 9600 8N1, slave 1. Confirmar que la respuesta es correcta.
   Si el esclavo responde al analizador pero la A6 no, el problema está en la
   configuración de KCS o en A/B invertidos.
2. Si KCS no expone los registros remotos en su UI/MQTT, usar el plan B.

## Opción B (plan B) — ESPHome en la KC868-A6

Configuración mínima verificada por usuarios de KC868-A6 con esclavos
Modbus de terceros:

```yaml
uart:
  - id: rs485
    tx_pin: GPIO27
    rx_pin: GPIO14
    baud_rate: 9600
    data_bits: 8
    parity: NONE
    stop_bits: 1

modbus:
  id: modbus1
  uart_id: rs485
  send_wait_time: 200ms

modbus_controller:
  - id: esclavo_ds18b20
    address: 1
    modbus_id: modbus1
    update_interval: 5s

sensor:
  - platform: modbus_controller
    modbus_controller_id: esclavo_ds18b20
    name: "Temperatura DS18B20 1"
    register_type: holding
    address: 0
    value_type: S_INT
    scale: 0.1
    unit_of_measurement: "°C"
    device_class: temperature

  - platform: modbus_controller
    modbus_controller_id: esclavo_ds18b20
    name: "Temperatura DS18B20 2"
    register_type: holding
    address: 1
    value_type: S_INT
    scale: 0.1
    unit_of_measurement: "°C"
    device_class: temperature

  - platform: modbus_controller
    modbus_controller_id: esclavo_ds18b20
    name: "Temperatura DS18B20 3"
    register_type: holding
    address: 2
    value_type: S_INT
    scale: 0.1
    unit_of_measurement: "°C"
    device_class: temperature

  - platform: modbus_controller
    modbus_controller_id: esclavo_ds18b20
    name: "Temperatura DS18B20 4"
    register_type: holding
    address: 3
    value_type: S_INT
    scale: 0.1
    unit_of_measurement: "°C"
    device_class: temperature

binary_sensor:
  - platform: modbus_controller
    modbus_controller_id: esclavo_ds18b20
    name: "Sensor 1 operativo"
    register_type: holding
    address: 4
    bitmask: 1
  - platform: modbus_controller
    modbus_controller_id: esclavo_ds18b20
    name: "Sensor 2 operativo"
    register_type: holding
    address: 5
    bitmask: 1
  - platform: modbus_controller
    modbus_controller_id: esclavo_ds18b20
    name: "Sensor 3 operativo"
    register_type: holding
    address: 6
    bitmask: 1
  - platform: modbus_controller
    modbus_controller_id: esclavo_ds18b20
    name: "Sensor 4 operativo"
    register_type: holding
    address: 7
    bitmask: 1
```

Notas:

- `value_type: S_INT` interpreta la temperatura como entero con signo; si se
  usa `U_INT`, las temperaturas negativas aparecerán como valores enormes.
- `address` es 0-based, igual que en el firmware del esclavo.
- Si se usa `0x7FFF` como centinela, filtrar ese valor en una plantilla de
  ESPHome para no publicar 3276,7 °C.
- Comprobar la sintaxis exacta de `binary_sensor` con la versión de ESPHome
  instalada, ya que algunos `value_type` cambian entre versiones.
- Grabar ESPHome en la A6 borra KCS; se puede volver a KCS desde la web de
  KinCony si se necesita.

## Registros de diagnóstico opcionales (8..13)

Si se deja `MODBUS_EXTENDED_REGS 1` en `config.h`, el maestro puede leer
además:

| Registro | Significado |
|---|---|
| 8 | Errores acumulados de lectura OneWire |
| 9 | Uptime en segundos (palabra baja) |
| 10 | Uptime en segundos (palabra alta) |
| 11 | Reinicios por watchdog |
| 12 | Máscara de sensores detectados (bit i = sensor i+1) |
| 13 | Versión de firmware (101) |

La A6 puede seguir leyendo solo 0..7: los registros extra no afectan al mapa
obligatorio.
