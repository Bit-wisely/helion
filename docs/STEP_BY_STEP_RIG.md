# Cardboard light-follower: every step, from parts to a moving servo

What you get: two LDRs on a cardboard plate, glued to an SG90 servo. Shine a light and the plate turns toward the brighter side.
Wiring picture: `docs/rig_wiring_diagram.png`. Code: this project, built with the `rig` setting (no power sensors).
**Not yet run on real hardware by the author**, and not yet compiled with the ARM compiler: if the first build prints an error, send it to me.

---
## 0. Read these cautions first

1. **Power the board only from its own micro-USB cable** in this rig. Never connect the ST-Link's 3.3 V or 5 V pins (two supplies fighting can damage the board). Do not use the 9 V adapter or LM2596 here.
2. **Never feed 5 V into A0 or A1.** Those pins accept 3.3 V maximum. The LDR dividers go to the **3.3** pin, the servo goes to **5V**.
3. **Servo red wire to 5V, brown to GND, orange to A6.** Reversed power can destroy the servo.
4. **Never turn the servo horn or shaft by hand**, powered or not (SG90 plastic gears strip). Fit the horn only after the servo has centred itself (step 8).
5. **Servo current:** an SG90 can pull about 0.5 to 0.7 A when it pushes against a stop or a heavy plate. A laptop USB port gives 0.5 A, so the board may reset or the servo may buzz. Use a light plate, keep it free to swing, narrow the limits (step 10), add a 100 to 470 uF capacitor across servo 5 V and GND if you have one, or use a powered USB hub.
6. **A short press of the button (or touching A4 to GND briefly) switches DEMO to FIELD mode**, where the plate only moves every 30 s. If it looks frozen, press again to return to DEMO.
7. **Dim rooms:** if both LDRs read very dark the tracker "parks" at one end and stops following. Use a real lamp or torch for tests.
8. **ST-Link and Blue Pill USB plug into the same laptop**; that is fine.
9. Do not unplug wires while powered. Do not touch the LDR dividers with 5 V wires.
10. Keep the BOOT0 and BOOT1 yellow jumpers on the **0** side.

---
## 1. Check you have everything

| Item | Qty | Note |
|---|:--:|---|
| Blue Pill (STM32F103C8) | 1 | |
| ST-Link V2 + its jumper ribbon | 1 | |
| SG90 servo, horns, screw | 1 | |
| LDR (photoresistor) | 2 | your photo shows 1; check the bag |
| Resistor 4.7 kohm (10 kohm also works) | 2 | **not in your photos** |
| Half-size breadboard | 1 | |
| Jumper wires | about 12 | male-male (breadboard to board/servo) and male-female (LDR leads) |
| Cardboard, glue, tape | | plate about 8 x 5 cm, a fin 4 cm tall |
| Header pins (2 x 20 male) + soldering iron | 1 set | your Blue Pill photo shows bare holes |
| Optional: button, capacitor 100 to 470 uF | | |
| Optional later: USB-to-TTL adapter (3.3 V) | 1 | only for the laptop dashboard |

---
## 2. Prepare the Blue Pill

1. Solder two rows of 20 male header pins to the board (long side of the pins pointing down). Without headers it cannot plug into the breadboard.
2. The 4-pin SWD header on the right side is already fitted. Read the tiny labels under the board next to it and note which pin is **GND, CLK (SWCLK), DIO (SWDIO), 3V3**.
3. Check both yellow jumpers are on the **0** side.

---
## 3. Install the software (Windows)

1. Install **Visual Studio Code** (code.visualstudio.com).
2. In VS Code, Extensions, search **PlatformIO IDE**, install, restart VS Code. The first start downloads tools: wait until it finishes.
3. Install the **ST-Link driver** (ST "STSW-LINK009"). Plug the ST-Link into the laptop; Device Manager should show it without a warning sign.
   (Linux: install PlatformIO's udev rules. macOS: nothing extra.)
4. Unzip `helion-main.zip` to a folder with no unusual characters in its path.
5. VS Code: File, Open Folder, choose `helion-main`. PlatformIO will detect `platformio.ini`.

The first build downloads about 300 MB (compiler and the STM32 libraries), so you need internet once.

---
## 4. Build the cardboard head

1. Cut a plate about 8 x 5 cm. Glue a 4 cm tall strip upright across the middle, along the 5 cm direction: the **fin**.
2. Mark two spots near the front edge, one each side of the fin, about 4 cm apart. Glue or tape one LDR at each spot, both facing forward (toward the light), legs pointing back.
3. Fit a male-female jumper on each LDR leg (female end onto the leg, tape it so it cannot slip). LDRs have no polarity.
4. Do **not** glue the plate to the servo yet.

"Left" and "right" below are as seen from behind the plate, looking at the light.

---
## 5. Wire the breadboard (rails first)

Put the Blue Pill in the middle of the breadboard (USB socket at one end). Each pin now has 2 to 4 free holes in its own column.

Rails: **top + rail = 3.3 V**, **top - rail = GND**, **bottom + rail = 5 V**, **bottom - rail = GND**. Join the two - rails with one jumper wire. If your rails are split in the middle, bridge them.

| Step | Wire | Notes |
|---|---|---|
| 1 | Blue Pill **3.3** (top row) to top + rail | |
| 2 | Blue Pill **G** (top row) to top - rail | |
| 3 | Blue Pill **5V** (bottom row) to bottom + rail | |
| 4 | Blue Pill **G** (bottom row) to bottom - rail | |
| 5 | top - rail to bottom - rail | common ground |
| 6 | LDR LEFT lead 1 to top + rail | |
| 7 | LDR LEFT lead 2 to the hole in the column of Blue Pill **A0** | |
| 8 | resistor 1: one leg in the A0 column, other leg to the top - rail | |
| 9 | LDR RIGHT lead 1 to top + rail | |
| 10 | LDR RIGHT lead 2 to the column of Blue Pill **A1** | |
| 11 | resistor 2: one leg in the A1 column, other leg to the top - rail | |
| 12 | servo red to bottom + rail (5 V) | use male-male jumpers into the servo plug |
| 13 | servo brown to bottom - rail | |
| 14 | servo orange to the column of Blue Pill **A6** | |
| 15 | optional button between the **A4** column and the - rail | |

Check against `docs/rig_wiring_diagram.png`. Look for: red wire on 5V only to the servo, nothing 5 V near the LDR side, no leg of the resistor touching the neighbouring column.

### ST-Link to the Blue Pill SWD header (4 wires)

| ST-Link socket pin (printed on it) | Blue Pill |
|---|---|
| 2 SWCLK | header **CLK** |
| 4 SWDIO | header **DIO** |
| 6 GND | header **GND** |
| 1 RST (recommended) | top-row pin **R** |

Leave ST-Link 3.3 V and 5 V pins empty and leave the header 3V3 empty.

---
## 6. Pre-power check (30 seconds)

- No bare wire ends touching each other.
- Servo plug orientation: brown at the GND side, red in the middle, orange at the signal side.
- The 3.3 rail and 5 V rail are different rails and are not joined.

---
## 7. Flash the firmware

1. Plug the **Blue Pill micro-USB** into the laptop. The red PWR LED lights. (The servo may twitch: normal.)
2. Plug the **ST-Link** into the laptop.
3. In VS Code open the PlatformIO tab (alien head), expand **rig**, click **Upload**.
   Or in the PlatformIO terminal: `pio run -e rig -t upload`
4. Success looks like: `Programming Started ... Verified OK ... Resetting Target` then `SUCCESS`.

If it fails:

| Message | Fix |
|---|---|
| `UNEXPECTED idcode` | clone chip: remove the `;` before `upload_flags` in `platformio.ini`, upload again |
| `init mode failed`, `target not found`, `open failed` | check CLK/DIO/GND wires, swap them if you misread the labels; connect RST to R; try again while holding the Blue Pill reset button, then release just as upload starts |
| compile errors | copy the red lines and send them to me |
| no ST-Link found (Windows) | install the driver (step 3 of section 3), try another USB port |

---
## 8. First run

1. Remove your hand from the servo. After reset the servo **centres** (about 600 ms).
2. Now power off and fit the horn pointing straight along the servo body, screw it in, and glue the cardboard plate to the horn so it points forward.
3. Power on again. The plate **sweeps its whole range once** (about 2 seconds, about 110 degrees), then settles toward the brightest direction. Keep its path clear.
4. Move a torch or lamp about 30 cm in front and sweep it left and right slowly. The plate should follow within about a second.

---
## 9. If it does not behave

| Symptom | Cause and fix |
|---|---|
| Turns away from the light, ends at a stop | direction reversed: swap the A0 and A1 wires, or set `#define PAN_DIR (-1)` in `Core/Src/main.c`, upload again |
| Hunts to one end and sticks | same as above; this is the "runaway" safety stop. Fix the direction, then touch A4 to GND briefly **twice** (each short press clears faults and also switches DEMO/FIELD, so two presses return to DEMO), or just power-cycle |
| Does nothing, rests on one side | too dark (parked) or in FIELD mode: use a bright lamp, press the button once |
| Rests off-centre in even light | the two LDRs differ: calibrate (section 10) |
| Jitters at rest | cover the LDRs from flicker lamps (mains LED lamps flicker), use `MAINS_HZ` 50 or 60 to match your supply |
| Buzzes at the ends | narrow `PAN_MIN_PULSE`/`PAN_MAX_PULSE` to 1100 / 1900 |
| Board resets when servo moves | servo current: see caution 5 |
| Nothing at all, no PWR LED | USB cable is charge-only, or a 3.3/5 V short: unplug and recheck wiring |

---
## 10. Tuning and calibration

Settings are `#define` lines at the top of `Core/Src/main.c`. After changing, upload again.

| Setting | Default | Meaning |
|---|---|---|
| `PAN_DIR` | +1 | flip to -1 if it turns away from light |
| `PAN_MIN_PULSE` / `PAN_MAX_PULSE` | 900 / 2100 | servo travel limits in microseconds; 1100 / 1900 is gentler |
| `NIGHT_ENTER_ADC_SUM` / `NIGHT_EXIT_ADC_SUM` | 400 / 900 | darkness thresholds; lower them for a dim room |
| `MAINS_HZ` | 50 | set 60 in 60 Hz countries |
| `BOOT_MODE` | `MODE_DEMO` | `DEMO` = moves every 0.3 s |

**Calibrate for LDR mismatch:** put the plate under even, diffuse light (an overcast window, a lamp far away so both LDRs get the same light). Touch a jumper from **A4** to **GND** and **hold it for more than 2 seconds** (or hold a button). The tracker stores a gain for each LDR in flash and keeps it after power-off.

---
## 11. Optional: see data on the laptop

Add a 3.3 V USB-to-TTL adapter: its **RX to Blue Pill A9**, its **TX to A10**, its **GND to GND**. Leave its VCC unconnected. Open `tools/helion_dashboard.html` in Chrome or Edge and press Connect (or run `python tools/helion_monitor.py COM5`). For the rig build the power cards show zero because there are no INA219 modules; the servo position, light sum and error are real.

---
## 12. What the code does (short)

- `Core/Src/main.c` reads both LDRs (PA0, PA1) with oversampling, computes a normalised left/right error, and moves the servo (PA6) in small steps toward balance, with a deadband and a runaway fault.
- At power-up it centres the servo, then sweeps once to find the brightest direction. In darkness it parks. Faults hold the servo.
- `-DHAVE_INA219=0` (environment `rig`) removes all I2C and power-sensor code paths; the serial telemetry stays available on PA9/PA10.
- Files you need: `platformio.ini`, `STM32F103C8TX_FLASH.ld`, `Core/Src/main.c`, `Core/Src/stm32f1xx_it.c`, `Core/Inc/main.h`, `Core/Inc/stm32f1xx_hal_conf.h`.

## 13. Later: the full project

Add the INA219 modules, LM2596, panels and USB-TTL adapter following `README.md` and flash with `pio run -e helion -t upload` (the default). In that build the Blue Pill's micro-USB must stay **unplugged**, because the LM2596 powers the 5 V net.
