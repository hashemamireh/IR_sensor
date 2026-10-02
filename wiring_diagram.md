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
| **MAX17043** | VCC | 3V3 | Left Pin 1 | Power | 3.3V logic supply |
| **MAX17043** | GND | GND | Left Pin 15 | Ground | — |
| **MAX17043** | CELL | VBAT | Left Pin 16 | Battery voltage | Direct battery measurement input (no divider needed) |
| **MAX17043** | SDA | GPIO23 | Right Pin 5 | I2C SDA (shared with OLED) | I2C address 0x36 |
| **MAX17043** | SCL | GPIO24 | Right Pin 4 | I2C SCL (shared with OLED) | Shared I2C bus |
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
           Pin 4  │  GPIO3            GPIO24 │  Pin 4 ─── I2C SCL (OLED + MAX17043)
           Pin 5  │  GPIO0            GPIO23 │  Pin 5 ─── I2C SDA (OLED + MAX17043)
           Pin 6  │  GPIO1            N/15   │  Pin 6     (do not use)
           Pin 7  │  GPIO6            GPIO27 │  Pin 7     (onboard RGB LED)
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
              │ MAX17043         │
              │ Fuel Gauge (I2C) │
              │ Addr: 0x36       │
              ├──────────────────┤
              │ VCC ─────────────┼─ 3V3  (Left Pin 1)
              │ GND ─────────────┼─ GND  (Left Pin 15)
              │ CELL ────────────┼─ VBAT (Left Pin 16)
              │ SDA ─────────────┼─ GP23 (shared with OLED)
              │ SCL ─────────────┼─ GP24 (shared with OLED)
              └──────────────────┘

  I2C Bus Topology:

    ESP32-C5 GP23 (SDA) ──┬── OLED SDA (0x3C)
                          └── MAX17043 SDA (0x36)

    ESP32-C5 GP24 (SCL) ──┬── OLED SCL
                          └── MAX17043 SCL
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

### MAX17043 Fuel Gauge → Shared I2C Bus (GPIO23 SDA, GPIO24 SCL)

- The MAX17043 is an I2C fuel gauge (address **0x36**) that shares the same I2C bus as the OLED
  display (address **0x3C**). No additional GPIO pins are consumed — only a shared bus.
- **Why not an ADC voltage divider?** Using the ESP32-C5's internal ADC for battery monitoring
  would consume one of the six ADC1 channels. More critically, the ADC continuous DMA mode
  (used for OPT101 recording) configures the ADC unit globally — reading a battery channel
  during recording would interfere with sensor data acquisition. The MAX17043 eliminates this
  conflict entirely since it uses I2C, not ADC.
- **CELL pin** connects directly to the VBAT rail (battery positive). The MAX17043 handles the
  full LiPo voltage range (2.5–4.5V) internally — no external voltage divider is needed.
- **VCC pin** is powered from the 3V3 regulated rail (not VBAT) for stable logic-level I2C
  communication.
- The MAX17043's **ModelGauge™** algorithm provides accurate state-of-charge (SoC%) without
  requiring a sense resistor or coulomb counting, and compensates for load and temperature
  variations — significantly more accurate than a voltage-to-percentage lookup table.
- **I2C bus during recording**: Battery reads are deferred during active recording to minimize
  I2C bus traffic (the OLED is also frozen). A reading is taken immediately before and after
  each recording session. This is a software-level precaution, not a hardware conflict.

---

## GPIO Usage Summary

| GPIO | Assigned To | Type | Notes |
|:---|:---|:---|:---|
| GPIO4 | OPT101 OUT | Analog In (ADC1_CH3) | Sensor signal |
| GPIO5 | Encoder SW | Digital In (LP_GPIO5) | Button; LP core wake source |
| GPIO8 | Encoder CLK | Digital In (interrupt) | Rotation signal A |
| GPIO10 | Encoder DT | Digital In (interrupt) | Rotation signal B |
| GPIO23 | I2C SDA | I2C Data | Shared: OLED (0x3C) + MAX17043 (0x36) |
| GPIO24 | I2C SCL | I2C Clock | Shared: OLED + MAX17043 |
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
| GPIO6 | Available (ADC1_CH5, FSPICLK — no strapping) |
| GPIO7 | Available (strapping) |
| GPIO25 | Available (strapping) |
| GPIO26 | Available (clean GP pin) |
| GPIO28 | Available (strapping) |
