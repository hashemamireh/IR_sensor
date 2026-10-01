# Waveshare ESP32-C5-WIFI6-KIT Pinout & Hardware Reference

> **Documentation & Source References:**
> - Official Wiki: https://docs.waveshare.com/ESP32-C5-WIFI6-KIT
> - Resources & Schematics: https://docs.waveshare.com/ESP32-C5-WIFI6-KIT/Resources-And-Documents
> - Espressif ESP32-C5 Datasheet: https://documentation.espressif.com/esp32-c5_datasheet_en.pdf
> - Espressif ESP32-C5 Technical Reference Manual: https://documentation.espressif.com/esp32-c5_technical_reference_manual_en.pdf

---

## 1. Board Overview & System Specifications

The **Waveshare ESP32-C5-WIFI6-KIT** is a development board powered by the **ESP32-C5-WROOM-1** series module. It is engineered for dual-band Wi-Fi 6 and multi-protocol IoT applications.

| Feature | Specification |
| :--- | :--- |
| **SoC / Module** | Espressif ESP32-C5 / ESP32-C5-WROOM-1 (or WROOM-1U for external antenna) |
| **Core Architecture** | 32-bit RISC-V single-core processor @ up to 240 MHz |
| **SRAM** | 384 KB on-chip SRAM |
| **ROM** | 320 KB on-chip ROM |
| **PSRAM** | 8 MB SPI PSRAM (on -N16R8 / -N32R8 models) |
| **Flash Memory** | 16 MB (-N16R8) or 32 MB (-N32R8) SPI NOR Flash |
| **Wireless Protocols** | • Dual-Band Wi-Fi 6 (IEEE 802.11 ax, 2.4 GHz & 5 GHz, backward compatible 802.11 b/g/n)<br>• Bluetooth 5 (LE) & Bluetooth Mesh<br>• IEEE 802.15.4 (Zigbee 3.0 & Thread) |
| **General Purpose I/Os** | 29 GPIOs total (GPIO0–GPIO28); 22 accessible via headers |
| **Hardware Peripherals** | 2× SPI, 2× UART, 1× I2C, RMT, 6ch LED PWM, 1× 12-bit ADC (6ch), CAN FD (TWAI), USB Serial/JTAG, ETM, MCPWM |
| **USB Interfaces** | Dual USB Type-C ports:<br>1. **Left ("USB")**: Native ESP32-C5 USB Serial/JTAG controller<br>2. **Right ("UART")**: Onboard CH343 USB-to-UART bridge (UART0) |
| **Power Inputs** | 5V via USB Type-C, external 5V header pin, or 3.7V Li-ion battery via MX1.25 connector |
| **Battery Management** | Onboard lithium battery charging circuit with BAT and CHG indicators |
| **Onboard User Elements** | WS2812B addressable RGB LED (GPIO27), BOOT button (GPIO9), RST button |

---

## 2. Physical Pinout Tables

The board exposes two 16-pin 2.54 mm pitch headers on the left and right sides. All pin rows are ordered from **top (antenna side) to bottom (USB connectors side)**.

### Left Header (Pins 1–16, Top to Bottom)

| Pin # | Header | Silkscreen | Pin Name / GPIO | PWM Capable | ADC Channel | SPI (FSPI) | Low-Power (LP) Bus | SDIO Bus | JTAG / Other | Strapping / Hardware Notes |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **1** | Left | `3V3` | `3V3` | No | — | — | — | — | — | **Power Output / Supply Rail** (+3.3V) |
| **2** | Left | `RST` | `RST` | No | — | — | — | — | — | **Active-Low Reset** (Connected to RST button) |
| **3** | Left | `2` | `GPIO2` | **Yes** | `ADC1_CH1` | `FSPIQ` (MISO) | `LP_I2C_SDA`<br>`LP_UART_RTSN`<br>`LP_GPIO2` | — | `MTMS` (JTAG) | **Strapping Pin** |
| **4** | Left | `3` | `GPIO3` | **Yes** | `ADC1_CH2` | — | `LP_I2C_SCL`<br>`LP_UART_CTSN`<br>`LP_GPIO3` | — | `MTDI` (JTAG) | — |
| **5** | Left | `0` | `GPIO0` | **Yes** | — | — | `LP_UART_DTRN`<br>`LP_GPIO0` | — | — | `XTAL_32K_P` (32.768 kHz Osc P), **Strapping Pin** |
| **6** | Left | `1` | `GPIO1` | **Yes** | `ADC1_CH0` | — | `LP_UART_DSRN`<br>`LP_GPIO1` | — | — | `XTAL_32K_N` (32.768 kHz Osc N), **Strapping Pin** |
| **7** | Left | `6` | `GPIO6` | **Yes** | `ADC1_CH5` | `FSPICLK` | `LP_GPIO6` | — | — | — |
| **8** | Left | `7` | `GPIO7` | **Yes** | — | `FSPID` (MOSI) | — | `SDIO_DATA1` | — | **Strapping Pin** |
| **9** | Left | `8` | `GPIO8` | **Yes** | — | — | — | `SDIO_DATA0` | `PAD_COMP0` | Analog Comparator 0 |
| **10** | Left | `9` | `GPIO9` | **Yes** | — | — | — | `SDIO_CLK` | `PAD_COMP1` | **BOOT Strapping Pin**; connected to onboard **BOOT Button** |
| **11** | Left | `10` | `GPIO10` | **Yes** | — | `FSPICS0` (CS) | — | `SDIO_CMD` | — | Fast SPI Chip Select 0 |
| **12** | Left | `26` | `GPIO26` | **Yes** | — | — | — | — | — | General-purpose digital I/O |
| **13** | Left | `25` | `GPIO25` | **Yes** | — | — | — | — | — | **Strapping Pin**; general-purpose digital I/O |
| **14** | Left | `5V` | `5V` | No | — | — | — | — | — | **5V Power Rail** (From USB VBUS or external 5V input) |
| **15** | Left | `GND` | `GND` | No | — | — | — | — | — | **Ground Reference** (0V) |
| **16** | Left | `VBAT` | `VBAT` | No | — | — | — | — | — | **Battery Rail** (+3.7V Li-ion input/charger rail) |

---

### Right Header (Pins 1–16, Top to Bottom)

| Pin # | Header | Silkscreen | Pin Name / GPIO | PWM Capable | ADC Channel | SPI (FSPI) | Low-Power (LP) Bus | SDIO Bus | JTAG / USB / Serial | Strapping / Hardware Notes |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **1** | Right | `GND` | `GND` | No | — | — | — | — | — | **Ground Reference** (0V) |
| **2** | Right | `TX` | `GPIO11` | **Yes** | — | — | — | — | `U0TXD` (UART0 TX) | Connected to onboard CH343 USB-to-UART bridge |
| **3** | Right | `RX` | `GPIO12` | **Yes** | — | — | — | — | `U0RXD` (UART0 RX) | Connected to onboard CH343 USB-to-UART bridge |
| **4** | Right | `24` | `GPIO24` | **Yes** | — | — | — | — | — | General-purpose digital I/O |
| **5** | Right | `23` | `GPIO23` | **Yes** | — | — | — | — | — | General-purpose digital I/O |
| **6** | Right | `N/15` | `GPIO15` | **Yes** | — | — | — | — | `SPICS1` | **Caution**: Reserved on PSRAM models (-N16R8/-N32R8); do not use externally |
| **7** | Right | `27` | `GPIO27` | **Yes** | — | — | — | — | — | **Strapping Pin**; connected onboard to **WS2812B RGB LED** |
| **8** | Right | `4` | `GPIO4` | **Yes** | `ADC1_CH3` | `FSPIHD` (Hold) | `LP_UART_RXD`<br>`LP_GPIO4` | — | `MTCK` (JTAG) | — |
| **9** | Right | `5` | `GPIO5` | **Yes** | `ADC1_CH4` | `FSPIWP` (WP) | `LP_UART_TXD`<br>`LP_GPIO5` | — | `MTDO` (JTAG) | — |
| **10** | Right | `NC` | `NC` | No | — | — | — | — | — | **Not Connected** / Reserved |
| **11** | Right | `28` | `GPIO28` | **Yes** | — | — | — | — | — | **Strapping Pin**; general-purpose digital I/O |
| **12** | Right | `GND` | `GND` | No | — | — | — | — | — | **Ground Reference** (0V) |
| **13** | Right | `14` | `GPIO14` | **Yes** | — | — | — | `SDIO_DATA2` | `USB_D+` | Native USB Serial/JTAG D+ (Left USB Type-C) |
| **14** | Right | `13` | `GPIO13` | **Yes** | — | — | — | `SDIO_DATA3` | `USB_D-` | Native USB Serial/JTAG D- (Left USB Type-C) |
| **15** | Right | `GND` | `GND` | No | — | — | — | — | — | **Ground Reference** (0V) |
| **16** | Right | `VBAT` | `VBAT` | No | — | — | — | — | — | **Battery Rail** (+3.7V Li-ion input/charger rail) |

---

## 3. Peripheral-to-Pin Lookup Matrices

Use these lookup tables when configuring communication buses, analog sampling, or interrupts in firmware (ESP-IDF / Arduino).

### 3.1. Analog-to-Digital Converter (12-bit ADC1)
The ESP32-C5 integrates a single 12-bit SAR ADC controller (`ADC1`) providing 6 external analog input channels:

| ADC Signal | GPIO Pin | Physical Header Pin | Alternate Functions |
| :--- | :--- | :--- | :--- |
| **ADC1_CH0** | `GPIO1` | Left Pin 6 | `XTAL_32K_N`, `LP_UART_DSRN`, `LP_GPIO1` |
| **ADC1_CH1** | `GPIO2` | Left Pin 3 | `MTMS`, `FSPIQ`, `LP_I2C_SDA`, `LP_UART_RTSN` |
| **ADC1_CH2** | `GPIO3` | Left Pin 4 | `MTDI`, `LP_I2C_SCL`, `LP_UART_CTSN` |
| **ADC1_CH3** | `GPIO4` | Right Pin 8 | `MTCK`, `FSPIHD`, `LP_UART_RXD`, `LP_GPIO4` |
| **ADC1_CH4** | `GPIO5` | Right Pin 9 | `MTDO`, `FSPIWP`, `LP_UART_TXD`, `LP_GPIO5` |
| **ADC1_CH5** | `GPIO6` | Left Pin 7 | `FSPICLK`, `LP_GPIO6` |

---

### 3.2. Fast SPI (FSPI / SPI2)
Hardware default IOMUX mapping for Fast SPI (can also be routed via GPIO Matrix):

| SPI Signal | Role | GPIO Pin | Physical Header Pin |
| :--- | :--- | :--- | :--- |
| **FSPICLK** | Serial Clock | `GPIO6` | Left Pin 7 |
| **FSPID** | Serial Data In/Out 0 (MOSI) | `GPIO7` | Left Pin 8 |
| **FSPIQ** | Serial Data In/Out 1 (MISO) | `GPIO2` | Left Pin 3 |
| **FSPICS0** | Chip Select 0 | `GPIO10` | Left Pin 11 |
| **FSPIHD** | Hold / Quad SPI Data 3 | `GPIO4` | Right Pin 8 |
| **FSPIWP** | Write Protect / Quad SPI Data 2 | `GPIO5` | Right Pin 9 |

---

### 3.3. I2C Bus & Low-Power I2C (LP_I2C)
The ESP32-C5 standard I2C can be routed to any digital GPIO via the GPIO Matrix. Additionally, dedicated Low-Power I2C (`LP_I2C`) pins are available:

| I2C Signal | Default Hardware Pin | Physical Header Pin | Description |
| :--- | :--- | :--- | :--- |
| **LP_I2C_SDA** | `GPIO2` | Left Pin 3 | Low-Power I2C Data line (ULP / Low-Power core accessible) |
| **LP_I2C_SCL** | `GPIO3` | Left Pin 4 | Low-Power I2C Clock line (ULP / Low-Power core accessible) |

*Note: For standard HP (High Performance) I2C, any free GPIOs (e.g., GPIO23 & GPIO24) can be used with external pull-ups.*

---

### 3.4. UART Interfaces & Low-Power UART (LP_UART)

| Interface | Signal | GPIO Pin | Physical Header Pin | Default Connection / Usage |
| :--- | :--- | :--- | :--- | :--- |
| **UART0** | `U0TXD` | `GPIO11` | Right Pin 2 (`TX`) | Connected to onboard CH343 USB-UART bridge |
| **UART0** | `U0RXD` | `GPIO12` | Right Pin 3 (`RX`) | Connected to onboard CH343 USB-UART bridge |
| **LP_UART** | `LP_UART_TXD` | `GPIO5` | Right Pin 9 | Low-Power UART Transmit |
| **LP_UART** | `LP_UART_RXD` | `GPIO4` | Right Pin 8 | Low-Power UART Receive |
| **LP_UART** | `LP_UART_RTSN` | `GPIO2` | Left Pin 3 | Low-Power UART RTS (Flow Control) |
| **LP_UART** | `LP_UART_CTSN` | `GPIO3` | Left Pin 4 | Low-Power UART CTS (Flow Control) |
| **LP_UART** | `LP_UART_DTRN` | `GPIO0` | Left Pin 5 | Low-Power UART DTR |
| **LP_UART** | `LP_UART_DSRN` | `GPIO1` | Left Pin 6 | Low-Power UART DSR |

---

### 3.5. SDIO Slave / Host Interface

| SDIO Signal | Function | GPIO Pin | Physical Header Pin |
| :--- | :--- | :--- | :--- |
| **SDIO_CLK** | Clock | `GPIO9` | Left Pin 10 |
| **SDIO_CMD** | Command | `GPIO10` | Left Pin 11 |
| **SDIO_DATA0** | Data Bit 0 | `GPIO8` | Left Pin 9 |
| **SDIO_DATA1** | Data Bit 1 | `GPIO7` | Left Pin 8 |
| **SDIO_DATA2** | Data Bit 2 | `GPIO14` | Right Pin 13 |
| **SDIO_DATA3** | Data Bit 3 | `GPIO13` | Right Pin 14 |

---

### 3.6. USB & JTAG Debugging Interfaces

| Interface | Signal | GPIO Pin | Description |
| :--- | :--- | :--- | :--- |
| **Native USB** | `USB_D-` | `GPIO13` | Connected directly to Left USB-C port (`USB`) |
| **Native USB** | `USB_D+` | `GPIO14` | Connected directly to Left USB-C port (`USB`) |
| **JTAG** | `MTMS` | `GPIO2` | JTAG Test Mode Select |
| **JTAG** | `MTDI` | `GPIO3` | JTAG Test Data In |
| **JTAG** | `MTCK` | `GPIO4` | JTAG Test Clock |
| **JTAG** | `MTDO` | `GPIO5` | JTAG Test Data Out |

---

### 3.7. Onboard Hardware Peripherals & Control Elements

| Component | Identifier | Connected Pin | Behavior & Usage |
| :--- | :--- | :--- | :--- |
| **Addressable RGB LED** | `RGB` / `IO27` | `GPIO27` | Single WS2812B / XL-0807RGBC NeoPixel LED. Data in driven by GPIO27. |
| **Boot Mode Button** | `BOOT` | `GPIO9` | Pulls GPIO9 to GND when pressed. Hold down while pulsing RST to enter download mode. |
| **Hardware Reset Button** | `RST` | `RST` / `CHIP_PU` | Pulls EN/RST pin to GND to perform hard system reset. |
| **Power LED** | `PWR` | Power Circuit | Red LED, illuminates when 3.3V power is present. |
| **Battery Status LED** | `BAT` | Battery Circuit | Indicates battery presence/status. |
| **Charge Status LED** | `CHG` | Charge IC | Illuminates during active battery charging via USB. |
| **Left USB Port** | `USB` | `GPIO13` (D-), `GPIO14` (D+) | Native USB Serial/JTAG controller for firmware upload, JTAG debugging, and CDC ACM communication. |
| **Right USB Port** | `UART` | `GPIO11` (TX), `GPIO12` (RX) | Onboard CH343 USB-to-UART converter for primary serial console logging and standard flashing. |
| **Battery Connector** | `BAT` | `VBAT`, `GND` | 2-pin MX1.25 pitch connector for 3.7V rechargeable lithium polymer/ion cell. |

---

## 4. Hardware Design Cautions & Strapping Pins

When selecting pins for external circuits or sensors, observe the following constraints:

1. **Internal Flash / PSRAM Pins (GPIO16–GPIO22)**:
   - These pins are dedicated internally inside the ESP32-C5-WROOM-1 module to communicate with SPI Flash and PSRAM (SPI0/SPI1).
   - They are **not broken out** to the headers and must **never** be reconfigured in software.
2. **PSRAM Conflict Pin (GPIO15 / Silkscreen `N/15`)**:
   - On boards equipped with PSRAM (`ESP32-C5-WIFI6-KIT-N16R8` and `ESP32-C5-WIFI6-KIT-N32R8`), GPIO15 is internally dedicated to PSRAM chip select (`SPICS1`).
   - Leave the `N/15` pin disconnected when using PSRAM-enabled modules.
3. **Boot Strapping Pins**:
   - `GPIO9`: Boot mode pin. Internally pulled high. Must be LOW during chip reset to enter UART Download mode. (Controlled by onboard BOOT button).
   - `GPIO2`, `GPIO7`, `GPIO25`, `GPIO27`, `GPIO28`: Used as boot strapping pins for voltage/configuration selection during power-up/reset. Avoid connecting external low-impedance pull-ups/pull-downs that could force invalid strapping states at boot.
4. **Native USB Pins (`GPIO13` / `GPIO14`)**:
   - Reconfiguring GPIO13/GPIO14 as general digital pins will disable native USB Serial/JTAG connectivity on the left USB-C port.
5. **Recommended General Purpose Digital Pins (Safe for any IO)**:
   - `GPIO23`, `GPIO24`, `GPIO26`, `GPIO8`, `GPIO10` have no conflict with onboard LEDs or boot strapping.

---

## 5. Machine-Readable JSON Pinout Schema

For automated code generation, linting, and LLM reasoning, the complete pin dictionary is provided below:

```json
{
  "board": "Waveshare ESP32-C5-WIFI6-KIT",
  "module": "ESP32-C5-WROOM-1",
  "soc": "ESP32-C5",
  "total_header_pins": 32,
  "headers": {
    "left": [
      {
        "pin_number": 1,
        "silkscreen": "3V3",
        "name": "3V3",
        "type": "power",
        "pwm": false,
        "description": "3.3V Power Rail Output/Supply"
      },
      {
        "pin_number": 2,
        "silkscreen": "RST",
        "name": "RST",
        "type": "reset",
        "pwm": false,
        "description": "Active-low reset pin connected to RST button"
      },
      {
        "pin_number": 3,
        "silkscreen": "2",
        "gpio": 2,
        "name": "GPIO2",
        "type": "gpio",
        "pwm": true,
        "adc_channel": "ADC1_CH1",
        "spi_function": "FSPIQ",
        "i2c_function": "LP_I2C_SDA",
        "uart_function": "LP_UART_RTSN",
        "lp_gpio": "LP_GPIO2",
        "jtag_function": "MTMS",
        "strapping": true,
        "notes": "Boot strapping pin"
      },
      {
        "pin_number": 4,
        "silkscreen": "3",
        "gpio": 3,
        "name": "GPIO3",
        "type": "gpio",
        "pwm": true,
        "adc_channel": "ADC1_CH2",
        "spi_function": null,
        "i2c_function": "LP_I2C_SCL",
        "uart_function": "LP_UART_CTSN",
        "lp_gpio": "LP_GPIO3",
        "jtag_function": "MTDI",
        "strapping": false,
        "notes": null
      },
      {
        "pin_number": 5,
        "silkscreen": "0",
        "gpio": 0,
        "name": "GPIO0",
        "type": "gpio",
        "pwm": true,
        "adc_channel": null,
        "spi_function": null,
        "i2c_function": null,
        "uart_function": "LP_UART_DTRN",
        "lp_gpio": "LP_GPIO0",
        "other_functions": ["XTAL_32K_P"],
        "strapping": true,
        "notes": "32.768kHz crystal oscillator input P, strapping pin"
      },
      {
        "pin_number": 6,
        "silkscreen": "1",
        "gpio": 1,
        "name": "GPIO1",
        "type": "gpio",
        "pwm": true,
        "adc_channel": "ADC1_CH0",
        "spi_function": null,
        "i2c_function": null,
        "uart_function": "LP_UART_DSRN",
        "lp_gpio": "LP_GPIO1",
        "other_functions": ["XTAL_32K_N"],
        "strapping": true,
        "notes": "32.768kHz crystal oscillator input N, strapping pin"
      },
      {
        "pin_number": 7,
        "silkscreen": "6",
        "gpio": 6,
        "name": "GPIO6",
        "type": "gpio",
        "pwm": true,
        "adc_channel": "ADC1_CH5",
        "spi_function": "FSPICLK",
        "i2c_function": null,
        "uart_function": null,
        "lp_gpio": "LP_GPIO6",
        "strapping": false,
        "notes": "Fast SPI Clock"
      },
      {
        "pin_number": 8,
        "silkscreen": "7",
        "gpio": 7,
        "name": "GPIO7",
        "type": "gpio",
        "pwm": true,
        "adc_channel": null,
        "spi_function": "FSPID",
        "sdio_function": "SDIO_DATA1",
        "strapping": true,
        "notes": "Fast SPI MOSI / SDIO Data 1, strapping pin"
      },
      {
        "pin_number": 9,
        "silkscreen": "8",
        "gpio": 8,
        "name": "GPIO8",
        "type": "gpio",
        "pwm": true,
        "adc_channel": null,
        "spi_function": null,
        "sdio_function": "SDIO_DATA0",
        "other_functions": ["PAD_COMP0"],
        "strapping": false,
        "notes": "Analog comparator 0"
      },
      {
        "pin_number": 10,
        "silkscreen": "9",
        "gpio": 9,
        "name": "GPIO9",
        "type": "gpio",
        "pwm": true,
        "adc_channel": null,
        "spi_function": null,
        "sdio_function": "SDIO_CLK",
        "other_functions": ["PAD_COMP1"],
        "strapping": true,
        "onboard_connection": "BOOT_BUTTON",
        "notes": "Boot mode strapping pin; active low onboard BOOT button"
      },
      {
        "pin_number": 11,
        "silkscreen": "10",
        "gpio": 10,
        "name": "GPIO10",
        "type": "gpio",
        "pwm": true,
        "adc_channel": null,
        "spi_function": "FSPICS0",
        "sdio_function": "SDIO_CMD",
        "strapping": false,
        "notes": "Fast SPI Chip Select 0"
      },
      {
        "pin_number": 12,
        "silkscreen": "26",
        "gpio": 26,
        "name": "GPIO26",
        "type": "gpio",
        "pwm": true,
        "adc_channel": null,
        "spi_function": null,
        "strapping": false,
        "notes": "General purpose digital I/O"
      },
      {
        "pin_number": 13,
        "silkscreen": "25",
        "gpio": 25,
        "name": "GPIO25",
        "type": "gpio",
        "pwm": true,
        "adc_channel": null,
        "spi_function": null,
        "strapping": true,
        "notes": "General purpose digital I/O, strapping pin"
      },
      {
        "pin_number": 14,
        "silkscreen": "5V",
        "name": "5V",
        "type": "power",
        "pwm": false,
        "description": "5V power input / USB VBUS rail"
      },
      {
        "pin_number": 15,
        "silkscreen": "GND",
        "name": "GND",
        "type": "ground",
        "pwm": false,
        "description": "Ground reference"
      },
      {
        "pin_number": 16,
        "silkscreen": "VBAT",
        "name": "VBAT",
        "type": "power",
        "pwm": false,
        "description": "3.7V Lithium Battery Rail"
      }
    ],
    "right": [
      {
        "pin_number": 1,
        "silkscreen": "GND",
        "name": "GND",
        "type": "ground",
        "pwm": false,
        "description": "Ground reference"
      },
      {
        "pin_number": 2,
        "silkscreen": "TX",
        "gpio": 11,
        "name": "GPIO11",
        "type": "gpio",
        "pwm": true,
        "uart_function": "U0TXD",
        "onboard_connection": "CH343_UART_TX",
        "strapping": false,
        "notes": "UART0 TX connected to CH343 USB-to-UART bridge"
      },
      {
        "pin_number": 3,
        "silkscreen": "RX",
        "gpio": 12,
        "name": "GPIO12",
        "type": "gpio",
        "pwm": true,
        "uart_function": "U0RXD",
        "onboard_connection": "CH343_UART_RX",
        "strapping": false,
        "notes": "UART0 RX connected to CH343 USB-to-UART bridge"
      },
      {
        "pin_number": 4,
        "silkscreen": "24",
        "gpio": 24,
        "name": "GPIO24",
        "type": "gpio",
        "pwm": true,
        "strapping": false,
        "notes": "General purpose digital I/O"
      },
      {
        "pin_number": 5,
        "silkscreen": "23",
        "gpio": 23,
        "name": "GPIO23",
        "type": "gpio",
        "pwm": true,
        "strapping": false,
        "notes": "General purpose digital I/O"
      },
      {
        "pin_number": 6,
        "silkscreen": "N/15",
        "gpio": 15,
        "name": "GPIO15",
        "type": "gpio",
        "pwm": true,
        "other_functions": ["SPICS1"],
        "strapping": false,
        "notes": "Reserved internally for PSRAM on -N16R8 / -N32R8 boards. Leave NC."
      },
      {
        "pin_number": 7,
        "silkscreen": "27",
        "gpio": 27,
        "name": "GPIO27",
        "type": "gpio",
        "pwm": true,
        "onboard_connection": "WS2812B_RGB_LED",
        "strapping": true,
        "notes": "Drives onboard WS2812B addressable RGB LED; strapping pin"
      },
      {
        "pin_number": 8,
        "silkscreen": "4",
        "gpio": 4,
        "name": "GPIO4",
        "type": "gpio",
        "pwm": true,
        "adc_channel": "ADC1_CH3",
        "spi_function": "FSPIHD",
        "uart_function": "LP_UART_RXD",
        "lp_gpio": "LP_GPIO4",
        "jtag_function": "MTCK",
        "strapping": false,
        "notes": null
      },
      {
        "pin_number": 9,
        "silkscreen": "5",
        "gpio": 5,
        "name": "GPIO5",
        "type": "gpio",
        "pwm": true,
        "adc_channel": "ADC1_CH4",
        "spi_function": "FSPIWP",
        "uart_function": "LP_UART_TXD",
        "lp_gpio": "LP_GPIO5",
        "jtag_function": "MTDO",
        "strapping": false,
        "notes": null
      },
      {
        "pin_number": 10,
        "silkscreen": "NC",
        "name": "NC",
        "type": "nc",
        "pwm": false,
        "description": "Not Connected"
      },
      {
        "pin_number": 11,
        "silkscreen": "28",
        "gpio": 28,
        "name": "GPIO28",
        "type": "gpio",
        "pwm": true,
        "strapping": true,
        "notes": "General purpose digital I/O, strapping pin"
      },
      {
        "pin_number": 12,
        "silkscreen": "GND",
        "name": "GND",
        "type": "ground",
        "pwm": false,
        "description": "Ground reference"
      },
      {
        "pin_number": 13,
        "silkscreen": "14",
        "gpio": 14,
        "name": "GPIO14",
        "type": "gpio",
        "pwm": true,
        "sdio_function": "SDIO_DATA2",
        "usb_jtag": "USB_D+",
        "onboard_connection": "NATIVE_USB_DP",
        "strapping": false,
        "notes": "Native USB Serial/JTAG D+ line"
      },
      {
        "pin_number": 14,
        "silkscreen": "13",
        "gpio": 13,
        "name": "GPIO13",
        "type": "gpio",
        "pwm": true,
        "sdio_function": "SDIO_DATA3",
        "usb_jtag": "USB_D-",
        "onboard_connection": "NATIVE_USB_DM",
        "strapping": false,
        "notes": "Native USB Serial/JTAG D- line"
      },
      {
        "pin_number": 15,
        "silkscreen": "GND",
        "name": "GND",
        "type": "ground",
        "pwm": false,
        "description": "Ground reference"
      },
      {
        "pin_number": 16,
        "silkscreen": "VBAT",
        "name": "VBAT",
        "type": "power",
        "pwm": false,
        "description": "3.7V Lithium Battery Rail"
      }
    ]
  },
  "onboard_devices": {
    "rgb_led": {
      "type": "WS2812B",
      "data_pin": "GPIO27"
    },
    "boot_button": {
      "pin": "GPIO9",
      "active": "LOW"
    },
    "reset_button": {
      "pin": "RST",
      "active": "LOW"
    },
    "usb_native": {
      "port": "Left USB-C",
      "dm_pin": "GPIO13",
      "dp_pin": "GPIO14"
    },
    "usb_uart": {
      "port": "Right USB-C",
      "ic": "CH343",
      "tx_pin": "GPIO11",
      "rx_pin": "GPIO12"
    },
    "battery_connector": {
      "type": "MX1.25 2-pin",
      "nominal_voltage": "3.7V"
    }
  }
}
```