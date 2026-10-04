# Dual-Axis Solar Tracker: Complete Hardware Engineering Manual & Implementation Guide

---

## 1. System Architecture & High-Level Block Diagram

The dual-axis solar tracker is an autonomous electro-mechanical system designed to align a photovoltaic (PV) panel perpendicular to incoming solar irradiance. In this hardware design:
* The **6 V, 3 W solar panel is treated as a Device Under Test (DUT)** and a measured energy source; it does **not** power the system electronics.
* The system is powered by an external **7–12 V DC power source** stepped down to a regulated **5.00 V rail** for servo actuators and logic input.
* Telemetry measures **gross generated power**, **baseline fixed-panel generation**, and **parasitic system power consumption** simultaneously.

```mermaid
flowchart TD
    subgraph Power_Supply ["Power Supply Subsystem (High-Side Switched)"]
        DC_IN["7-12V DC Barrel Jack"] --> SW["Master Slide Switch (Positive Leg)"]
        SW --> FUSE["2A Fuse"]
        FUSE --> DIODE["3A Schottky Diode (SS34 / 1N5822)"]
        DIODE --> INA_SYS["INA219 #2 (0x41) System Input Sensor"]
        INA_SYS --> LM2596["LM2596 Buck Converter (Outputs 5.00V)"]
        LM2596 --> TVS["5.6V TVS Overvoltage Clamp (1.5KE5.6A)"]
        TVS --> CAP_BANK["1000µF Low-ESR + 100nF MLCC"]
        CAP_BANK --> SERVO_RAIL["5.0V Dedicated Servo Power Rail"]
        CAP_BANK --> ISO_DIODE["Schottky Diode (Back-feed Isolation)"]
        ISO_DIODE --> BP_5V["Blue Pill 5V Pin -> Onboard 3.3V LDO"]
    end

    subgraph Controller ["STM32F103C8T6 Microcontroller Subsystem"]
        BP_5V --> BP_3V3["3.3V Rail (Powers ADC, I2C, LDRs, OLED)"]
        BP_3V3 --> ADC_INPUTS["ADC1 (PA0-PA3)"]
        BP_3V3 --> I2C_BUS["I2C1 Bus (PB6 SCL, PB7 SDA)"]
        TIM3["TIM3 PWM (PA6, PA7)"] --> PULLDOWN["10kΩ Pull-downs to GND"]
        PULLDOWN --> SERVO_SIG["MG90S Signal Lines"]
        BTN["Tactile Pushbutton (PA4)"] --> Controller
    end

    subgraph Sensors_Actuators ["Sensors & Mechanical Actuators"]
        LDR_HEAD["4x GL5528 LDRs + 35mm Matte Baffle"] --> ADC_INPUTS
        SERVO_RAIL --> SERVO_PWR["MG90S Pan & Tilt Servos"]
        SERVO_SIG --> SERVO_PWR
        SERVO_PWR --> TURNTABLE["Turntable / Bearing & CG-Balanced Mount"]
    end

    subgraph Telemetry ["Telemetry & Power Measurement Subsystem"]
        PV_TRK["Tracked 6V 3W PV Panel"] --> INA_TRK["INA219 #1 (0x40)"]
        INA_TRK --> LOAD_TRK["10-12Ω 5W Matched Resistor"]
        
        PV_FIX["Fixed Reference 6V 3W PV Panel"] --> INA_FIX["INA219 #3 (0x44)"]
        INA_FIX --> LOAD_FIX["10-12Ω 5W Matched Resistor"]

        I2C_BUS --> INA_TRK
        I2C_BUS --> INA_SYS
        I2C_BUS --> INA_FIX
        I2C_BUS --> OLED["0.96 inch SSD1306 OLED (0x3C)"]
    end
```

---

## 2. Complete Bill of Materials (BOM) & Sourcing Guide

Below is the verified parts list for building the dual-axis tracker with side-by-side fixed-panel benchmarking. "Suggested Buy" quantities provide spare tolerances for bench testing, pin soldering, resistor matching, and clone troubleshooting.

| Item Description | Build Qty | Suggested Buy | Engineering Purpose & Selection Notes |
| :--- | :---: | :---: | :--- |
| **STM32F103C8T6 Blue Pill** | 1 | 2 | Main controller board (+ pin headers if unsoldered). Buy 2 in case of clone CPUTAPID issues or flash failure. |
| **ST-Link V2 Debug Probe** | 1 | 1 | Hardware SWD programmer (connect `SWDIO`, `SWCLK`, `GND`, and **`NRST`**). |
| **TowerPro MG90S Micro Servo** | 2 | 3 | Metal-gear actuators ($2.2\text{ kg}\cdot\text{cm}$ torque) for Azimuth & Elevation. Keep 1 spare. |
| **GL5528 CdS Photoresistor (LDR)** | 4 | 6 | Light differential quadrant sensors. Extras allow tolerance matching. |
| **INA219 I²C Power Sensor Module** | 3 | 4 | Must have solder pads for `A0`/`A1` address bridging (`0x40`, `0x41`, `0x44`). |
| **SSD1306 0.96" I²C OLED Display** | 1 | 1 | Real-time telemetry dashboard (128×64 resolution, default address `0x3C`). |
| **6 V 3 W Polycrystalline Solar Panel** | 2 | 2 | Matched pair from the same factory batch (1× Tracked DUT, 1× Fixed Reference). |
| **10–12 Ω 5 W Wirewound Resistor** | 2 | 4 | MPP test load resistors. Measure with DMM and match within $\pm 0.1\ \Omega$. Aluminum or cement (heatsinked). |
| **LM2596 DC-DC Buck Converter Module** | 1 | 2 | High-efficiency step-down to regulated 5.00 V. Buy spare in case of pot misadjustment. |
| **9 V 2 A DC Power Adapter** | 1 | 1 | 5.5×2.1 mm center-positive wall-plug supply for the entire system. |
| **DC Barrel Jack (Female Socket)** | 1 | 1 | 5.5×2.1 mm chassis or breadboard-friendly socket. |
| **SPST Slide Switch** | 1 | 1 | Master power toggle switch wired in series with barrel jack positive line. |
| **3 A Schottky Diode (SS34 / 1N5822)** | 1 | 3 | Reverse-polarity protection (3 A rated; replaces 1N5819 to safely exceed 2 A fuse rating). |
| **5.6 V TVS Diode (1.5KE5.6A / SMBJ5.0A)**| 1 | 2 | Overvoltage clamp across 5V rail to protect electronics against buck regulator failure. |
| **2 A Fuse (Glass or Resettable PPTC)** | 1 | 2 | Overcurrent protection against motor stalls or wiring shorts. |
| **Tactile Pushbutton (6×6 mm)** | 1 | 2 | User interface on `PA4`: Short press = Mode toggle; Long press (>2s) = LDR calibration. |
| **3.3 kΩ – 4.7 kΩ Metal Film Resistors** | 4 | 10 | Universal compromise LDR dividers (operates linearly indoor & outdoor without swapping). |
| **10 kΩ 1/4W Metal Film Resistors** | 2 | 10 | Servo PWM floating pull-downs to GND (2×). |
| **4.7 kΩ 1/4W Metal Film Resistors** | 2 | 4 | Optional I²C bus pull-ups (fit only if bus modules lack onboard pull-ups). |
| **10 nF Ceramic Capacitors (0.01 µF)** | 4 | 10 | High-frequency noise suppression capacitors on ADC input pins `PA0`–`PA3`. |
| **100 nF Ceramic Capacitors (0.1 µF)** | 2 | 10 | Logic decoupling on Blue Pill and high-frequency bypass across servo power rail. |
| **1000 µF 16 V Electrolytic Capacitor** | 1 | 2 | Low-ESR inrush reservoir placed directly across the 5.0 V servo power rail. |
| **Pan-Tilt Micro-Servo Bracket Kit** | 1 | 1 | 2-axis mechanical nylon or aluminum gimbal bracket for micro servos. |
| **608 Skate Ball Bearing (8×22×7 mm)** | 1 | 2 | Turntable axial support bearing to take pan axis weight off servo spline. |
| **M8 Bolt/Rod, Nuts & Printed Coupler** | 1 set | 1 set | Mechanical shafting hardware (or use a 3–4 inch metal Lazy Susan ring bearing). |
| **Rigid Base Plate (approx. 15×15 cm)** | 1 | 1 | Heavy acrylic, wood, or aluminum mounting platform for system stability. |
| **Fixed-Panel Reference Mount** | 1 | 1 | Rigid static bracket matching baseline deployment angle (e.g. 15–30° south-facing). |
| **Matte-Black Foamboard / 3D Print** | 1 | 1 | 35–40 mm tall cross divider creating directional shadows across the 4 LDRs. |
| **Perfboard 7×9 cm + Half Breadboard** | 1 + 1 | 2 + 1 | Perfboard for high-current 5V/GND power star distribution; breadboard for MCU logic. |
| **Screw Terminals or Wago 221 Connectors** | 1 pack | 1 pack | High-reliability, low-resistance junctions for star ground and 5 V distribution. |
| **Dupont Jumper Wires (M-F, F-F, M-M)** | ~40 each | 1 pack each | High-quality 20 cm jumper wires for modular interconnects. |
| **Bench Hardware Kit** | 1 set | 1 set | 22 AWG hookup wire, heat-shrink tubing, M3 standoffs/nuts/bolts, cable ties, hot glue. |

---

## 3. Electrical Protection & Power Distribution

### 3.1 Reverse-Polarity, Overvoltage & Overcurrent Protection
Connecting an incorrect center-negative power brick or experiencing a buck converter failure will destroy sensitive components if unmitigated:
* **High-Side Power Switch:** Wire the SPST power switch exclusively in series with the positive line coming from the DC barrel jack. Never switch ground, as sneaky return paths via ST-Link or chassis grounds will keep the board energized.
* **3 A Schottky Diode (SS34 / 1N5822):** Placed in series on the positive rail after the fuse. The original 1N5819 is only rated for 1.0 A, which is inadequate when two servos stall ($\approx 1.2–1.4\text{ A}$ total) and leaves the diode vulnerable while the 2 A fuse remains intact. A 3 A part ensures the diode easily survives full stall loads.
* **5.6 V Overvoltage TVS Diode (1.5KE5.6A):** Placed directly across the LM2596 5 V output rail. If the buck regulator's internal pass switch shorts collector-to-emitter, the TVS diode clamps the output to $\le 5.6\text{ V}$ and draws enough surge current to immediately blow the 2 A fuse, saving the servos and Blue Pill.
* **In-Line 2 A Fuse:** A fast-acting 2 A glass or PPTC resettable fuse wired on the input positive rail.

### 3.2 LM2596 Pre-Flight Bench Trimming
> [!CAUTION]
> Multi-turn cermet potentiometers on generic LM2596 modules ship in an arbitrary state (typically resting at $V_{\text{out}} \approx V_{\text{in}} - 1.5\text{ V}$). If you connect $12\text{ V}$ to the input, the output may deliver $10.5\text{ V}$, instantly frying the Blue Pill's low-dropout regulator and all servo electronics.

1. Disconnect all microcontrollers, sensors, and servos from the LM2596 output.
2. Connect the 9 V or 12 V DC adapter to the input.
3. Attach digital multimeter (DMM) test leads securely across `OUT+` and `OUT-`.
4. Turn the brass screw while observing the meter until it reads exactly **$5.00\text{ V} \pm 0.05\text{ V}$**. Do not guess the rotation direction; follow the live voltage reading on your meter.
5. Seal the screw with a drop of silicone adhesive, hot glue, or enamel to lock the setpoint against transport vibrations.

### 3.3 Servo Inrush Suppression & Breadboard Hazards
* **The Breadboard Hazard:** Standard solderless breadboard spring clips possess contact resistances of $0.1\text{–}0.3\ \Omega$. Under a two-servo stall or start-up surge of $1.3\text{ A}$, the breadboard clips drop $0.2\text{–}0.4\text{ V}$, inducing ground bounce and rail sag that resets the microcontroller. **Use a soldered perfboard rail, terminal strip, or heavy gauge ($18\text{–}20\text{ AWG}$) stranded wire for the 5 V and GND lines feeding the servos.**
* **Decoupling Capacitor Bank:** Solder a **$1000\ \mu\text{F}$ 16V low-ESR electrolytic capacitor** in parallel with a **$100\text{ nF}$ multi-layer ceramic capacitor (MLCC)** directly across the servo power distribution rail. The electrolytic capacitor supplies instantaneous peak charge during motor commutations, while the ceramic capacitor shunts high-frequency RF ringing.

### 3.4 Programmer & Interface Isolation Rules
* **ST-Link 3.3V Contention & NRST:** When flashing or debugging while the system is powered from the barrel jack, **connect `SWDIO`, `SWCLK`, `GND`, and `NRST`**. Disconnect the 3.3 V wire from the ST-Link header. Connecting both will force two active regulators to fight over the 3.3 V rail. Connecting `NRST` guarantees the programmer can halt the core even if the MCU enters sleep or disables debug pins.
* **Micro-USB Hazard & Schottky Isolation:** **Never plug in the Blue Pill's onboard micro-USB port while 5 V is supplied to the `5V` pin**, or install a small Schottky diode in series between the LM2596 5 V rail and the Blue Pill's 5 V header pin to prevent dangerous back-feeding.

---

## 4. Physical Microcontroller Pinout & Wiring Interconnect

The STM32F103C8T6 offers multiplexed peripherals. The table below represents the exact physical pin mapping optimized to prevent bus contention, avoid ADC overvoltage, and protect debug interfaces.

| Pin Name | Physical Pin | Function / Alternate Mode | Connected Peripheral | Electrical Constraints / Notes |
| :--- | :--- | :--- | :--- | :--- |
| **`PA0`** | Pin 10 | `ADC1_IN0` | Top-Left LDR Divider | **0 to 3.3 V strictly.** $10\text{ nF}$ filter cap to GND. |
| **`PA1`** | Pin 11 | `ADC1_IN1` | Top-Right LDR Divider | **0 to 3.3 V strictly.** $10\text{ nF}$ filter cap to GND. |
| **`PA2`** | Pin 12 | `ADC1_IN2` | Bottom-Left LDR Divider | **0 to 3.3 V strictly.** $10\text{ nF}$ filter cap to GND. |
| **`PA3`** | Pin 13 | `ADC1_IN3` | Bottom-Right LDR Divider| **0 to 3.3 V strictly.** $10\text{ nF}$ filter cap to GND. |
| **`PA4`** | Pin 14 | `GPIO_Input` (Pull-Up) | Mode / Calibration Button | Active-low pushbutton to GND. Internal pull-up. |
| **`PA6`** | Pin 16 | `TIM3_CH1` (PWM 50 Hz) | Pan (Azimuth) Servo Signal | External **$10\text{ k}\Omega$ pull-down to GND** required. |
| **`PA7`** | Pin 17 | `TIM3_CH2` (PWM 50 Hz) | Tilt (Elevation) Servo Signal| External **$10\text{ k}\Omega$ pull-down to GND** required. |
| **`PB6`** | Pin 42 | `I2C1_SCL` (100 kHz) | SCL Line (OLED + 3x INA219)| Shared I²C clock. Powered from 3.3 V rail. |
| **`PB7`** | Pin 43 | `I2C1_SDA` (100 kHz) | SDA Line (OLED + 3x INA219)| Shared I²C data. Powered from 3.3 V rail. |
| **`PC13`** | Pin 2 | `GPIO_Output` | Onboard Status LED | Active-low. Indicates calibration/status states. |
| **`PA13`** | Pin 34 | `SYS_JTMS-SWDIO` | ST-Link SWDIO | Dedicated hardware debug. Do not reuse as GPIO. |
| **`PA14`** | Pin 37 | `SYS_JTCK-SWCLK` | ST-Link SWCLK | Dedicated hardware debug. Do not reuse as GPIO. |
| **`5V`**   | Header | Main DC In | Output from LM2596 | Must be pre-trimmed to $5.00\text{ V}$. |
| **`GND`**  | Header | Common System Ground | Central Ground Star Point | Common return path for logic, servos, and loads. |
| **`3.3V`** | Header | Regulated 3.3V Output | Power for LDRs, OLED, INA219s | Sourced from Blue Pill onboard LDO (150 mA max). |

---

## 5. Optical Sensor Head & Photometrics

```
               [Top-Left LDR]       |       [Top-Right LDR]
                                    |
             -----------------------+-----------------------  <-- 35 mm Tall Matte-Black
                                    |                             Cross-Baffle
              [Bottom-Left LDR]     |     [Bottom-Right LDR]
```

### 5.1 Photometric Curve of GL5528
The cadmium sulfide (CdS) photoresistor exhibits an inverse logarithmic resistance profile:
* **Dark Resistance:** $R_D \ge 1.0\text{ M}\Omega$
* **10 Lux (Dusk/Room light):** $R_{10} \approx 10\text{–}20\text{ k}\Omega$
* **1000 Lux (Bright overcast):** $R_{1000} \approx 1.0\text{ k}\Omega$
* **50,000–100,000 Lux (Direct Tropical Sun):** $R_{\text{sat}} \approx 80\text{–}150\ \Omega$

### 5.2 Divider Resistor Selection ($R_{\text{fixed}}$)
The LDR is wired between the $3.3\text{ V}$ rail and the ADC pin, with a fixed resistor $R_{\text{fixed}}$ wired from the ADC pin to $\text{GND}$:
$$V_{\text{ADC}} = 3.3\text{ V} \times \frac{R_{\text{fixed}}}{R_{\text{LDR}} + R_{\text{fixed}}}$$

* **Indoor Bench Demonstration (100–500 lux):** Use **$R_{\text{fixed}} = 10\text{ k}\Omega$**. At $300\text{ lux}$, $R_{\text{LDR}} \approx 3\text{ k}\Omega$, centering $V_{\text{ADC}}$ at $3.3 \times \frac{10}{13} \approx 2.5\text{ V}$, providing high sensitivity.
* **Outdoor Solar Testing (50,000–100,000 lux):** Use **$R_{\text{fixed}} = 1.5\text{ k}\Omega$**. If a $10\text{ k}\Omega$ resistor is used under direct sun ($R_{\text{LDR}} \approx 100\ \Omega$), the output saturates near $3.3 \times \frac{10000}{10100} \approx 3.26\text{ V}$, collapsing differential signals into quantization noise. With $R_{\text{fixed}} = 1.5\text{ k}\Omega$:
  $$V_{\text{ADC}} = 3.3 \times \frac{1500}{100 + 1500} \approx 3.09\text{ V}$$
  A shadow casting on one sensor increases its resistance to $1.0\text{ k}\Omega$:
  $$V_{\text{ADC, shaded}} = 3.3 \times \frac{1500}{1000 + 1500} \approx 1.98\text{ V} \quad (\Delta V = 1.11\text{ V!})$$
* **Tip:** Solder female pin-header sockets on your sensor breakout board so you can swap between $10\text{ k}\Omega$ and $1.5\text{ k}\Omega$ resistors in seconds.

### 5.3 Optical Shadow Baffle Geometry
For a tracking offset angle $\theta$, the length of the shadow cast by a baffle of height $H$ is:
$$L_{\text{shadow}} = H \times \tan(\theta)$$
* With a short $20\text{ mm}$ baffle at a $2^\circ$ deviation ($\tan(2^\circ) \approx 0.0349$):
  $$L_{\text{shadow}} = 20\text{ mm} \times 0.0349 = 0.70\text{ mm}$$
  Since an LDR's clear epoxy dome is $5.0\text{ mm}$ in diameter, a $0.70\text{ mm}$ shadow edge only covers $14\%$ of the sensitive area, which is easily obscured by component tolerance.
* **Selected Baffle Height:** Fabricate a **$35\text{–}40\text{ mm}$ tall baffle** printed in matte-black PLA or coated with non-reflective matte paint. At $2^\circ$, $L_{\text{shadow}} = 35\text{ mm} \times 0.0349 \approx 1.22\text{ mm}$, providing an unmistakable differential signal.

### 5.4 Normalized Differential Error Formulation
To make tracking immune to clouds, atmospheric haze, and absolute brightness changes, normalize the spatial delta by the total quadrant irradiance:
$$\text{Total Light} = V_{\text{TL}} + V_{\text{TR}} + V_{\text{BL}} + V_{\text{BR}}$$
$$\text{Error}_{\text{Azimuth}} = \frac{(V_{\text{TL}} + V_{\text{BL}}) - (V_{\text{TR}} + V_{\text{BR}})}{\text{Total Light} + 1} \times 1000$$
$$\text{Error}_{\text{Elevation}} = \frac{(V_{\text{TL}} + V_{\text{TR}}) - (V_{\text{BL}} + V_{\text{BR}})}{\text{Total Light} + 1} \times 1000$$
The output ranges from $-1000$ to $+1000$ ($\pm 100\%$). A deadband of $40$ corresponds to a steady $4.0\%$ relative difference threshold regardless of whether the system operates under a $500\text{ lux}$ room light or a $90,000\text{ lux}$ clear sky.

---

## 6. Mechanical Design, Bearing Integration & Thermal Management

```
       [Solar Panel]
             |
    +--------+--------+  <-- Elevation Pivot (Center of Gravity Balanced)
    |   MG90S Tilt    |
    +--------+--------+
             |
      [Turntable Arm]
             |
    ===================  <-- 3" to 4" Metal Lazy Susan Turntable Bearing
             |               (Carries all downward structural weight)
    +--------+--------+
    |   MG90S Pan     |  <-- Only delivers rotational steering torque
    +--------+--------+
             |
       [Base Plate]
```

### 6.1 Center-of-Gravity (CG) Elevation Balance
A 3 W glass/aluminum polycrystalline solar panel weighs approximately $180\text{–}250\text{ g}$. 
* If mounted cantilevered on top of the tilt horn, the center of mass sits $30\text{–}50\text{ mm}$ above the pivot.
* Gravitational torque on the servo shaft:
  $$\tau = m \cdot g \cdot d = 0.22\text{ kg} \times 9.81\text{ m/s}^2 \times 0.04\text{ m} \approx 0.086\text{ N}\cdot\text{m} = 0.88\text{ kg}\cdot\text{cm}$$
* The MG90S has an operating torque rating of $1.8\text{–}2.2\text{ kg}\cdot\text{cm}$. A static load of $0.88\text{ kg}\cdot\text{cm}$ consumes **$40\text{–}50\%$ of the motor's total torque capacity just holding the panel still**, generating continuous heat and gear deflection.
* **Design Rule:** The horizontal tilt pivot pins must pass directly through the physical center of gravity of the combined panel, bracket, and sensor head. When unpowered, the panel must rest stably at any tilt angle without falling.

### 6.2 Pan Axis Bearing Architecture
Never allow the weight of the elevation servo, brackets, and solar panel to press directly down onto the 4.8 mm brass spline of the azimuth servo. The internal plastic potentiometer wiper inside the servo will crush and fail.
* **Turntable Bearing:** Bolt a **3-inch or 4-inch square steel/aluminum Lazy Susan turntable bearing** (or a 608 skate bearing press-fit with an M8 bolt coupler) between the base plate and the rotating pan deck. 
* The azimuth servo is installed underneath, driving the center axis through an Oldham-style flexible slot or linkage that transmits only rotational torque ($F_t$), leaving axial load ($F_z$) supported entirely by the steel ball race.

### 6.3 Thermal Management of Matched Load Resistors
At peak irradiance, each 6 V 3 W panel delivers $I_{\text{mp}} \approx 0.52\text{ A}$ into its $10\ \Omega$ load resistor:
$$P_{\text{dissipated}} = I^2 R = (0.52\text{ A})^2 \times 10\ \Omega \approx 2.70\text{ W}$$
Continuous dissipation of $2.7\text{ W}$ in an uncooled environment will heat a standard $5\text{ W}$ cement or aluminum resistor to **$75\text{–}95^\circ\text{C}$**.
* **Do not mount the load resistors on 3D-printed parts (PLA softens at $55\text{–}60^\circ\text{C}$).**
* Bolt aluminum-housed wirewound resistors to an external aluminum plate or mount ceramic cement resistors on elevated metal standoffs on the base plate.
* Keep resistors isolated from the LDR sensor head to prevent thermal air currents from causing optical refraction or temperature-induced sensor drift.

---

## 7. Multi-Drop I²C Power Telemetry Architecture

All telemetry devices communicate across a shared two-wire interface:
* `PB6` $\rightarrow$ `I2C1_SCL`
* `PB7` $\rightarrow$ `I2C1_SDA`
* Powered strictly from the **3.3 V rail**.

```
           +-----------------------------------------------------------+
           |                     STM32 Blue Pill                       |
           |               I2C1: PB6 (SCL), PB7 (SDA)                  |
           +-------------+---------------------+-----------------------+
                         |                     |                       |
              [0x40 - Default]          [0x41 - Bridge A0]      [0x44 - Bridge A1]
              INA219 #1 (Tracked)       INA219 #2 (System)      INA219 #3 (Fixed Ref)
              +-----------------+       +-----------------+     +-----------------+
              | Shunt: Panel #1 |       | Shunt: Barrel   |     | Shunt: Panel #2 |
              | Load: 10Ω 5W    |       | Rail to LM2596  |     | Load: 10Ω 5W    |
              +-----------------+       +-----------------+     +-----------------+
```

### 7.1 Hardware Address Configuration Table

| Target Device | Function | I²C Address (7-bit) | Address Jumpers (Solder Pads) | Measurement Role |
| :--- | :--- | :--- | :--- | :--- |
| **INA219 #1** | Tracked PV Generation | `0x40` | Leave `A0` and `A1` open | Measures $V_{\text{pv,trk}}$, $I_{\text{pv,trk}}$, and calculates $P_{\text{gross}}$. |
| **INA219 #2** | System Power Consumption | `0x41` | Solder bridge across **`A0`** | Measures input barrel voltage ($V_{\text{in}}$) and total operating current ($I_{\text{sys}}$). |
| **INA219 #3** | Baseline Fixed PV | `0x44` | Solder bridge across **`A1`** | Measures $V_{\text{pv,fix}}$, $I_{\text{pv,fix}}$ on identical static panel. |
| **SSD1306** | Real-Time User Dashboard | `0x3C` | Factory default setting | Displays comparative mW, mAh, tracking error, and system status. |

> [!IMPORTANT]
> **Star Grounding & Return Current Rule:**
> The negative leads of both solar panels, the returns of both $10\ \Omega$ load resistors, all three INA219 GND pins, the LM2596 `IN−`/`OUT−`, the SSD1306 OLED GND, and the Blue Pill GND pin **must all terminate at the common star ground point on perfboard**.
> The INA219 measures bus voltage relative to its own GND pin ($V_{\text{IN-}} - V_{\text{GND}}$). If the solar panel return or sensor grounds float relative to each other, the bus voltage reading will be invalid and fluctuating.

---

## 8. Production STM32 Firmware Implementation

> [!TIP]
> **Modular Source Tree Available:**
> The complete, buildable, modular firmware codebase is organized in the [`Core/`](Core/) folder of this repository:
> - **[`Core/Src/main.c`](Core/Src/main.c)**: Orchestrates non-blocking UI, boot-time I²C scanner, staggered servo startup, and telemetry loops.
> - **[`Core/Src/stm32f1xx_hal_msp.c`](Core/Src/stm32f1xx_hal_msp.c)**: Full MSP callbacks for ADC1, TIM3, and I2C1 clocks and GPIO alternate-function setup.
> - **[`Core/Src/ina219.c`](Core/Src/ina219.c)**: Driver using correct `0x399F` configuration and I²C bus lockup recovery.
> - **[`Core/Src/ssd1306.c`](Core/Src/ssd1306.c) & [`Core/Src/ssd1306_fonts.c`](Core/Src/ssd1306_fonts.c)**: Complete text and numeric dashboard rendering engine.
> - **[`Core/Src/tracker.c`](Core/Src/tracker.c)**: DWT 20 ms hum filter, runaway detection, clamped calibration, and dawn scan.
> 
> The monolithic listing below serves as an architectural code reference.

```c
/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Production Dual-Axis Solar Tracker Controller
  *                   Target: STM32F103C8T6 (Blue Pill)
  *                   Clock: 72 MHz (HSE 8 MHz + PLL)
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

/* Private typedef -----------------------------------------------------------*/
typedef enum {
    MODE_DEMO = 0,   // Rapid 300 ms response for indoor flashlight demonstration
    MODE_FIELD = 1   // 30 s gated movement for high-efficiency outdoor field use
} TrackerMode;

typedef struct {
    float v_tracked; // Volts
    float i_tracked; // mA
    float p_tracked; // mW
    float v_fixed;
    float i_fixed;
    float p_fixed;
    float p_system;  // mW
    float mah_tracked;
    float mah_fixed;
} SystemTelemetry;

/* Private define ------------------------------------------------------------*/
#define PAN_DIR                 (+1)    // Flip to -1 if azimuth runs away from light
#define TILT_DIR                (+1)    // Flip to -1 if elevation runs away from light

#define PAN_MIN_PULSE           600     // Microsecond limits preventing mechanical bind
#define PAN_MAX_PULSE           2400
#define TILT_MIN_PULSE          800
#define TILT_MAX_PULSE          2200

#define DEADBAND_PERCENT        40      // 40 = 4.0% normalized error threshold
#define STEP_DEG_TICKS          15      // ~1.5 degree servo step per actuation

#define NIGHT_ENTER_LUX         300     // Below this sum -> Enter night park
#define NIGHT_EXIT_LUX          550     // Above this sum -> Resume tracking

#define INA219_ADDR_TRACKED     (0x40 << 1)
#define INA219_ADDR_SYSTEM      (0x41 << 1)
#define INA219_ADDR_FIXED       (0x44 << 1)
#define OLED_ADDR               (0x3C << 1)

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
I2C_HandleTypeDef hi2c1;
TIM_HandleTypeDef htim3;

uint16_t pan_pulse  = 1500;  // Center position (1500 us)
uint16_t tilt_pulse = 1500;

float cal_factors[4] = {1.0f, 1.0f, 1.0f, 1.0f}; // Quadrant offset multipliers
bool is_parked = false;
TrackerMode current_mode = MODE_DEMO;
SystemTelemetry telemetry = {0};

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);
static void MX_TIM3_Init(void);

void INA219_Init(uint16_t addr);
void INA219_ReadData(uint16_t addr, float* voltage, float* current_ma, float* power_mw);
void OLED_Init(void);
void OLED_Clear(void);
void OLED_WriteCommand(uint8_t cmd);
void Calibrate_Sensor_Offsets(void);

/* Helper: Pulse Clamp */
uint16_t Clamp(uint16_t val, uint16_t min_val, uint16_t max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

/* Helper: 20 ms Mains-Hum Rejection ADC Sampler */
uint16_t Read_Filtered_Channel(uint32_t channel) {
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_71CYCLES_5;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);

    // Discard initial conversion after channel multiplexing switch
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 2);
    (void)HAL_ADC_GetValue(&hadc1);

    // Accumulate 40 samples over exactly 20 ms (500 us intervals)
    uint32_t sum = 0;
    for (int i = 0; i < 40; i++) {
        HAL_ADC_Start(&hadc1);
        HAL_ADC_PollForConversion(&hadc1, 2);
        sum += HAL_ADC_GetValue(&hadc1);
        for(volatile int d = 0; d < 700; d++); // ~500 us spin-wait at 72 MHz
    }
    return (uint16_t)(sum / 40);
}

/* Core Algorithm: Normalized Dual-Axis Tracking */
void Execute_Tracking_Step(void) {
    float raw_tl = (float)Read_Filtered_Channel(ADC_CHANNEL_0);
    float raw_tr = (float)Read_Filtered_Channel(ADC_CHANNEL_1);
    float raw_bl = (float)Read_Filtered_Channel(ADC_CHANNEL_2);
    float raw_br = (float)Read_Filtered_Channel(ADC_CHANNEL_3);

    // Apply startup offset calibration factors
    float tl = raw_tl * cal_factors[0];
    float tr = raw_tr * cal_factors[1];
    float bl = raw_bl * cal_factors[2];
    float br = raw_br * cal_factors[3];

    float total_light = tl + tr + bl + br;

    // Night-Time Parking with Hysteresis
    if (total_light < NIGHT_ENTER_LUX) {
        if (!is_parked) {
            HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
            HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);

            pan_pulse = 2200;  // Pre-position facing East
            tilt_pulse = 1200; // Flat parking angle
            __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, pan_pulse);
            __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, tilt_pulse);

            HAL_Delay(500);

            // De-energize servos to eliminate quiescent holding power
            HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_1);
            HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_2);
            is_parked = true;
        }
        return;
    }

    if (is_parked && total_light < NIGHT_EXIT_LUX) {
        return; // Stay parked until solid morning light is confirmed
    }
    is_parked = false;

    // Calculate Normalized Spatial Error (Scale: -1000 to +1000)
    float left   = tl + bl;
    float right  = tr + br;
    float top    = tl + tr;
    float bottom = bl + br;

    int32_t err_azimuth   = (int32_t)((1000.0f * (left - right)) / (left + right + 1.0f));
    int32_t err_elevation = (int32_t)((1000.0f * (top - bottom)) / (top + bottom + 1.0f));

    bool moved = false;

    // Azimuth Deadband Evaluation
    if (abs(err_azimuth) > DEADBAND_PERCENT) {
        if (err_azimuth > 0) {
            pan_pulse += (PAN_DIR * STEP_DEG_TICKS);
        } else {
            pan_pulse -= (PAN_DIR * STEP_DEG_TICKS);
        }
        pan_pulse = Clamp(pan_pulse, PAN_MIN_PULSE, PAN_MAX_PULSE);
        moved = true;
    }

    // Elevation Deadband Evaluation
    if (abs(err_elevation) > DEADBAND_PERCENT) {
        if (err_elevation > 0) {
            tilt_pulse += (TILT_DIR * STEP_DEG_TICKS);
        } else {
            tilt_pulse -= (TILT_DIR * STEP_DEG_TICKS);
        }
        tilt_pulse = Clamp(tilt_pulse, TILT_MIN_PULSE, TILT_MAX_PULSE);
        moved = true;
    }

    // Actuate Servos with Pulse Gating in Field Mode
    if (moved) {
        HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
        HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);

        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, pan_pulse);
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, tilt_pulse);

        if (current_mode == MODE_FIELD) {
            HAL_Delay(400); // Allow physical movement to complete
            HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_1);
            HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_2);
        }
    }
}

/* Sensor Offset Normalization Routine */
void Calibrate_Sensor_Offsets(void) {
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET); // Status LED ON
    HAL_Delay(1000);

    float s[4];
    s[0] = (float)Read_Filtered_Channel(ADC_CHANNEL_0);
    s[1] = (float)Read_Filtered_Channel(ADC_CHANNEL_1);
    s[2] = (float)Read_Filtered_Channel(ADC_CHANNEL_2);
    s[3] = (float)Read_Filtered_Channel(ADC_CHANNEL_3);

    float avg = (s[0] + s[1] + s[2] + s[3]) / 4.0f;
    if (avg > 50.0f) {
        for (int i = 0; i < 4; i++) {
            cal_factors[i] = avg / s[i];
        }
    }
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET); // Status LED OFF
}

/* Hardware INA219 Driver */
void INA219_Init(uint16_t addr) {
    uint8_t cfg[3] = {0x00, 0x39, 0x9F}; // 32V, 320mV Shunt, 12-bit Continuous
    HAL_I2C_Master_Transmit(&hi2c1, addr, cfg, 3, 50);

    uint8_t cal[3] = {0x05, 0x10, 0x00}; // Cal = 4096 for 0.1 Ohm shunt
    HAL_I2C_Master_Transmit(&hi2c1, addr, cal, 3, 50);
}

void INA219_ReadData(uint16_t addr, float* voltage, float* current_ma, float* power_mw) {
    uint8_t reg;
    uint8_t data[2];

    reg = 0x02; // Bus Voltage Register
    HAL_I2C_Master_Transmit(&hi2c1, addr, &reg, 1, 20);
    if (HAL_I2C_Master_Receive(&hi2c1, addr, data, 2, 20) == HAL_OK) {
        int16_t raw_v = (int16_t)((data[0] << 8) | data[1]);
        *voltage = (float)((raw_v >> 3) * 4) * 0.001f;
    } else {
        *voltage = 0.0f;
    }

    reg = 0x01; // Shunt Voltage Register
    HAL_I2C_Master_Transmit(&hi2c1, addr, &reg, 1, 20);
    if (HAL_I2C_Master_Receive(&hi2c1, addr, data, 2, 20) == HAL_OK) {
        int16_t raw_shunt = (int16_t)((data[0] << 8) | data[1]);
        *current_ma = (float)raw_shunt * 0.1f; // 0.1 Ohm shunt: 10 uV/LSB -> 0.1 mA/LSB
    } else {
        *current_ma = 0.0f;
    }

    if (*current_ma < 0.0f) *current_ma = 0.0f;
    *power_mw = (*voltage) * (*current_ma);
}

/* Minimal SSD1306 Display Driver */
void OLED_WriteCommand(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd};
    HAL_I2C_Master_Transmit(&hi2c1, OLED_ADDR, buf, 2, 10);
}

void OLED_Init(void) {
    OLED_WriteCommand(0xAE); // Display Off
    OLED_WriteCommand(0xD5); OLED_WriteCommand(0x80);
    OLED_WriteCommand(0xA8); OLED_WriteCommand(0x3F);
    OLED_WriteCommand(0xD3); OLED_WriteCommand(0x00);
    OLED_WriteCommand(0x40);
    OLED_WriteCommand(0x8D); OLED_WriteCommand(0x14); // Enable Charge Pump
    OLED_WriteCommand(0x20); OLED_WriteCommand(0x00);
    OLED_WriteCommand(0xA1);
    OLED_WriteCommand(0xC8);
    OLED_WriteCommand(0xDA); OLED_WriteCommand(0x12);
    OLED_WriteCommand(0x81); OLED_WriteCommand(0xCF);
    OLED_WriteCommand(0xD9); OLED_WriteCommand(0xF1);
    OLED_WriteCommand(0xDB); OLED_WriteCommand(0x40);
    OLED_WriteCommand(0xA4);
    OLED_WriteCommand(0xA6);
    OLED_WriteCommand(0xAF); // Display On
}

void OLED_Clear(void) {
    uint8_t payload[17];
    payload[0] = 0x40;
    memset(&payload[1], 0x00, 16);

    for (uint8_t page = 0; page < 8; page++) {
        OLED_WriteCommand(0xB0 + page);
        OLED_WriteCommand(0x00);
        OLED_WriteCommand(0x10);
        for (uint8_t i = 0; i < 8; i++) {
            HAL_I2C_Master_Transmit(&hi2c1, OLED_ADDR, payload, 17, 10);
        }
    }
}

/* Application Entry Point */
int main(void) {
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_ADC1_Init();
    MX_I2C1_Init();
    MX_TIM3_Init();

    // 1. Mandatory STM32 ADC Hardware Self-Calibration
    HAL_ADCEx_Calibration_Start(&hadc1);

    // 2. Initialize Telemetry & Display
    INA219_Init(INA219_ADDR_TRACKED);
    INA219_Init(INA219_ADDR_SYSTEM);
    INA219_Init(INA219_ADDR_FIXED);
    OLED_Init();
    OLED_Clear();

    // 3. Arm PWM and Center Motors
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, pan_pulse);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, tilt_pulse);
    HAL_Delay(600);

    if (current_mode == MODE_FIELD) {
        HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_1);
        HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_2);
    }

    uint32_t last_track_tick = 0;
    uint32_t last_telemetry_tick = 0;

    while (1) {
        uint32_t now = HAL_GetTick();

        // Pushbutton Interface on PA4:
        // Short Press (<2s) = Toggle Mode (Demo <-> Field)
        // Long Press (>2s)  = Trigger Sensor Offset Calibration
        if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_4) == GPIO_PIN_RESET) {
            uint32_t press_time = HAL_GetTick();
            while (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_4) == GPIO_PIN_RESET) {
                HAL_Delay(10);
            }
            uint32_t duration = HAL_GetTick() - press_time;
            if (duration > 2000) {
                Calibrate_Sensor_Offsets();
            } else if (duration > 50) {
                current_mode = (current_mode == MODE_DEMO) ? MODE_FIELD : MODE_DEMO;
            }
        }

        // Timing Engine: Tracking Loop
        uint32_t track_interval = (current_mode == MODE_DEMO) ? 300 : 30000;
        if (now - last_track_tick >= track_interval) {
            last_track_tick = now;
            Execute_Tracking_Step();
        }

        // Timing Engine: Telemetry Acquisition (Every 500 ms)
        if (now - last_telemetry_tick >= 500) {
            float dt_hours = (now - last_telemetry_tick) / 3600000.0f;
            last_telemetry_tick = now;

            INA219_ReadData(INA219_ADDR_TRACKED, &telemetry.v_tracked, &telemetry.i_tracked, &telemetry.p_tracked);
            INA219_ReadData(INA219_ADDR_FIXED,   &telemetry.v_fixed,   &telemetry.i_fixed,   &telemetry.p_fixed);
            INA219_ReadData(INA219_ADDR_SYSTEM,  &telemetry.p_system,  &telemetry.p_system,  &telemetry.p_system);

            telemetry.mah_tracked += (telemetry.i_tracked * dt_hours);
            telemetry.mah_fixed   += (telemetry.i_fixed * dt_hours);
        }
    }
}

/* Hardware Initializers */
static void MX_ADC1_Init(void) {
    hadc1.Instance = ADC1;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 1;
    HAL_ADC_Init(&hadc1);
}

static void MX_I2C1_Init(void) {
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 100000; // 100 kHz Standard Mode
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&hi2c1);
}

static void MX_TIM3_Init(void) {
    TIM_OC_InitTypeDef sConfigOC = {0};

    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 71;                    // 72 MHz / 72 = 1 MHz count rate (1 us tick)
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 19999;                    // 1 MHz / 20000 = 50 Hz frame rate (20 ms period)
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&htim3);

    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 1500;                       // 1500 us initial center
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;

    HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1); // PA6
    HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_2); // PA7
}

static void MX_GPIO_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    // PC13 LED
    GPIO_InitStruct.Pin = GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

    // PA4 Mode / Calibration Button
    GPIO_InitStruct.Pin = GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9; // 8MHz * 9 = 72MHz
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);

    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6; // 72MHz / 6 = 12MHz ADC Clock
    HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);
}
```

---

## 9. Experimental Benchmark Protocol (Eliminating Data Artifacts)

### 9.1 Pre-Test Panel Mismatch Calibration
Commercial low-cost solar panels demonstrate manufacturing variations of $\pm 5\%\text{ to }\pm 10\%$.
1. Mount both the tracked panel and the reference panel flat, perfectly parallel on a static table outdoors under clear noon sunlight.
2. Record power output across their identical $10\ \Omega$ load resistors for 10 minutes.
3. Compute the factory mismatch coefficient:
   $$k_{\text{mismatch}} = \frac{P_{\text{fixed, baseline}}}{P_{\text{tracked, baseline}}}$$
4. Multiply all real-time tracking generation readings by $k_{\text{mismatch}}$ in post-processing. This guarantees your recorded gains reflect purely mechanical tracking yield rather than silicon wafer binning.

### 9.2 The $I^2 R$ Non-Linearity Effect
When operating a solar panel into a fixed resistor below its maximum power voltage, the panel acts as a current source proportional to irradiance ($I \propto G$).
Because power scales quadratically with current ($P = I^2 R$):
$$\text{If irradiance increases by } 15\% \implies I_2 = 1.15 \cdot I_1$$
$$P_2 = (1.15 \cdot I_1)^2 R = 1.3225 \cdot P_1 \implies \mathbf{+32.25\%\text{ Power Gain!}}$$
* **How to Report Data Defensibly:** Never publish solely power percentage gains. Report both:
  1. **Optical Charge Harvest:** Measured in Milliamp-Hours ($\text{mAh}$).
  2. **Delivered Electrical Energy:** Measured in Milliwatt-Hours ($\text{mWh}$).
* **Accurate Presentation Statement:** 
  > *"Mechanical dual-axis tracking yielded a $+16.2\%$ increase in total optical charge capture ($142.1\text{ mAh}$ vs. $122.3\text{ mAh}$), resulting in a $+34.5\%$ increase in electrical power dissipation ($782\text{ mWh}$ vs. $581\text{ mWh}$) across the $10\ \Omega$ test load."*

---

## 10. Comprehensive Bench Commissioning Checklist

| Step | Action | Expected Measurement / Result | Failure Mode / Remediation |
| :--- | :--- | :--- | :--- |
| **1** | Power LM2596 without loads. | DMM probes on `OUT+` / `OUT-` read **$5.00\text{ V} \pm 0.05\text{ V}$**. | If $> 5.2\text{ V}$, rotate screw until regulated. |
| **2** | Connect Blue Pill logic (servos unplugged). | DMM on Blue Pill `3.3V` pin reads **$3.30\text{ V}$**. | If zero, check ground or blown LDO. |
| **3** | Flashlight on LDRs with 3.3V supply. | ADC input pins read between **$0.1\text{ V}\text{ and }3.1\text{ V}$**. | If $> 3.3\text{ V}$, divider is connected to 5V rail. |
| **4** | Connect ST-Link V2. | Only `SWDIO`, `SWCLK`, and `GND` connected. | If 3.3V is connected, disconnect immediately. |
| **5** | I²C Address Scan. | Firmware detects `0x3C`, `0x40`, `0x41`, `0x44`. | If devices missing, check address solder bridges. |
| **6** | Plug in Servos. | No twitching or resets on startup. | If resetting, check $1000\ \mu\text{F}$ capacitor & pull-downs. |
| **7** | Calibrate Offsets. | Hold button on `PA4` for $> 2\text{ s}$ under uniform white light. | LED toggles; normalization stored in RAM. |
| **8** | Flashlight Direction Test (Demo Mode). | Pan and tilt follow flashlight smoothly. | If running away, invert `PAN_DIR` or `TILT_DIR`. |
| **9** | Night Park Test. | Cover sensor head completely with a dark cloth. | Servos park East at $45^\circ$, then PWM stops. |
| **10**| Load Resistor Temperature. | Resistors warm to touch, below $80^\circ\text{C}$. | Ensure mounted away from PLA 3D-printed parts. |
