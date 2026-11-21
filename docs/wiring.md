# Esquema de cableado

## 1. Vista general

```
 KINCONY KC868-A6 (MAESTRO)             CABLE 6 x 0,22 mm² / 25 m              ESP32-C3 SUPERMINI (ESCLAVO)
 ┌────────────────────────┐                                                    ┌────────────────────────────┐
 │ RS485 A  ──────────────┼────────────── hilo 5 ─────────────────────────────┼── A  MÓDULO RS485 AUTO      │
 │ RS485 B  ──────────────┼────────────── hilo 6 ─────────────────────────────┼── B  (MAX3485 / SP3485)     │
 │ +5 V                   ├────────────── hilos 1+2 (en paralelo) ────────────┼── 5V placa                 │
 │ GND                    ├────────────── hilos 3+4 (en paralelo) ────────────┼── GND placa                │
 └────────────────────────┘                                                    │        │TXD        │RXD     │
                                                                               │        │           │        │
                                                                               │      GPIO6       GPIO7     │
                                                                               │      (TX)        (RX)      │
                                                                               └────────────────────────────┘

 BUS ONEWIRE LOCAL (< 2 m, NO va por el cable de 25 m)

   ESP32-C3 GPIO3 ───────┬──── DQ  DS18B20 #1..#4
                         │
                      4,7 kΩ
                         │
                       3V3

   Cada DS18B20: VDD -> 3V3, GND -> GND, DQ -> GPIO3
```

> Si el módulo RS485 no responde, **invertir A y B**. Muchos fabricantes
> etiquetan de forma opuesta.

## 2. Asignación de pines ESP32-C3 SuperMini

| Pin placa | Uso | Notas |
|---|---|---|
| GPIO3 | OneWire DQ | Pull-up **4,7 kΩ a 3V3** |
| GPIO6 | UART1 TX → `DI/TXD` del módulo RS485 | No usar GPIO20/21 (UART0) |
| GPIO7 | UART1 RX ← `RO/RXD` del módulo RS485 | |
| GPIO8 | LED integrado | Strapping; el LED ya viene cableado en la placa |
| 5V | Entrada de alimentación remota | **Nunca** alimentar a la vez por USB-C |
| 3V3 | Salida del regulador | Alimentar aquí el módulo RS485 y los DS18B20 |
| GPIO2, GPIO9 | **No usar** | Strapping / BOOT |

Módulo RS485 de conmutación automática (sin DE/RE):

| Módulo | ESP32-C3 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| TXD / DI | GPIO6 |
| RXD / RO | GPIO7 |
| A / B | Hilos 5 y 6 del cable |

**Importante:** el ESP32-C3 no tolera 5 V en sus GPIO. Usar un módulo con
transceptor a 3,3 V (MAX3485, SP3485, SN65HVD75…). Si tu módulo es de
MAX485 a 5 V, añadir adaptadores de nivel en TXD/RXD o cambiar de módulo.

## 3. Alimentación remota a 5 V: comprobación de caída

Sección del conductor: 0,22 mm² → ρ/A = 0,0175 / 0,22 ≈ **0,0795 Ω/m**.

- Positivo: 2 hilos en paralelo → 0,0398 Ω/m × 25 m = **0,99 Ω**
- GND: 2 hilos en paralelo → 0,0398 Ω/m × 25 m = **0,99 Ω**
- Resistencia total de bucle ≈ **1,99 Ω**

| Corriente | Caída | Tensión en el esclavo |
|---|---|---|
| 100 mA | 0,20 V | 4,80 V |
| 250 mA | 0,50 V | 4,50 V |
| 500 mA | 0,99 V | 4,01 V (peligroso) |

Medidas adoptadas en el firmware:

- WiFi y Bluetooth apagados, CPU a 160 MHz.
- **100 µF/10 V electrolítico + 100 nF cerámico** en paralelo en la entrada
  5 V del esclavo (lo más cerca posible de la placa).
- Opcional: 10 µF adicional en el pin 3V3.

Si la tensión medida en 5V cae por debajo de **4,3 V** con el equipo en
funcionamiento real, sustituir el cable por uno de mayor sección o enviar
12/24 V por el cable y poner un regulador local junto al esclavo.

## 4. Terminación y polarización del bus RS485

- Resistencia de **120 Ω entre A y B únicamente en los dos extremos**
  (maestro y esclavo). Nunca en dispositivos intermedios.
- Resistencias de polarización (fail-safe) **solo en un punto del bus**,
  normalmente el maestro: ~680 Ω de A a VCC y ~680 Ω de B a GND.
- Muchos módulos TTL-RS485 ya llevan 120 Ω y/o polarización soldadas.
  **Comprobarlo antes de añadir otras**, porque duplicarlas aumenta la carga
  del transceptor y puede provocar errores a 25 m.
- Usar el **par trenzado** del cable para A y B. Mantener A/B alejados de los
  pares de alimentación siempre que sea posible.
- Si cable A/B no es trenzado, bajar a 4800 baudios o usar cable apantallado
  con la pantalla conectada a tierra **solo en el maestro**.

## 5. Lista de materiales (esclavo)

| Cantidad | Elemento |
|---|---|
| 1 | ESP32-C3 SuperMini |
| 1 | Módulo TTL-RS485 de conmutación automática, lógica 3,3 V |
| 4 | DS18B20 (preferiblemente encapsulado sumergible) + 4× VDD/GND/DQ |
| 1 | Resistencia 4,7 kΩ (pull-up OneWire) |
| 1 | Electrolítico 100 µF / 10 V + cerámico 100 nF |
| 1 | Resistencia 120 Ω (si el módulo no la trae) |
| 2 | Resistencias 680 Ω (polarización, solo si hace falta) |
| 1 | Cable 6×0,22 mm², longitud 25 m |
