# ☀️ Helion — High-Precision Dual-Axis Solar Tracker & Telemetry System

[![STM32](https://img.shields.io/badge/Microcontroller-STM32F103C8T6-blue.svg)](https://www.st.com/en/microcontrollers-microprocessors/stm32f103c8.html)
[![Sensors](https://img.shields.io/badge/Sensors-3x%20INA219%20%7C%204x%20GL5528%20LDR-orange.svg)]()
[![Actuators](https://img.shields.io/badge/Actuators-2x%20MG90S%20Servos-green.svg)]()
[![Status](https://img.shields.io/badge/Status-Hardware%20Audited%20%26%20Verified-brightgreen.svg)]()

**Helion** is an autonomous electro-mechanical dual-axis solar tracker and multi-channel telemetry platform. It keeps a photovoltaic (PV) solar panel oriented perpendicular to incoming solar rays while simultaneously recording **tracked energy generation**, **baseline static panel yield**, and **parasitic system power consumption**.

---

## 📸 System Overview & Hardware Reference

![Complete System Wiring Overview](images/system_wiring_overview.jpg)

> [!CAUTION]
> **SCHEMATIC CAUTION — DO NOT WIRE DIRECTLY FROM THE OVERVIEW DRAWING:**
> The original graphical diagram `images/system_wiring_overview.jpg` contains serious drawing errors: a dead short across 5 V through the INA219 shunt, an accidental 5 V-to-3.3 V rail tie, and a low-side power switch. Always follow the written specifications, the 51-point checklist in the audit report, and the corrected architecture below.

### Core Capabilities
* **Active Dual-Axis Tracking:** Pan (Azimuth) and Tilt (Elevation) controlled by metal-gear MG90S servos with continuous pulse-gated holding torque against wind gusts.
* **Normalized Quad-LDR Sensing:** Mathematical error normalization algorithm ($\pm 100\%$ scale) immune to ambient room lighting, cloud cover, and seasonal brightness changes.
* **Triple INA219 Telemetry:** Simultaneous high-side voltage, current, and power logging over I²C (`0x40`: Tracked Panel, `0x41`: System Input Rail, `0x44`: Fixed Reference Panel).
* **Baseline Benchmarking:** Direct comparative yield tracking against an identical static solar panel using matched $10\ \Omega$ maximum power point load resistors.
* **Reverse Polarity, TVS & Surge Protection:** 3 A Schottky diode protection, 2 A fuse, 5.6 V overvoltage TVS clamp, low-ESR $1000\ \mu\text{F}$ inrush suppression bank, and isolated programmer lines with hardware NRST.

---

## 🛠️ Complete Bill of Materials (BOM)

| Item Description | Build Qty | Suggested Buy | Purpose & Selection Notes |
|:---|:---:|:---:|:---|
| **STM32F103C8T6 Blue Pill** | 1 | 2 | Main ARM Cortex-M3 controller board running @ 72 MHz |
| **ST-Link V2 Debugger** | 1 | 1 | Hardware SWD programmer (SWDIO, SWCLK, GND, and **NRST**) |
| **TowerPro MG90S Servo** | 2 | 3 | Metal-gear servos ($2.2\text{ kg}\cdot\text{cm}$) for Pan & Tilt |
| **GL5528 CdS LDR** | 4 | 6 | Light differential quadrant sensors with 35–40mm shadow baffle |
| **INA219 I²C Power Module** | 3 | 4 | Multi-drop power sensor with solder pads for A0/A1 addressing |
| **SSD1306 0.96" OLED** | 1 | 1 | Real-time telemetry dashboard (128×64 resolution, address `0x3C`) |
| **6V 3W Polycrystalline Panel** | 2 | 2 | Matched pair (1× Tracked DUT, 1× Fixed Reference) |
| **10–12 Ω 5W Resistor** | 2 | 4 | MPP test load resistors matched within $\pm 0.1\ \Omega$ (heatsinked) |
| **LM2596 DC-DC Buck Converter** | 1 | 2 | High-efficiency step-down to regulated 5.00 V |
| **9 V 2 A DC Power Adapter** | 1 | 1 | Main system wall supply (5.5×2.1 mm center-positive) |
| **DC Barrel Jack (Female)** | 1 | 1 | Chassis socket with positive-line series slide switch & 2A fuse |
| **3 A Schottky Diode (SS34 / 1N5822)** | 1 | 3 | Reverse-polarity protection (3 A rated; replaces 1N5819) |
| **5.6 V TVS Diode (1.5KE5.6A / SMBJ5.0A)**| 1 | 2 | Overvoltage clamp across 5V rail against buck regulator failure |
| **2 A Fuse (Glass/PPTC)** | 1 | 2 | Overcurrent protection against motor stalls |
| **Tactile Pushbutton** | 1 | 2 | PA4 user interface (Short = Mode Toggle, Long = Calibrate) |
| **3.3 kΩ – 4.7 kΩ Resistors** | 4 | 10 | Universal compromise LDR dividers (works indoor & outdoor) |
| **10 kΩ Resistors** | 2 | 10 | External servo signal pull-downs (PA6 & PA7) |
| **1000 µF 16V Electrolytic Cap** | 1 | 2 | Low-ESR inrush reservoir across 5V servo rail |

---

## 📌 Microcontroller Pin Mapping

| Pin Name | Function | Connected Component | Electrical Notes |
|:---|:---|:---|:---|
| **`PA0`** | `ADC1_IN0` | Top-Left LDR Divider | **0 to 3.3 V strictly.** $10\text{ nF}$ filter cap to GND |
| **`PA1`** | `ADC1_IN1` | Top-Right LDR Divider | **0 to 3.3 V strictly.** $10\text{ nF}$ filter cap to GND |
| **`PA2`** | `ADC1_IN2` | Bottom-Left LDR Divider | **0 to 3.3 V strictly.** $10\text{ nF}$ filter cap to GND |
| **`PA3`** | `ADC1_IN3` | Bottom-Right LDR Divider | **0 to 3.3 V strictly.** $10\text{ nF}$ filter cap to GND |
| **`PA4`** | `GPIO_Input` | Mode / Calibrate Button | Active-low pushbutton to GND (Internal pull-up) |
| **`PA6`** | `TIM3_CH1` | Pan (Azimuth) Servo PWM | External $10\text{ k}\Omega$ pull-down to GND required |
| **`PA7`** | `TIM3_CH2` | Tilt (Elevation) Servo PWM | External $10\text{ k}\Omega$ pull-down to GND required |
| **`PB6`** | `I2C1_SCL` | Shared I²C SCL Bus Line | OLED (`0x3C`) + INA219s (`0x40`, `0x41`, `0x44`) |
| **`PB7`** | `I2C1_SDA` | Shared I²C SDA Bus Line | Shared 3.3 V I²C data line |
| **`PC13`** | `GPIO_Output` | Status LED | Active-low onboard LED |
| **`5V`**   | Power In | LM2596 Output (5.00V) | Must be trimmed to **$5.00\text{ V} \pm 0.05\text{ V}$** before connecting |
| **`3.3V`** | Power Out | Sensor & Display Rail | Sourced from onboard LDO (≤150 mA max) |

---

## 🖼️ Component Hardware Gallery

| Component | Hardware Photograph | Connection Summary |
|:---|:---:|:---|
| **STM32 Blue Pill** | ![Blue Pill](images/blue_pill_pinout.jpg) | Core MCU. Powered via 5V pin from LM2596. SWD debugging uses SWDIO, SWCLK, GND only. |
| **INA219 Power Module** | ![INA219 Module](images/ina219_module.jpg) | High-side current/voltage monitor. Address configured via A0/A1 solder bridges. |
| **MG90S Servo** | ![MG90S Servo](images/mg90s_servo.jpg) | Metal-gear micro servo. Orange = Signal (PA6/PA7), Red = 5V Rail, Brown = GND. |
| **LM2596 Buck Converter** | ![LM2596 Converter](images/lm2596_module.jpg) | Multi-turn trimmer adjusts 7-12V input down to regulated 5.00 V system power. |
| **SSD1306 OLED** | ![SSD1306 Display](images/ssd1306_oled.jpg) | 128×64 pixel display on I²C address `0x3C`. Powered from 3.3 V rail. |
| **Protection Hardware** | ![Protection Components](images/protection_components.jpg) | 1N5819 Schottky diode + 2A glass fuse + DC barrel jack socket. |

---

## 🔌 Detailed Wiring Schematics

### 1. LDR Sensor Voltage Divider
![LDR Divider Schematic](images/ldr_divider_circuit.jpg)

### 2. Multi-Drop I²C Telemetry Bus
![I2C Bus Wiring](images/i2c_bus_wiring.jpg)

### 3. Solar Panel Power Measurement Path
![INA219 Panel Wiring](images/ina219_panel_wiring.jpg)

---

## 📚 Complete Project Documentation

This repository contains two exhaustive engineering manuals for building and debugging the system:

1. 📖 **[Dual-Axis Solar Tracker: Complete Hardware Engineering Manual & Implementation Guide](solar_tracker_hardware_manual.md)**
   * System Block Architecture & Mathematical Models
   * Optical Shadow Baffle Geometry & Photometric Curves
   * Mechanical Center-of-Gravity (CG) Balancing & Bearing Design
   * Circuit Wiring Rules, Overvoltage TVS Protection & Resistor Compromise
   * Solar Harvesting Experimental Benchmarking Protocol

2. 🔍 **[Hardware Audit & Wiring Verification Report](hardware_audit_report.md)**
   * Audit of 33 Electrical, Firmware, and Mechanical Points
   * Clarifications on INA219 Register Defaults, Holding Torque & Integer Promotion
   * 51-Point Component-by-Component Bench Assembly & Connection Verification Checklist

3. 💻 **[Production Firmware Source Code](Core/)**
   * Production-grade, modular STM32 HAL C codebase (`Core/Inc`, `Core/Src`)
   * Fully implemented SSD1306 OLED font and real-time dashboard rendering engine
   * Hardware DWT cycle counter microsecond delay for 20 ms / 50 Hz mains-hum rejection
   * Boot-time I²C scanner, staggered servo soft-start, runaway protection, and dawn acquisition sweep

---

## ⚡ Quick Start & Pre-Power Commissioning

1. **Continuity & Short Check:** With power OFF, verify with a multimeter that `5V`, `3.3V`, and `GND` are completely isolated from each other.
2. **Trim Power Output:** Connect 9 V supply to LM2596 input with nothing attached to output. Measure with DMM across `OUT+`/`OUT-`. Adjust trimmer screw until reading exactly **$5.00\text{ V} \pm 0.05\text{ V}$**.
3. **Flash MCU:** Connect ST-Link V2 using `SWDIO`, `SWCLK`, `GND`, and `NRST`. *Do NOT connect the ST-Link 3.3V wire when powered via LM2596.* Flash the firmware from `Core/`.
4. **Set I²C Addresses:**
   * INA219 #1 (Tracked Panel): Leave A0 & A1 open (`0x40`)
   * INA219 #2 (System Power): Bridge **A0** (`0x41`)
   * INA219 #3 (Fixed Panel): Bridge **A1** (`0x44`)
5. **Verify I²C Bus Scan:** Confirm all 4 devices (`0x3C`, `0x40`, `0x41`, `0x44`) are detected on startup.
6. **Connect Servos Last:** Power servos via a current-limited bench supply set to 1 A initially to verify smooth staggered initialization.
7. **Calibrate Sensors:** Hold button on `PA4` for >2s under uniform lighting to store relative LDR offset calibration factors.

---

## 👤 Author & License

* **Developer:** Muhammed Shifan ([@Bit-wisely](https://github.com/Bit-wisely))
* **Repository:** [https://github.com/Bit-wisely/helion.git](https://github.com/Bit-wisely/helion.git)
* **License:** MIT License — Open source for hardware developers and research applications.
