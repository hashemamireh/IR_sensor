# Wiring Diagram — ESP32-C5 IR Sensor Device

> **Hardware Reference**: [`ESP32-C5-WIFI6-KIT-N32R8.md`](./docs/external/ESP32-C5-WIFI6-KIT-N32R8.md)
>
> **Project Overview**: [`overview.md`](./overview.md)

---

## Pin Assignment Summary

| Device | Device Pin | ESP32-C5 GPIO | Header Position | ESP32-C5 Function | Rationale |
|:---|:---|:---|:---|:---|:---|
| **OPT101** | VCC | 3V3 | Left Pin 1 | Power | 3.3V supply per overview §3.1 |
| **OPT101** | GND | GND | Left Pin 15 | Ground | — |
| **OPT101** | OUT | GPIO4 | Right Pin 8 | ADC1_CH3 | Clean ADC pin, no strapping conflicts |
| **OLED** | VCC | 3V3 | Left Pin 1 | Power | 3.3V regulated supply |
| **OLED** | GND | GND | Left Pin 15 | Ground | — |
| **OLED** | SDA | GPIO23 | Right Pin 5 | I2C SDA (via GPIO Matrix) | Safe GP pin, no conflicts |
| **OLED** | SCL | GPIO24 | Right Pin 4 | I2C SCL (via GPIO Matrix) | Safe GP pin, no conflicts |
| **Encoder** | CLK | GPIO8 | Left Pin 9 | Digital Input (interrupt) | Safe pin, no strapping |
| **Encoder** | DT | GPIO10 | Left Pin 11 | Digital Input (interrupt) | Safe pin, no strapping |
| **Encoder** | SW | GPIO5 | Right Pin 9 | Digital Input (LP_GPIO5) | LP core accessible for deep sleep wake |
| **Encoder** | + | 3V3 | Left Pin 1 | Power | 3.3V supply |
| **Encoder** | GND | GND | Right Pin 1 | Ground | — |
| **Batt Divider** | VBAT | VBAT | Left Pin 16 | Battery rail | Raw LiPo voltage (3.0–4.2V) |
| **Batt Divider** | GND | GND | Left Pin 15 | Ground | — |
| **Batt Divider** | ADC | GPIO6 | Left Pin 7 | ADC1_CH5 | Divided voltage; no strapping, no onboard conflicts |
| **RGB LED** | DATA | GPIO27 | Right Pin 7 | WS2812B data | Already wired onboard — no external wiring needed |

> **Note:** The OPT101 board's other pins (1M, -V, COM) are left unconnected. With pins 1
> and 8 shorted internally on the board, the internal 1 MΩ feedback resistor is engaged,
> giving 14 kHz bandwidth — sufficient for our 10 kHz sampling.

---

## Connection Diagram

```
                    ESP32-C5-WIFI6-KIT-N32R8
                  ┌──────────────────────────┐
    Left Header   │  (antenna end / top)     │   Right Header
                  │                          │
   3V3 ─── Pin 1  │  3V3              GND    │  Pin 1 ─── GND
           Pin 2  │  RST              TX     │  Pin 2
           Pin 3  │  GPIO2            RX     │  Pin 3
           Pin 4  │  GPIO3            GPIO24 │  Pin 4 ─── OLED SCL
           Pin 5  │  GPIO0            GPIO23 │  Pin 5 ─── OLED SDA
           Pin 6  │  GPIO1            N/15   │  Pin 6     (do not use)
  BATT ADC Pin 7  │  GPIO6            GPIO27 │  Pin 7     (onboard RGB LED)
           Pin 8  │  GPIO7            GPIO4  │  Pin 8 ─── OPT101 OUT
   ENC CLK Pin 9  │  GPIO8            GPIO5  │  Pin 9 ─── Encoder SW
           Pin 10 │  GPIO9            NC     │  Pin 10
   ENC DT  Pin 11 │  GPIO10           GPIO28 │  Pin 11
           Pin 12 │  GPIO26           GND    │  Pin 12
           Pin 13 │  GPIO25           GPIO14 │  Pin 13    (USB D+)
           Pin 14 │  5V               GPIO13 │  Pin 14    (USB D-)
   GND ─── Pin 15 │  GND              GND    │  Pin 15
  VBAT ─── Pin 16 │  VBAT             VBAT   │  Pin 16
                  │  (USB connectors / bot)  │
                  └──────────────────────────┘

  ┌───────────┐       ┌──────────┐       ┌──────────────┐
  │  OPT101   │       │ I2C OLED │       │  KY-040      │
  │  Board    │       │ 128x64   │       │  Rotary Enc  │
  ├───────────┤       ├──────────┤       ├──────────────┤
  │ VCC ──────┼─ 3V3  │ VCC ─────┼─ 3V3  │ +   ─────────┼─ 3V3
  │ GND ──────┼─ GND  │ GND ─────┼─ GND  │ GND ─────────┼─ GND
  │ OUT ──────┼─ GP4  │ SDA ─────┼─ GP23 │ CLK ─────────┼─ GP8
  │ 1M   (NC) │       │ SCL ─────┼─ GP24 │ DT  ─────────┼─ GP10
  │ -V   (NC) │       └──────────┘       │ SW  ─────────┼─ GP5
  │ COM  (NC) │                          └──────────────┘
  └───────────┘
                  ┌──────────────────┐
                  │ Battery Voltage  │
                  │ Divider Board    │
                  │ (2× 100 kΩ)      │
                  ├──────────────────┤
                  │ VBAT ────────────┼─ VBAT (Left Pin 16)
                  │ GND ─────────────┼─ GND  (Left Pin 15)
                  │ ADC ─────────────┼─ GP6  (Left Pin 7)
                  └──────────────────┘

  Battery Divider Schematic:

    VBAT ──┤ 100 kΩ ├──┬──┤ 100 kΩ ├── GND
                       │
                      ADC out → GPIO6
                    (VBAT / 2)
```

---

## Design Rationale

### OPT101 → GPIO4 (ADC1_CH3)

- The OPT101 output is analog (0–~2.15 V at 3.3V supply), requiring an ADC1 channel.
- GPIO4 (`ADC1_CH3`) is **not** a strapping pin and has no onboard connections.
- The six ADC1 channels are on GPIO1, 2, 3, 4, 5, 6. Of these:
  - GPIO1, 2 = strapping pins
  - GPIO3 = JTAG (MTDI)
  - GPIO6 = FSPICLK
  - **GPIO4 and GPIO5** are the cleanest choices
- GPIO4 is chosen for ADC, leaving GPIO5 for the encoder button (LP wake).

### I2C OLED → GPIO23 (SDA), GPIO24 (SCL)

- The ESP32-C5's standard I2C peripheral can be routed to **any** GPIO via the GPIO Matrix.
- GPIO23 and GPIO24 are explicitly listed as "recommended general purpose digital pins" in the
  board documentation — no strapping, no onboard connections, no JTAG conflicts.
- **Why not GPIO2/GPIO3 (dedicated LP_I2C)?** GPIO2 is a strapping pin + JTAG MTMS, GPIO3 is
  JTAG MTDI. External pull-ups on strapping pins during boot can cause issues. The OLED doesn't
  need LP core access (it's frozen/off during deep sleep), so there's no benefit to using LP_I2C.
- **Why not VBAT?** VBAT ranges from 3.0–4.2V. At full charge, the OLED module's onboard I2C
  pull-ups would pull SDA/SCL to 4.2V, exceeding the ESP32-C5's absolute maximum GPIO input
  voltage (~3.6V) and risking damage. The 3V3 regulated rail matches logic levels and is safe.

> **Note:** The I2C OLED module typically has built-in pull-up resistors (4.7 kΩ or 10 kΩ to
> VCC). Verify your specific module has them. If not, add external **4.7 kΩ pull-ups** from
> SDA and SCL to 3V3.

### Rotary Encoder → GPIO8 (CLK), GPIO10 (DT), GPIO5 (SW)

- **CLK and DT** need interrupt-capable digital pins. GPIO8 and GPIO10 are both recommended safe
  pins with no strapping or onboard conflicts.
- **SW (button)** is on GPIO5 because:
  - GPIO5 is **LP_GPIO5**, accessible by the Low-Power RISC-V core.
  - The overview §7.1 specifies: *"The LP RISC-V core monitors the GPIO for the encoder press
    while the main HP core is fully powered down."*
  - This is required to implement the ≥2 second long-press deep sleep wake feature.
- The KY-040 module has built-in pull-up resistors on CLK, DT, and SW, so no external pull-ups
  are needed.

### Battery Voltage Divider → GPIO6 (ADC1_CH5)

- The LiPo battery voltage (3.0–4.2V) exceeds the ESP32-C5's ADC input range, so a **resistor
  voltage divider** is needed to scale it down.
- A **2× 100 kΩ** divider (1:1 ratio) gives `Vout = VBAT / 2`:
  - At 4.2V (full charge): **2.1V** — within ADC 6 dB range (~0–2.2V)
  - At 3.0V (cutoff): **1.5V**
  - Current drain through divider: 4.2V / 200 kΩ = **~21 µA** — negligible
- GPIO6 (`ADC1_CH5`) is chosen because it's **not a strapping pin** and has no onboard connections.
  The FSPICLK alternate function is unused in this project.
- In firmware, multiply the ADC-calibrated voltage reading by **2** to recover the actual battery
  voltage.
- Per overview §5.2: battery ADC reads use **one-shot mode** (16–32 samples averaged) every
  30–60 seconds when idle. Battery reads are **suspended during recording** to avoid interfering
  with OPT101 DMA continuous mode on the shared ADC1 unit.

---

## GPIO Usage Summary

| GPIO | Assigned To | Type | Notes |
|:---|:---|:---|:---|
| GPIO4 | OPT101 OUT | Analog In (ADC1_CH3) | Sensor signal |
| GPIO5 | Encoder SW | Digital In (LP_GPIO5) | Button; LP core wake source |
| GPIO6 | Battery Divider | Analog In (ADC1_CH5) | Battery voltage / 2 |
| GPIO8 | Encoder CLK | Digital In (interrupt) | Rotation signal A |
| GPIO10 | Encoder DT | Digital In (interrupt) | Rotation signal B |
| GPIO23 | OLED SDA | I2C Data | Via GPIO Matrix |
| GPIO24 | OLED SCL | I2C Clock | Via GPIO Matrix |
| GPIO27 | RGB LED | WS2812B Data | Onboard, no external wiring |

### Reserved / Unavailable Pins

| GPIO | Reason |
|:---|:---|
| GPIO9 | Onboard BOOT button |
| GPIO11 | UART0 TX (CH343 bridge) |
| GPIO12 | UART0 RX (CH343 bridge) |
| GPIO13 | Native USB D- |
| GPIO14 | Native USB D+ |
| GPIO15 | PSRAM CS (internal, do not use) |
| GPIO16–22 | Internal Flash/PSRAM (not exposed) |
| GPIO27 | Onboard WS2812B LED |

### Available / Unassigned Pins

| GPIO | Notes |
|:---|:---|
| GPIO0 | Available (strapping — use with care) |
| GPIO1 | Available (ADC1_CH0, strapping) |
| GPIO2 | Available (strapping + JTAG — use with care) |
| GPIO3 | Available (JTAG) |
| GPIO7 | Available (strapping) |
| GPIO25 | Available (strapping) |
| GPIO26 | Available (clean GP pin) |
| GPIO28 | Available (strapping) |
