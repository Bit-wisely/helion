"""Generates docs/wiring_diagram.png and .svg   (python3 make_wiring_diagram.py; needs matplotlib).
Same-named flags are the same net. All GND flags meet at ONE star-ground point."""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Polygon

C = dict(r="#c62828", o="#e07b12", k="#222222", y="#a67c00", b="#1f6fd1", g="#1f9a4d", u="#6b7b86")
W, H = 1800, 1240
fig = plt.figure(figsize=(W / 100, H / 100), dpi=100)
ax = fig.add_axes([0, 0, 1, 1]); ax.set_xlim(0, W); ax.set_ylim(H, 0); ax.axis("off")


def box(x, y, w, h, title, lines=(), fc="#ffffff", ec="#222", lw=1.6, fs=12, ty=16):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0,rounding_size=6", fc=fc, ec=ec, lw=lw, zorder=3))
    ax.text(x + 8, y + ty, title, fontsize=fs, fontweight="bold", va="center", zorder=4)
    for i, t in enumerate(lines):
        ax.text(x + 8, y + ty + 18 + i * 15, t, fontsize=10, color="#444", va="center", zorder=4)


def wire(pts, c, lw=2.4, z=2):
    ax.plot([p[0] for p in pts], [p[1] for p in pts], color=C[c], lw=lw, zorder=z, solid_capstyle="round", solid_joinstyle="round")


def dot(x, y, c="k"):
    ax.plot([x], [y], "o", color=C[c], ms=6, zorder=5)


def label(x, y, t, fs=9.5, c="#555", **kw):
    ax.text(x, y, t, fontsize=fs, color=c, va="center", **kw)


def flag(x, y, name, c="k", up=False):
    """net flag whose wire starts at (x, y); GND is the usual ground symbol."""
    if name == "GND":
        d = -1 if up else 1
        wire([(x, y), (x, y + d * 8)], "k")
        for i, w in enumerate((16, 10, 4)):
            ax.plot([x - w, x + w], [y + d * (8 + i * 5)] * 2, color=C["k"], lw=2.2, zorder=3)
        label(x + 22, y + d * 14, "GND", 9)
        return
    d = -1 if up else 1
    wire([(x, y), (x, y + d * 12)], c)
    ax.add_patch(Polygon([(x - 22, y + d * 12), (x + 22, y + d * 12), (x + 22, y + d * 30), (x - 22, y + d * 30)],
                         closed=True, fc="#fff", ec=C[c], lw=2, zorder=3))
    ax.text(x, y + d * 21, name, ha="center", va="center", fontsize=10, color=C[c], fontweight="bold", zorder=4)


# ------------------------------------------------------------------ title
ax.text(30, 26, "Helion: complete physical connection diagram (laptop display revision)", fontsize=19, fontweight="bold", va="center")
label(30, 54, "No OLED and no status LED: all readings leave USART1 (PA9/PA10) through a USB-TTL adapter to the laptop.   "
              "Same-named flags are one net.   Every GND flag goes to ONE star-ground point.", 11, "#444")

# ------------------------------------------------------------------ power path
label(40, 100, "POWER PATH   (switch the + leg only, never the ground)", 11, "#555", fontweight="bold")
Y0, PH = 116, 78
chain = [(40, 110, "J1 jack", ["9 V 2 A", "centre +"]), (185, 100, "SW1", ["on/off", "+ leg only"]), (320, 90, "F1", ["2 A fuse"]),
         (445, 120, "D1", ["SS34 / 1N5822", "3 A Schottky"]),
         (600, 190, "INA219 #2", ["0x41 (bridge A0), system", "VIN+ in, VIN- out", "VCC 3V3, SCL, SDA"]),
         (850, 190, "LM2596 buck", ["IN+ IN- OUT+ OUT-", "trim OUT to 5.00 V", "with nothing attached"])]
for x, w, t, l in chain:
    box(x, Y0, w, PH, t, l)
for (x1, w1, *_), (x2, *_r) in zip(chain[:-1], chain[1:]):
    wire([(x1 + w1, Y0 + 39), (x2, Y0 + 39)], "r")
flag(95, Y0 + PH, "GND"); label(40, Y0 + PH + 38, "J1 minus", 9)
dot(582, Y0 + 39, "r"); wire([(582, Y0 + 39), (582, Y0 + 100)], "r")
box(527, Y0 + 100, 110, 30, "TVS1 SMBJ12A", [], fs=9.5); flag(582, Y0 + 130, "GND")
flag(945, Y0 + PH, "GND")
wire([(1040, Y0 + 39), (1068, Y0 + 39)], "r"); label(1046, Y0 + 26, "OUT+", 9); flag(1068, Y0 + 39, "5V", "r")
flag(660, Y0 + PH, "3V3", "o"); flag(715, Y0 + PH, "SCL", "b"); flag(770, Y0 + PH, "SDA", "g")

# ------------------------------------------------------------------ decoupling / protection on the rails
label(170, 222, "RAIL PROTECTION / DECOUPLING", 9, "#555", fontweight="bold")
for x, w, t, net, c in ((170, 135, "C1 1000uF+100nF", "5V", "r"), (320, 130, "TVS2 SMBJ5.0A", "5V", "r"), (465, 135, "C2 100uF+100nF", "3V3", "o")):
    box(x, 290, w, 30, t, [], fs=8.5)
    flag(x + w // 2, 290, net, c, up=True); flag(x + w // 2, 320, "GND")

# ------------------------------------------------------------------ MCU
MX, MY, MW, MH = 680, 330, 280, 640
box(MX, MY, MW, MH, "STM32F103C8T6 Blue Pill", ["8 MHz crystal, 72 MHz core", "micro-USB: leave UNPLUGGED", "PC13 LED, PA2, PA3, PA7: unused"],
    fc="#f4f9fb", lw=2.2, fs=13, ty=44)
for px, nm, c in ((750, "5V", "r"), (820, "GND", "k"), (890, "3V3", "o")):
    ax.text(px, MY + 14, nm, fontsize=10, ha="center", fontweight="bold", zorder=4)
    wire([(px, MY), (px, MY - 14)], c)
    flag(px, MY - 14, nm, c, up=True)

LY = {"PA0": 480, "PA1": 610, "PA4": 740, "PA6": 870}
RY = {"PB6  SCL": 480, "PB7  SDA": 530, "PA9  TX": 740, "PA10  RX": 790}
for nm, y in LY.items():
    ax.text(MX + 10, y, nm, fontsize=11, va="center", fontweight="bold", zorder=4); dot(MX, y)
for nm, y in RY.items():
    ax.text(MX + MW - 10, y, nm, fontsize=11, va="center", ha="right", fontweight="bold", zorder=4); dot(MX + MW, y)
BOT = MY + MH
for x, nm in ((740, "SWDIO"), (810, "SWCLK"), (880, "GND"), (940, "NRST")):
    ax.text(x, BOT - 14, nm, fontsize=9, ha="center", fontweight="bold", zorder=4)

# ------------------------------------------------------------------ light sensors
label(40, 378, "LIGHT SENSORS   (build 2 identical dividers)", 11, "#555", fontweight="bold")
for name, y in (("LEFT", LY["PA0"]), ("RIGHT", LY["PA1"])):
    flag(60, y - 44, "3V3", "o"); wire([(60, y - 14), (60, y), (110, y)], "o")
    box(110, y - 24, 110, 48, f"LDR {name}", ["GL5528"], fs=11)
    wire([(220, y), (MX, y)], "y")
    for nx, val in ((300, "4.7 k"), (400, "10 nF")):
        dot(nx, y, "y"); wire([(nx, y), (nx, y + 24)], "y")
        box(nx - 30, y + 24, 60, 30, val, [], fs=10); flag(nx, y + 54, "GND")
    label(470, y - 12, "to " + ("PA0" if name == "LEFT" else "PA1"), 9.5)
label(40, 712, "Both LDRs sit under one panel edge with a 35-40 mm centre baffle between them.", 9.5)

# button
box(330, 716, 120, 48, "Button", ["PA4 to GND"], fs=11)
wire([(450, 740), (MX, 740)], "y"); wire([(330, 740), (270, 740)], "k"); flag(270, 740, "GND")
label(40, 736, "internal pull-up:", 9.5); label(40, 752, "pressed = low", 9.5)

# servo
label(250, 812, "PAN SERVO", 11, "#555", fontweight="bold")
box(60, 830, 160, 120, "MG90S servo", ["red    V+     -> 5V", "orange signal", "brown  GND"], fs=12)
flag(110, 830, "5V", "r", up=True); flag(180, 950, "GND")
wire([(220, 870), (270, 870)], "y"); box(270, 852, 90, 36, "330 ohm", [], fs=10)
wire([(360, 870), (MX, 870)], "y"); dot(430, 870, "y")
wire([(430, 870), (430, 900)], "y"); box(400, 900, 60, 30, "10 k", [], fs=10); flag(430, 930, "GND")
label(475, 915, "pull-down to GND", 9.5)

# ------------------------------------------------------------------ I2C bus + INA219 #1 / #3
label(1090, 100, "PANEL MEASUREMENT   (2 identical chains)", 11, "#555", fontweight="bold")
SCLX, SDAX = 1010, 1040
for top, nm, addr, panel in ((150, "INA219 #1", "0x40 (A0, A1 open)", "TRACKED panel"), (340, "INA219 #3", "0x44 (bridge A1)", "FIXED panel")):
    box(1100, top, 210, 110, nm, [addr, "VCC 3V3, GND", "0.1 ohm shunt on module"], fs=12)
    wire([(SCLX, top + 80), (1100, top + 80)], "b"); wire([(SDAX, top + 96), (1100, top + 96)], "g")
    dot(SCLX, top + 80, "b"); dot(SDAX, top + 96, "g")
    flag(1150, top + 110, "3V3", "o"); flag(1260, top + 110, "GND")
    wire([(1310, top + 30), (1560, top + 30)], "r"); label(1318, top + 18, "VIN+", 9.5, "#000", fontweight="bold")
    label(1318, top + 54, "VIN-", 9.5, "#000", fontweight="bold")
    wire([(1310, top + 76), (1340, top + 76)], "r"); box(1340, top + 58, 80, 36, "10 ohm", [], fs=10)
    label(1330, top + 108, "5 W, on standoffs", 8.5)
    wire([(1420, top + 76), (1450, top + 76)], "r"); flag(1450, top + 76, "GND")
    box(1560, top + 6, 190, 94, panel, ["6 V 3 W solar panel", "PV+ -> VIN+", "PV- -> GND"], fs=11, fc="#fdfaf0")
    wire([(1560, top + 84), (1520, top + 84)], "k"); flag(1520, top + 84, "GND")
wire([(MX + MW, 480), (SCLX, 480), (SCLX, 230)], "b"); wire([(MX + MW, 530), (SDAX, 530), (SDAX, 246)], "g")
wire([(SCLX, 420), (SCLX, 480)], "b"); wire([(SDAX, 436), (SDAX, 530)], "g")
wire([(SCLX, 230), (SCLX, 420)], "b"); wire([(SDAX, 246), (SDAX, 436)], "g")
label(SCLX - 12, 500, "SCL", 9.5, C["b"], ha="right", fontweight="bold"); label(SDAX + 8, 550, "SDA", 9.5, C["g"], fontweight="bold")
label(1066, 340, "shared I2C bus: no extra pull-ups (measure first)", 9, rotation=90)

# ------------------------------------------------------------------ UART to laptop
label(1000, 660, "LAPTOP DISPLAY LINK   (replaces the OLED and the LED)", 11, "#555", fontweight="bold")
wire([(MX + MW, 740), (1100, 740)], "y"); wire([(MX + MW, 790), (1100, 790)], "y")
label(1040, 728, "TX", 9.5, C["y"], ha="center", fontweight="bold"); label(1040, 778, "RX", 9.5, C["y"], ha="center", fontweight="bold")
box(1100, 700, 240, 170, "USB-TTL adapter", ["CP2102 / CH340 / FT232", "set to 3.3 V logic", "RX   <-  PA9 (TX)", "TX   ->  PA10 (RX)", "GND  ->  star GND", "VCC  NOT connected"],
    fs=12, fc="#eef7ee")
flag(1220, 870, "GND")
wire([(1340, 785), (1480, 785)], "u", lw=3.4); label(1360, 771, "USB cable", 10, C["u"], fontweight="bold")
box(1480, 700, 300, 170, "LAPTOP", ["Chrome / Edge dashboard:", "tools/helion_dashboard.html", "or: tools/helion_monitor.py", "115200 baud, 8N1", "commands: MODE CAL CLEAR STATUS"],
    fc="#e6f3f7", ec="#0b6e8a", lw=2.4, fs=13)
label(1100, 912, "Do NOT connect the adapter's 5 V or 3.3 V pin:", 10, C["r"]); label(1100, 930, "the tracker is powered only by the LM2596.", 10, C["r"])

# ------------------------------------------------------------------ ST-Link
SLX, SLY = 680, 1050
box(SLX, SLY, 340, 100, "ST-Link V2  (flashing only)", ["SWDIO, SWCLK, GND, NRST (to the R pin)", "its 3.3 V wire: NOT connected"], fs=12)
for x, nm, c in ((740, "SWDIO", "y"), (810, "SWCLK", "y"), (880, "GND", "k"), (940, "NRST", "y")):
    wire([(x, BOT), (x, SLY)], c); label(x + 6, BOT + 40 + (14 if nm == "NRST" else 0), nm, 9, "#333")
wire([(1020, 1100), (1615, 1100), (1615, 870)], "u", lw=3.4); label(1030, 1086, "USB cable to the laptop", 10, C["u"], fontweight="bold")

# ------------------------------------------------------------------ legend
lx, ly = 40, 1020
ax.text(lx, ly, "Wire colours", fontsize=11, fontweight="bold", va="center")
for i, (k, t) in enumerate((("r", "9 V path and 5 V"), ("o", "3.3 V"), ("k", "GND (star ground)"), ("y", "signal"), ("b", "I2C SCL"), ("g", "I2C SDA"), ("u", "USB cable"))):
    ax.plot([lx, lx + 40], [ly + 26 + i * 20] * 2, color=C[k], lw=3); ax.text(lx + 50, ly + 26 + i * 20, t, fontsize=10, va="center")

fig.savefig("wiring_diagram.png", dpi=100)
fig.savefig("wiring_diagram.svg")
print("written")
