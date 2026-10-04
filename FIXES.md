# Helion – corrected design notes (rev 2)

Companion to `Core/Src/main.c`. Covers the hardware changes, which drawings to trust, how to
drop the firmware into a project, and a bring-up order that cannot hurt anything.

> **Status of this review.** Everything below comes from reading the README, the manual, the
> audit report and the five diagrams. The firmware was compiled with strict warnings against a
> stub of the HAL and its logic was unit-tested on a PC (INA219 decoding, tracking maths, runaway /
> LDR-fault / calibration / night-park logic, dashboard string lengths). It has **not** been built
> with the ARM toolchain or run on real hardware. Treat the first power-up as a test.

---

## 1. Which drawings to trust

| File | Verdict | Why |
|---|---|---|
| `ldr_divider_circuit.jpg` | **Correct** | 3.3 V → LDR → ADC pin → fixed R → GND, 10 nF at the pin. Matches the firmware. |
| `protection_components.jpg` | Parts photo only | Fine, but swap the 1N5819 for a 3 A part (see §3). |
| `i2c_bus_wiring.jpg` | Mostly correct | MCU is labelled `STM32L476RG` (should be F103C8). Draws two 4.7 k pull-ups on top of the ones already on the modules – measure first (§3, item 6). |
| `ina219_panel_wiring.jpg` | **Fix before use** | Shows an Arduino Uno with 5 V logic. Use the Blue Pill and **3.3 V**; the panel's negative lead must also go to the star ground (the picture leaves it floating). |
| `system_wiring_overview.jpg` | **Do not build from it** | See below. |

Problems in `system_wiring_overview.jpg`:

1. The INA219 `IN+` hangs on the 5 V buck output and `IN−` goes to GND – the 0.1 Ω shunt is across the supply.
   The system INA219 belongs **in series on the 9 V input** (§2).
2. A red trace joins the 3.3 V I²C pull-up node to the 5 V net. 3.3 V and 5 V must never touch
   (STM32 abs. max ≈ 4 V).
3. The 5 V feed enters at the Blue Pill's micro-USB end, contradicting the manual's own warning.
4. The power switch is drawn in the **ground** leg. Switch the **positive** leg only; a switched
   ground leaves sneak paths through the ST-Link, panels and star ground.
5. In the LDR block, the LDRs are not tied to 3.3 V and two of them are drawn twice.

---

## 2. Corrected power path (build exactly this)

```
J1 barrel (+) ─► SW1 ─► F1 (2 A) ─► D1 ─┬─► INA219 #2 VIN+ ─► VIN− ─► U1 LM2596 IN+
                                         │
                                       TVS1 (to GND)

J1 barrel (−) ───────────────────────────────► STAR GND ◄── U1 IN−, U1 OUT−

U1 OUT+ ──► 5V BUS ──┬─► C1 1000 µF + C2 100 nF ──► GND
                     ├─► TVS2 (to GND)
                     ├─► Pan servo V+ , Tilt servo V+
                     └─► D2 ─► Blue Pill 5V pin ─► C3 100 µF + 100 nF ─► GND   (D2 optional, see item 5)

Blue Pill 3V3 ──┬─► LDR × 4 (top legs)  ─► C4 100 µF + 100 nF ─► GND (near OLED/INA cluster)
                ├─► INA219 #1/#2/#3 VCC
                └─► OLED VCC
```

Signal wiring (unchanged from the manual, with the fixes in §3):

| Net | From | To |
|---|---|---|
| LDR TL / TR / BL / BR | 3V3 → LDR → node | node → PA0 / PA1 / PA2 / PA3, node → R_fixed → GND, node → 10 nF → GND |
| Button | PA4 | → button → GND (internal pull-up) |
| Pan / Tilt PWM | PA6 / PA7 | → 330 Ω → servo signal, servo signal → 10 k → GND |
| I²C | PB6 SCL / PB7 SDA | → all four modules |
| Tracked panel | PV+ → INA219 #1 VIN+ ; VIN− → 10 Ω 5 W → GND ; PV− → STAR GND |
| Fixed panel | PV+ → INA219 #3 VIN+ ; VIN− → 10 Ω 5 W → GND ; PV− → STAR GND |
| SWD | ST-Link SWDIO, SWCLK, GND, **NRST** | → Blue Pill (never 3.3 V while the board is externally powered) |

---

## 3. Hardware changes

| # | Problem | Fix |
|---|---|---|
| 1 | **D1 = 1N5819 is rated 1 A.** Two stalled servos + logic draw ≈ 1.0–1.2 A at 9 V and the 2 A fuse will not protect the diode. | Use a **3 A Schottky** (SS34 or 1N5822), or a P-MOSFET ideal-diode circuit. |
| 2 | **Input surge / wrong adapter.** INA219 VIN+ is rated 26 V; LM2596 module inputs are typically 40 V parts. | Add **TVS1 = SMBJ12A** from D1's cathode to GND (upstream of the INA219). Clamps ≈ 19 V. |
| 3 | **A failed LM2596 puts the full input voltage on 5 V-only servos** (MG90S abs. max ≈ 6 V). | Add **TVS2 = SMBJ5.0A** across the 5 V bus. With the 2 A fuse upstream it acts as a crowbar: the fuse opens, the servos survive. Trim the LM2596 with nothing connected, as the manual says. |
| 4 | **Switch in the ground leg** (diagram). | SW1 in the **positive** leg only (as drawn in the manual's block diagram). |
| 5 | **USB / SWD conflict.** USB VBUS and the `5V` header pin are the same net on a Blue Pill. | Program over SWD only; leave micro-USB unplugged. Optional hardening: fit **D2 = 1N5819** from the 5 V bus to the Blue Pill `5V` pin (stops a PC from back-feeding the servo rail and isolates the MCU from servo sag). With D2, trim the buck to **5.10 V** so the onboard 3.3 V LDO keeps its headroom. |
| 6 | **I²C pull-ups may be stacked** (audit 18). | With power off, measure resistance SDA→3V3 on the finished bus. Target 1.5 k – 10 k. Below 1.5 k: remove pull-ups from some modules. Above 10 k: add 4.7 k. Never add 4.7 k "just in case". |
| 7 | **3.3 V rail has little headroom on clone boards** (audit 12). | 100 µF + 100 nF near the OLED / INA219 cluster (C4). |
| 8 | **ADC pin PA0 is missing its 10 nF** in the pinout table. | 10 nF on all four ADC pins (the schematic already shows it). |
| 9 | **LDR divider value** has to be hand-swapped between indoor and outdoor. | One compromise value of **4.7 kΩ** works across dusk to full sun (≈ 3.2 V at 100 Ω LDR, ≈ 0.5 V swing when one quadrant is shaded). Or keep 10 k indoors / 1.5 k outdoors and remember to swap. The night thresholds in `main.c` assume 4.7–10 k. |
| 10 | **Servo signals**: ringing and cross-coupling into the MCU pin. | 330 Ω series resistor in each signal line, in addition to the 10 k pull-down. |
| 11 | **No hardware reset line to the programmer.** | Wire ST-Link **NRST** to the Blue Pill `R` pin so you can "connect under reset" if the firmware ever stops SWD from responding. |
| 12 | **Load resistors run ≈ 2.7 W on a 5 W part** (manual §6.3). | Mount on metal standoffs, away from PLA parts and from the panels – their heat also skews your benchmark. |
| 13 | **Half-breadboard in the 5 V / GND servo path** (manual §3.3). | Solder 5 V, GND and servo feeds on perfboard with ≥ 20 AWG wire. Breadboards also loosen under vibration outdoors. |
| 14 | **Servo load** – the manual's own figure is 0.88 kg·cm of a ~2 kg·cm stall rating. | Balance the tilt axis on its centre of gravity, shorten the lever, keep a counter-weight or switch to a stronger servo if the panel droops when released. Leave `FIELD_RELEASE_SERVOS 0` until you have done this. |
| 15 | **Cables across the pan axis** wear out. | Leave a service loop; limit pan range to what the cable allows. |
| 16 | **Outdoor use**: moisture, UV, wind. | Enclosure + conformal coat on the electronics; PETG/ASA instead of PLA; panel-lead TVS (SMBJ12A) if the leads are long. |

---

## 4. Firmware – what changed and what to check

### 4.1 Behaviour summary

* **Tracking never depends on I²C.** If the telemetry bus dies, the tracker keeps working and the
  OLED shows `--` for the missing devices. The firmware re-probes missing devices every 5 s.
* **Boot:** staggered servo start → if dark, park east; otherwise run a coarse **sun search**
  (3 tilt rows × 13 pan positions, ≈ 7 s) so the baffle's narrow field of view cannot leave the
  panel pointing away from the sun. The same search runs every morning when light returns.
* **Controller:** step = 5 µs + |error|/20 µs (max 30 µs), 4 % deadband, same sign convention as before
  (`PAN_DIR` / `TILT_DIR`).
* **Fault handling** (LED blinks fast, tracking holds position, short press clears):
  * *Runaway*: moving the same way 40 times without the error shrinking (or reaching a travel
    limit after ≥ 12 such steps) = wrong `PAN_DIR`/`TILT_DIR`, slipped horn or blocked panel.
  * *LDR*: one channel ≈ 0 while another is clearly lit = open wire or broken divider. Clears itself
    once the sensor is back.
  * Reaching a travel limit while the error was still improving is **not** a fault – it shows `LIMIT`
    (the sun left the mechanical range).
* **Calibration** (hold button > 2 s under uniform light): rejects dark, saturated or very unequal
  lighting, then saves gains to the last flash page so they survive power cycles.
* **Watchdog** 4 s; the reason for the last reset is shown at boot if it was the watchdog.
* **Button:** short press = Demo ⇄ Field (and clears faults); hold > 2 s = calibrate. Non-blocking.
* **LED (PC13):** heartbeat = OK · double blink = an I²C device is missing · fast blink = fault ·
  rare blip = parked.

### 4.2 Project setup (STM32CubeIDE, F1 HAL)

1. Create a project for **STM32F103C8Tx** with **no peripherals enabled in the .ioc** (clock: HSE crystal,
   debug: Serial Wire). `main.c` creates ADC1, TIM3, I²C1, GPIO and IWDG itself and contains its own
   `HAL_*_MspInit` callbacks. If you enable them in CubeMX too, delete the duplicates from
   `stm32f1xx_hal_msp.c` or the linker will report multiple definitions.
2. Replace `Core/Src/main.c` with the file in this folder (your generated `main.h` is used as-is).
3. In `stm32f1xx_hal_conf.h` make sure these are enabled: `HAL_ADC_MODULE_ENABLED`,
   `HAL_FLASH_MODULE_ENABLED`, `HAL_GPIO_MODULE_ENABLED`, `HAL_I2C_MODULE_ENABLED`,
   `HAL_IWDG_MODULE_ENABLED`, `HAL_RCC_MODULE_ENABLED`, `HAL_TIM_MODULE_ENABLED`, `HAL_CORTEX_MODULE_ENABLED`.
4. `SysTick_Handler` must call `HAL_IncTick()` (CubeIDE generates this in `stm32f1xx_it.c`).
5. **Reserve the calibration page.** The calibration record lives at `0x0800FC00` (last 1 KB of a 64 KB
   part, also valid on 128 KB clones). In `STM32F103C8TX_FLASH.ld` change `FLASH ... LENGTH = 64K`
   to `63K` so the linker cannot place code there.
6. Build with `-O0`, `-O2` or `-Os`: no timing depends on the optimiser any more (DWT counter).
7. Float `printf` is not used; newlib-nano is fine.

### 4.3 Things you must tune on your own hardware

| Item | Where | How |
|---|---|---|
| `PAN_DIR`, `TILT_DIR` | top of `main.c` | If an axis runs away from a flashlight, flip the sign. The runaway fault will tell you. |
| Servo limits | `*_MIN/MAX_PULSE` | Set `SERVO_SWEEP_TEST 1`, flash, listen for grinding, press the button, put the limits ≈ 50 µs inside the pulse shown, set it back to `0`. |
| Night thresholds | `NIGHT_ENTER/EXIT_ADC_SUM` | Sums of four ADC readings (0–16380). Watch `last_sum` at dusk in the debugger and pick values. Defaults assume 4.7–10 k dividers. |
| Mains frequency | `MAINS_HZ` | 50 or 60. Sets the ADC averaging window to one mains cycle. |
| Panel mismatch | `K_MISMATCH_X1000` | Run both panels flat side by side (manual §9.1), set `P_fixed / P_tracked × 1000`. |
| I²C speed | `I2C_SPEED_HZ` | 100 kHz is safe. 400 kHz works with short wires and makes OLED refresh ~4× faster. |
| Field mode servo release | `FIELD_RELEASE_SERVOS` | Leave at 0 until the panel is properly balanced. |

### 4.4 Audit items, for the record

* **Wrong in the audit, not changed:** #5 (`0x399F` is already ±320 mV / 32 V / 12-bit / continuous – the
  suggested `0x3F9F` would set 128-sample averaging) and #17 (no overflow exists).
* **#9 was right but its fix backfires:** stopping PWM after every move removes holding torque. Hold is
  now the default; release is opt-in for field mode.
* **Added beyond the audit:** runaway and LDR-open detectors, calibration sanity checks, sun search,
  staggered servo start, tracker independent of I²C, NRST wiring advice, TVS / diode rating fixes.

---

## 5. Re-running the host tests

```
cd tests
gcc -std=c11 -Wall -Wextra -Wshadow -I. test.c -o test && ./test
```
`tests/main.h` is a minimal fake of the STM32 HAL, used only on a PC. It is **not** the header for the real project.

---

## 6. Bring-up order (each step has a pass/fail)

1. **Unpowered continuity:** 5 V ↔ GND, 3V3 ↔ GND, 5 V ↔ 3V3, 9 V ↔ GND must all read open (> 1 kΩ).
2. **LM2596 alone** (nothing on its output): trim to 5.00 V (5.10 V if D2 fitted). Seal the screw.
3. **Blue Pill alone** on the 5 V bus (no servos, no I²C): `3V3` pin = 3.30 V ± 0.1.
4. **Flash over SWD** (SWDIO, SWCLK, GND, NRST). With the I²C modules not connected yet, the LED shows the
   *double blink* (device missing) instead of the heartbeat – that is expected at this stage.
5. **LDR dividers:** shine a torch on each LDR – PA0…PA3 move between roughly 0.1 V and 3.1 V.
6. **I²C modules:** boot splash lists `OLED / INA1 / INA2 / INA3`. Any `--` means an address bridge or
   wiring problem (INA219 #2 → bridge A0 = 0x41, #3 → bridge A1 = 0x44).
7. **Servos last,** ideally from a bench supply limited to ~1 A. No reset or OLED glitch on start-up.
8. **Servo sweep test** (§4.3) to set safe limits, then restore `SERVO_SWEEP_TEST 0`.
9. **Direction test** in Demo mode with a torch. If it runs away, flip `PAN_DIR`/`TILT_DIR` – or just let the
   runaway fault tell you which axis.
10. **Calibrate** (hold button > 2 s under diffuse light). Expect `CAL SAVED`; power-cycle and check it persists.
11. **Night test:** cover the head – it should park east after ≈ 5 ticks (1.5 s in Demo) and release the servos;
    uncover – it should run the sun search and resume tracking.
12. **Panels last:** connect the two panels through the INA219s and load resistors; compare `GAIN` on the OLED.
