# Helion – design notes & bring-up (single-axis, rev 3)

Companion to `Core/Src/main.c`. Covers which drawings to trust, the corrected power path,
project setup, tuning, known limits and a bring-up order that cannot hurt anything.

> **Status.** The firmware logic is unit-tested on a PC against a fake HAL (49 checks, see §5).
> It has **not** been built with the ARM toolchain or run on real hardware. Treat the first
> power-up as a test.

---

## 1. Which drawings to trust

| File | Verdict |
|---|---|
| `ldr_divider_circuit.jpg` | **Correct** – 3.3 V → LDR → ADC pin → fixed R → GND, 10 nF at the pin. Build 2 copies (PA0, PA1). |
| `protection_components.jpg` | Parts photo only. Use a **3 A** Schottky, not the 1N5819 shown. |
| `i2c_bus_wiring.jpg` | MCU is labelled STM32L476RG (it is an F103C8). It also draws 4.7 k pull-ups on top of the ones already on the modules – measure first (§3, item 6). |
| `ina219_panel_wiring.jpg` | Shows an Arduino Uno / 5 V logic. Use the Blue Pill and **3.3 V**; panel negative must go to the star ground. |
| `system_wiring_overview.jpg` | **Do not build from it.** Shunt drawn across the supply, 3.3 V tied to 5 V, switch in the ground leg, 4 LDRs and 2 servos (old design). |

## 2. Power path (build exactly this)

```
J1 (+) ─► SW1 ─► F1 2 A ─► D1 3 A Schottky ─┬─► INA219 #2 VIN+ ─► VIN− ─► LM2596 IN+
                                             └─► TVS1 SMBJ12A ─► GND
J1 (−) ──────────────────────────────────────────► STAR GND ◄── LM2596 IN−, OUT−

LM2596 OUT+ ─► 5 V BUS ─┬─► C1 1000 µF + 100 nF ─► GND
                        ├─► TVS2 SMBJ5.0A ─► GND
                        ├─► servo V+
                        └─► Blue Pill 5V pin

Blue Pill 3V3 ─► 2 LDR (top legs), INA219 ×3 VCC, OLED VCC   (+100 µF/100 nF near the OLED)
```

| Net | Wiring |
|---|---|
| LDR Left / Right | 3V3 → LDR → node; node → PA0 / PA1; node → 4.7 k → GND; node → 10 nF → GND |
| Button | PA4 → button → GND (internal pull-up) |
| Pan servo | PA6 → 330 Ω → servo signal; servo signal → 10 k → GND |
| I²C | PB6 SCL / PB7 SDA → OLED + 3 INA219 |
| Tracked / fixed panel | PV+ → INA219 VIN+ ; VIN− → 10 Ω 5 W → GND ; PV− → STAR GND |
| SWD | ST-Link SWDIO, SWCLK, GND, **NRST** (never its 3.3 V while the board is externally powered) |

## 3. Hardware issues that show up in real use

| # | Problem | Fix |
|---|---|---|
| 1 | 1N5819 is a 1 A part; a stalled servo plus logic is ≈ 0.6–0.9 A at 9 V. | **SS34 / 1N5822 (3 A)**. |
| 2 | Input surge / wrong adapter (INA219 VIN+ rated 26 V). | TVS1 = SMBJ12A after D1. |
| 3 | A failed LM2596 puts the full input voltage on the 5 V-only servo. | TVS2 across the 5 V bus + 2 A fuse upstream (acts as a crowbar). **Use SMBJ5.0A** – a 1.5KE5.6A starts to leak at 5.1 V and warms up. Trim the buck with nothing connected. |
| 4 | Switch in the ground leg. | Switch the **positive** leg only. |
| 5 | USB and the `5V` header pin are the same net. | Program over SWD only, micro-USB unplugged. |
| 6 | Stacked I²C pull-ups. | Power off, measure SDA→3V3: want 1.5 k – 10 k. Never add 4.7 k "just in case". |
| 7 | 3.3 V LDO on clone boards is weak. | 100 µF + 100 nF near the OLED / INA219 cluster. |
| 8 | Servo signal ringing / cross-coupling. | 330 Ω series + 10 k pull-down on PA6. |
| 9 | No hardware reset from the programmer. | Wire ST-Link **NRST** to the Blue Pill `R` pin ("connect under reset"). |
| 10 | Load resistors dissipate ≈ 2.7 W each on 5 W parts; heat skews the benchmark and softens PLA. | Metal standoffs, away from the panels and printed parts. |
| 11 | Breadboard in the 5 V / GND servo path loosens outdoors and drops voltage. | Solder on perfboard, ≥ 20 AWG. |
| 12 | Servo load ≈ 0.88 kg·cm of a ≈ 2 kg·cm stall rating. | Balance the panel on its pivot; keep `FIELD_RELEASE_SERVOS` and `PARK_RELEASE_SERVO` at 0. |
| 13 | A servo covers ≈ 110–120° with the safe pulse limits; the sun's daily arc can be wider. | Aim the rest position at the middle of the arc; expect `LIMIT` at the extremes (not a fault). |
| 14 | Cables across the pan axis wear out. | Service loop; keep travel within what the cable allows. |
| 15 | Moisture, UV, wind. | Enclosure, conformal coat, PETG/ASA instead of PLA. |
| 16 | Full-sun LDR dividers sit near 3.2 V (≈ 3970 counts). | 4.7 k keeps both indoor and full-sun readings inside the ADC range. Do not go above ~10 k. |
| 17 | The OLED is unreadable and ages when it runs 24/7 outdoors. | Cosmetic only – tracking and logging do not depend on it. |

## 4. Firmware

### 4.1 Behaviour

* **Tracking never depends on I²C.** A dead telemetry bus shows `--` on the OLED and the firmware
  re-probes missing devices every 5 s.
* **Boot:** 100 ms power-up settle → centre servo → if dark, park; otherwise sweep the whole pan range
  and move to the brightest position. The same sweep runs at dawn.
* **Controller:** step = 5 µs + |error|/20 (max 30 µs), 4 % deadband. Demo mode ticks every 300 ms, field
  mode every 30 s.
* **Faults** (fast LED blink, servo holds, short press clears): *runaway* = 40 same-direction steps with no
  improvement (wrong `PAN_DIR`, slipped horn, blocked panel); *LDR* = one channel ≈ 0 while the other is lit
  (self-clears). Hitting the end of travel while the error was still improving is shown as `LIMIT`, not a fault.
* **Calibration** (hold button > 2 s under uniform light): rejects dark, saturated or very unequal light,
  then saves the two gains to the last flash page.
* **Watchdog** 4 s; a watchdog reset is shown at boot. Fatal CPU faults reset immediately; a failed
  clock/peripheral init blinks the LED for a few seconds and resets.
* **LED (PC13):** heartbeat = OK · double blink = I²C device missing · fast blink = fault · rare blip = parked.

### 4.2 Project setup (STM32CubeIDE, F1 HAL)

1. New project for **STM32F103C8Tx**, clock = HSE crystal, debug = Serial Wire, **no peripherals enabled**
   in the `.ioc`. `main.c` creates ADC1, TIM3, I²C1, GPIO and IWDG and contains its own `HAL_*_MspInit`
   callbacks. If you enable peripherals in CubeMX, delete the duplicates from the generated
   `stm32f1xx_hal_msp.c`.
2. Copy `Core/Src/main.c`, `Core/Src/stm32f1xx_it.c` and `Core/Inc/main.h` into the project (replace the generated ones).
3. In `stm32f1xx_hal_conf.h` enable `HAL_ADC`, `HAL_FLASH`, `HAL_GPIO`, `HAL_I2C`, `HAL_IWDG`, `HAL_RCC`,
   `HAL_TIM`, `HAL_CORTEX` modules.
4. **Reserve the calibration page.** It lives at `0x0800FC00`. In `STM32F103C8TX_FLASH.ld` change
   `FLASH ... LENGTH = 64K` to `63K`.
5. Any optimisation level works; timing uses the DWT cycle counter. Float `printf` is not used.

### 4.3 Things to tune on your own hardware

| Item | Where in `main.c` | How |
|---|---|---|
| `PAN_DIR` | top | If the servo runs away from a torch, flip the sign – the runaway fault will tell you. |
| Servo limits | `PAN_MIN/MAX_PULSE` | Set `SERVO_SWEEP_TEST 1`, flash, listen for grinding, press the button, keep the limits ≈ 50 µs inside, set it back to `0`. |
| Park position | `PARK_PAN_PULSE` | Aim at the sunrise side. With `PAN_DIR` +1 a higher pulse turns towards the **right** LDR – which side is east depends on how you mounted it. |
| Night thresholds | `NIGHT_ENTER/EXIT_ADC_SUM` | Sums of two ADC readings (0–8190). Watch `last_sum` (shown as `S…` on the OLED) at dusk. Defaults suit a 4.7 k divider. |
| Mains frequency | `MAINS_HZ` | 50 or 60. Sets the ADC averaging window to one mains cycle. |
| Panel mismatch | `K_MISMATCH_X1000` | Run both panels flat side by side, set `P_fixed / P_tracked × 1000`. |
| Mode after reset | `BOOT_MODE` | `MODE_FIELD` for a permanent outdoor install. |
| I²C speed | `I2C_SPEED_HZ` | 100 kHz is safe; 400 kHz works with short wires. |

### 4.4 Known limits (not changed – your call)

* A **runaway fault stays latched** until a short button press. Outdoors with no one around the tracker
  holds position until someone resets it; an automatic retry after some minutes would be a possible addition.
* The **mode is not saved** – set `BOOT_MODE` instead.
* Energy is integrated from instantaneous INA219 readings every 500 ms and is **lost on reset** (RAM only).
* The OLED shows tracked/fixed power and energy; nothing is logged off-board.

## 5. Host tests

```
cd tests
gcc -std=c11 -Wall -Wextra -Wshadow -I. test.c -o test && ./test
```
`tests/main.h` is a minimal fake of the STM32 HAL for the PC. It is **not** the header for the real project.

## 6. Bring-up order (each step has a pass/fail)

1. **Unpowered continuity:** 5 V↔GND, 3V3↔GND, 5 V↔3V3, 9 V↔GND all open (> 1 kΩ).
2. **LM2596 alone** (nothing on its output): trim to 5.00 V. Seal the screw.
3. **Blue Pill alone** on the 5 V bus: `3V3` pin = 3.30 V ± 0.1.
4. **Flash over SWD.** With the I²C modules not yet connected the LED shows the double blink – expected.
5. **LDR dividers:** torch on each LDR – PA0 / PA1 swing between roughly 0.1 V and 3.1 V.
6. **I²C modules:** boot screen shows `OLED:OK INA1:OK INA2:OK INA3:OK`. A `--` means an address bridge
   (INA #2 → A0 = 0x41, #3 → A1 = 0x44) or wiring problem.
7. **Servo last**, ideally from a bench supply limited to ~1 A. No reset or OLED glitch at start-up.
8. **Sweep test** (§4.3) to set the limits, then restore `SERVO_SWEEP_TEST 0`.
9. **Direction test** in Demo mode with a torch. If it runs away, flip `PAN_DIR`.
10. **Calibrate** (hold button > 2 s under diffuse light). Expect `CAL SAVED`; power-cycle and check it persists.
11. **Night test:** cover the head – it parks after ≈ 1.5 s in Demo; uncover – it sweeps and resumes tracking.
12. **Panels last:** connect both panels through the INA219s and load resistors; compare `GAIN` on the OLED.
