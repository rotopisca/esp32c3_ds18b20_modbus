# Esclavo Modbus RTU con ESP32-C3 SuperMini y 4× DS18B20

Proyecto completo para instalación industrial: la ESP32-C3 lee cuatro
DS18B20 por OneWire y expone las temperaturas y su estado como *holding
registers* Modbus RTU sobre RS485 a 25 m, con la KC868-A6 de KinCony como
maestro.

## Características

- ESP32-C3 SuperMini, Arduino IDE / arduino-cli, core `esp32:esp32` 3.3.12.
- RS485 de conmutación automática (sin DE/RE), 9600 baudios, 8N1, Slave ID 1.
- 4 DS18B20 direccionados por ROM de 64 bits (CRC y familia verificados).
- Lectura **asincrona** cada 5 s: el esclavo Modbus nunca se bloquea 750 ms.
- Detección de sensores desconectados con *debounce* (3 fallos → estado 0).
- Watchdog de hardware (TWDT) + watchdog software con reinicio controlado.
- Registros extra opcionales de diagnóstico (errores, uptime, resets, máscara).
- Cableado de 6 hilos: 2× +5 V, 2× GND, 1× A, 1× B.

## Mapa Modbus

| Registro | Significado |
|---|---|
| 0..3 | Temperatura de los sensores 1..4, `int16` ×10 (°C) |
| 4..7 | Estado de los sensores 1..4: 0 = error/desconectado, 1 = operativo |
| 8..13 | Diagnóstico opcional (ver `config.h`) |

Sensor en fallo: registro de temperatura = `0x7FFF`, registro de estado = `0`.

## Estructura

```
esp32c3_ds18b20_modbus/
├─ esp32c3_ds18b20_modbus.ino   # firmware principal
├─ config.h                     # pines, ID, ROM, temporizaciones
├─ sensors.h / sensors.cpp      # OneWire asíncrono y debounce
├─ src/modbus/                  # modbus-esp8266 4.1.0 versionado + licencia
├─ tools/scan_onewire/          # sketch para descubrir las ROM
└─ docs/
   ├─ wiring.md                 # esquema, cálculo de caída, terminación
   └─ kincony_a6.md             # configuración del maestro (KCS y plan B)
```

## Compilación y grabación

Requisitos en el Gestor de Librerías: `OneWire >= 2.3.8`,
`DallasTemperature >= 3.9.0` (probado con 4.0.5) y core ESP32 3.x.
**OneWire 2.3.7 o anterior no compila con core ESP32 3.x.**

Con Arduino IDE:

1. Herramientas → Placa → *esp32* → **ESP32C3 Dev Module** (o el perfil
   *Nologo ESP32C3 Super Mini* si aparece).
2. Abrir `esp32c3_ds18b20_modbus.ino` (la carpeta debe llamarse igual).
3. Verificar y grabar por USB-C.

Con arduino-cli:

```powershell
arduino-cli lib install OneWire@2.3.8 DallasTemperature@4.0.5
arduino-cli compile --fqbn esp32:esp32:nologo_esp32c3_super_mini .
arduino-cli upload -p COM3 --fqbn esp32:esp32:nologo_esp32c3_super_mini .
```

Si el perfil SuperMini no está instalado, usar `esp32:esp32:esp32c3`.
El puerto `COM3` es un ejemplo; comprobarlo con `arduino-cli board list`.

### Nota sobre la librería Modbus

El proyecto incluye una **copia versionada** de `modbus-esp8266` 4.1.0 en
`src/modbus/`. Motivo: otra librería distinta que también instala
`ModbusRTU.h` gana la resolución de includes en este equipo y provoca errores
del tipo `'ModbusRTU::ModbusRTU()' is protected`. La copia local evita
conflictos y hace el proyecto reproducible.

Para usarla desde el Gestor de Librerías en su lugar:

1. Desinstalar **todas** las librerías que aporten `ModbusRTU.h`.
2. Instalar `modbus-esp8266` 4.1.0.
3. Borrar `src/modbus/` y cambiar el include del `.ino` por
   `#include <ModbusRTU.h>`.

## Procedimiento de ROM de los sensores

1. Grabar `tools/scan_onewire/scan_onewire.ino`.
2. Abrir monitor serie a 115200 y anotar las cuatro ROM (`{0x28, ...}`).
3. Identificar cada sensor físico tocándolo con los dedos.
4. Pegar las ROM en `SENSOR_ROMS` de `config.h`, en el orden deseado.

Si se dejan todas a cero, el firmware descubre automáticamente los
dispositivos en el primer arranque y reintenta cada 60 s. Fijar las ROM es
recomendable: evita que sustituir un sensor reordene los canales.

## Puesta en marcha y pruebas

1. Medir tensión en el pin 5 V del esclavo con todo conectado: debe quedar
   por encima de 4,3 V (ver `docs/wiring.md`).
2. Conectar un adaptador USB-RS485 y un analizador Modbus en paralelo.
   Enviar `01 03 00 00 00 08`: deben volver 8 registros.
3. Desconectar un DS18B20: su registro de estado pasa a 0 en ≤15 s y su
   temperatura a `0x7FFF`. Al reconectarlo vuelve a 1 tras 2 lecturas buenas.
4. Comprobar el LED: parpadeo lento = todo OK; rápido = hay un sensor en fallo.
5. Verificar por USB que no aparecen mensajes `[WDT]` periódicos.
6. Prueba de resistencia: 24 h continuos con el maestro sondeando cada 1 s.

## Mejoras para fiabilidad industrial a largo plazo

Ya implementadas: watchdogs, CRC de ROM, validación de rango térmico,
*debounce*, recuperación de bus OneWire colgado, contadores de error y
reinicio, sin radio WiFi/BT, sección crítica en la publicación de registros.

Recomendaciones adicionales para una instalación definitiva:

- **Protección eléctrica:** diodos TVS bidireccionales entre A-B y A/B-tierra,
  fusible PTC rearmable en la alimentación 5 V, varistor en la fuente.
- **Aislamiento galvánico** del lado RS485 si maestro y esclavo están en
  edificios o cuadros distintos (repetidor aislante RS485; rompe bucles de tierra).
- **Cable:** par trenzado dedicado a A/B, pantalla conectada a tierra solo en
  el maestro, bornes de tornillo en vez de soldaduras y alivio de tensión.
- **Alimentación:** si la caída supera 0,7 V, enviar 12/24 V por el cable y
  regenerar 5 V localmente con un regulador con bajo *dropout*.
- **Terminación:** 120 Ω solo en los extremos y polarización en un único punto;
  revisar si el módulo ya los incorpora.
- **Robustez de software:** cabeceras de control de humedad/conformal coating,
  guardar las ROM y calibración en NVS (ya se usa NVS para el contador WDT),
  y publicar la tensión de alimentación midiendo 3V3 con ADC1.
- **Mantenimiento:** registrar uptime y errores del registro 8..11 en el
  maestro para detectar degradación progresiva antes de que falle el sensor.
- **Repuesto:** mantener un módulo esclavo pregrabado con las ROM ya fijadas.

## Licencias

- Código propio del proyecto: uso libre para este proyecto.
- `src/modbus/`: modbus-esp8266, Alexander Emelianov, licencia BSD
  (ver `src/modbus/LICENSE-modbus-esp8266.txt`).
- OneWire © Paul Stoffregen; DallasTemperature © Miles Burton.
