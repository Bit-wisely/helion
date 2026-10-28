"""docs/rig_wiring_diagram.png: wiring for the bare cardboard rig (2 LDR + SG90 + Blue Pill + ST-Link).
Pin names are the ones printed on the Blue Pill in the user's photo (USB socket on the left)."""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Polygon, Rectangle

C = dict(r="#c62828", o="#e07b12", k="#222222", y="#a67c00", u="#6b7b86", p="#7b3fa0")
W, H = 1700, 1120
fig = plt.figure(figsize=(W / 100, H / 100), dpi=100)
ax = fig.add_axes([0, 0, 1, 1]); ax.set_xlim(0, W); ax.set_ylim(H, 0); ax.axis("off")

def box(x, y, w, h, title, lines=(), fc="#fff", ec="#222", lw=1.6, fs=12, ty=16):
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
    d = -1 if up else 1
    if name == "GND":
        wire([(x, y), (x, y + d * 8)], "k")
        for i, w in enumerate((16, 10, 4)):
            ax.plot([x - w, x + w], [y + d * (8 + i * 5)] * 2, color=C["k"], lw=2.2, zorder=3)
        label(x + 22, y + d * 14, "GND", 9)
        return
    wire([(x, y), (x, y + d * 12)], c)
    ax.add_patch(Polygon([(x - 24, y + d * 12), (x + 24, y + d * 12), (x + 24, y + d * 30), (x - 24, y + d * 30)],
                         closed=True, fc="#fff", ec=C[c], lw=2, zorder=3))
    ax.text(x, y + d * 21, name, ha="center", va="center", fontsize=10, color=C[c], fontweight="bold", zorder=4)

ax.text(30, 28, "Cardboard light-follower rig: connection diagram", fontsize=19, fontweight="bold", va="center")
label(30, 56, "Parts: Blue Pill + ST-Link V2 + SG90 servo + 2 LDR + 2 resistors (4.7 k to 10 k) + breadboard + jumper wires.   "
              "Same-named flags are one net (use the breadboard + and - rails).", 11, "#444")

# ---------------- Blue Pill
BX0, BX1, BY0, BY1 = 380, 1190, 430, 570
TOP = ["G", "G", "3.3", "R", "B11", "B10", "B1", "B0", "A7", "A6", "A5", "A4", "A3", "A2", "A1", "A0", "C15", "C14", "C13", "VB"]
BOT = ["B12", "B13", "B14", "B15", "A8", "A9", "A10", "A11", "A12", "A15", "B3", "B4", "B5", "B6", "B7", "B8", "B9", "5V", "G", "3.3"]
px = [405 + i * 40 for i in range(20)]
box(BX0, BY0, BX1 - BX0, BY1 - BY0, "", [], fc="#dfeaf7", ec="#1c3f73", lw=2.4)
ax.text((BX0 + BX1) / 2, 500, "BLUE PILL  STM32F103C8T6   (top view, as in your photo)", ha="center", fontsize=13, fontweight="bold", zorder=4)
USED_T = {"3.3": "o", "R": "p", "A0": "y", "A1": "y", "A4": "y", "A6": "y"}
for i, (t, b) in enumerate(zip(TOP, BOT)):
    ut = USED_T.get(t) if not (t == "G" and i == 1) else "k"
    if t == "G" and i == 0: ut = None
    ax.text(px[i], 452, t, ha="center", fontsize=9.5, fontweight="bold" if ut else "normal", zorder=4, color="#000" if ut else "#556")
    ax.plot([px[i]], [436], "o", ms=7, color=C[ut] if ut else "#99a", zorder=4)
    ub = {"5V": "r", "G": "k"}.get(b) if i in (17, 18) else None
    ax.text(px[i], 548, b, ha="center", fontsize=9.5, fontweight="bold" if ub else "normal", zorder=4, color="#000" if ub else "#556")
    ax.plot([px[i]], [564], "o", ms=7, color=C[ub] if ub else "#99a", zorder=4)
label(BX0 + 6, 520, "pins used: 3.3  R  A0  A1  A4  A6 (top row)    5V  G (bottom row).   A5 A7 B0.. stay empty", 9, "#33425a")
# USB and SWD
box(330, 470, 50, 60, "", [], fc="#cfd6dd"); ax.text(355, 500, "USB", ha="center", fontsize=9, rotation=90, zorder=4, va="center")
box(1190, 470, 60, 60, "", [], fc="#f6e9b8"); ax.text(1220, 500, "SWD\n4-pin", ha="center", fontsize=9, zorder=4, va="center", fontweight="bold")

# ---------------- LDR dividers
def ldr_chain(xn, name, pin_x, branch_dir, pin_name, jog_y):
    flag(xn, 112, "3V3", "o", up=False)                    # flag hangs from y=112 downward
    wire([(xn, 142), (xn, 160)], "o")
    box(xn - 42, 160, 84, 56, name, ["GL5528"], fs=11)
    wire([(xn, 216), (xn, 260)], "y"); dot(xn, 260, "y")
    wire([(xn, 260), (xn, jog_y), (pin_x, jog_y), (pin_x, 436)], "y")
    bx = xn + branch_dir * 40
    wire([(xn, 260), (bx, 260)], "y")
    bw = 76; x0 = bx if branch_dir > 0 else bx - bw
    box(x0, 244, bw, 32, "4.7 k", [], fs=10)
    ex = x0 + bw if branch_dir > 0 else x0
    wire([(ex, 260), (ex + branch_dir * 34, 260)], "k"); flag(ex + branch_dir * 34, 260, "GND")
    label(pin_x + 8, 424, "", 9)

label(820, 100, "LIGHT SENSORS", 11, "#555", fontweight="bold")
ldr_chain(1110, "LDR LEFT", px[15], +1, "A0", 380)
ldr_chain(900, "LDR RIGHT", px[14], +1, "A1", 410)
label(1015, 369, "node -> A0", 9.5, C["y"], fontweight="bold"); label(880, 399, "node -> A1", 9.5, C["y"], fontweight="bold")

# ---------------- optional button on A4
wire([(px[11], 436), (px[11], 330)], "y"); box(px[11] - 40, 290, 80, 40, "Button", ["(optional)"], fs=10)
wire([(px[11], 290), (px[11], 262)], "k"); flag(px[11], 262, "GND", up=True)
label(px[11] - 30, 345, "A4", 9.5, C["y"], fontweight="bold")

# ---------------- servo
label(380, 100, "SG90 SERVO", 11, "#555", fontweight="bold")
box(380, 120, 220, 150, "SG90 micro servo", ["red    V+     -> 5V", "orange signal -> A6", "brown  GND"], fs=12)
wire([(600, 160), (650, 160)], "r"); wire([(650, 160), (650, 126)], "r")
ax.add_patch(Polygon([(626, 126), (674, 126), (674, 108), (626, 108)], closed=True, fc="#fff", ec=C["r"], lw=2, zorder=3))
ax.text(650, 117, "5V", ha="center", va="center", fontsize=10, color=C["r"], fontweight="bold", zorder=4)
wire([(600, 200), (px[9], 200), (px[9], 436)], "y"); label(610, 190, "signal", 9, C["y"], fontweight="bold")
wire([(600, 240), (650, 240)], "k"); flag(650, 240, "GND")
label(px[9] + 6, 330, "A6", 9.5, C["y"], fontweight="bold")

# ---------------- rails flags at the Blue Pill
wire([(px[2], 436), (px[2], 360)], "o"); flag(px[2], 360, "3V3", "o", up=True)
wire([(px[1], 436), (px[1], 400)], "k")
for i, w in enumerate((14, 9, 4)):
    ax.plot([px[1] - w, px[1] + w], [400 - 4 - i * 5] * 2, color=C["k"], lw=2.2, zorder=3)
label(px[1] - 22, 380, "GND", 9, ha="right")
wire([(px[3], 436), (px[3], 395)], "p"); flag(px[3], 395, "NRST", "p", up=True)
wire([(px[17], 564), (px[17], 600)], "r"); flag(px[17], 600, "5V", "r")
wire([(px[18], 564), (px[18], 600)], "k"); flag(px[18], 600, "GND")

# ---------------- ST-Link
SX, SY = 1330, 450
box(SX, SY, 340, 200, "ST-Link V2 (blue clone)", ["its 10-pin socket, pins as printed on it:", "pin 1  RST     -> Blue Pill R (flag NRST)", "pin 2  SWCLK  -> Blue Pill SWCLK",
                                                 "pin 4  SWDIO  -> Blue Pill SWDIO", "pin 6  GND     -> Blue Pill GND", "pins 3, 5, 7, 8, 9, 10:  NOT connected"], fs=12)
wire([(1250, 480), (SX, 480)], "y"); wire([(1250, 500), (SX, 500)], "y"); wire([(1250, 520), (SX, 520)], "k")
label(1262, 472, "SWCLK", 9, C["y"], fontweight="bold"); label(1262, 492, "SWDIO", 9, C["y"], fontweight="bold"); label(1262, 536, "GND", 9, "#000", fontweight="bold")
wire([(SX + 120, SY), (SX + 120, 400)], "p"); ax.add_patch(Polygon([(SX + 96, 400), (SX + 144, 400), (SX + 144, 382), (SX + 96, 382)], closed=True, fc="#fff", ec=C["p"], lw=2, zorder=3))
ax.text(SX + 120, 391, "NRST", ha="center", va="center", fontsize=10, color=C["p"], fontweight="bold", zorder=4)
label(1330, 672, "Do NOT use the ST-Link 3.3 V or 5 V pins:", 10, C["r"]); label(1330, 690, "the board is powered from its own USB cable.", 10, C["r"])

# ---------------- laptop
box(600, 700, 470, 110, "LAPTOP", ["USB port A: Blue Pill micro-USB (power for board + servo)", "USB port B: ST-Link (flashing)"], fc="#e6f3f7", ec="#0b6e8a", lw=2.4, fs=13)
wire([(330, 500), (300, 500), (300, 755), (600, 755)], "u", lw=3.4); label(310, 740, "USB cable 1 (micro-USB)", 10, C["u"], fontweight="bold")
wire([(1495, SY + 200), (1495, 755), (1070, 755)], "u", lw=3.4); label(1090, 740, "USB cable 2 (ST-Link)", 10, C["u"], fontweight="bold")

# ---------------- cardboard head sketch (top view)
label(150, 880, "CARDBOARD HEAD  (top view)", 11, "#555", fontweight="bold")
ax.add_patch(Rectangle((60, 905), 360, 190, fc="#f1dcb5", ec="#8a6a35", lw=2, zorder=2)); label(70, 1082, "cardboard plate, glued to the servo horn", 9)
ax.add_patch(Rectangle((236, 905), 8, 160, fc="#8a6a35", ec="#8a6a35", zorder=3)); label(250, 920, "fin 4 cm tall", 9)
for cx, nm in ((150, "LDR LEFT  -> A0"), (330, "LDR RIGHT -> A1")):
    ax.add_patch(plt.Circle((cx, 985), 20, fc="#c9585a", ec="#222", lw=2, zorder=4)); label(cx - 55, 1030, nm, 9)
label(20, 858, "LIGHT", 10, "#a67c00", fontweight="bold")
ax.annotate("", xy=(105, 940), xytext=(20, 886), arrowprops=dict(arrowstyle="->", color="#e0a800", lw=3))
label(250, 1100, "", 9)
box(480, 960, 150, 90, "SG90 body", ["fixed to the base", "horn = rotating arm"], fs=11)
ax.annotate("", xy=(480, 1005), xytext=(420, 1005), arrowprops=dict(arrowstyle="-", color="#8a6a35", lw=5))
label(660, 960, "Left / right = as seen from behind the plate, looking at the light.", 10, "#333")
label(660, 980, "Both LDRs face the same way, ~4 cm apart; the fin shades one when off-angle.", 10, "#333")
label(660, 1000, "If the servo turns AWAY from the light: swap the A0/A1 wires", 10, "#333")
label(660, 1018, "or flip PAN_DIR in main.c.  Keep the cable slack so it can swing.", 10, "#333")

# legend
lx, ly = 1450, 880
ax.text(lx, ly, "Wire colours", fontsize=11, fontweight="bold", va="center")
for i, (k, t) in enumerate((("r", "5 V (USB)"), ("o", "3.3 V"), ("k", "GND"), ("y", "signal"), ("p", "NRST (optional)"), ("u", "USB cable"))):
    ax.plot([lx, lx + 40], [ly + 26 + i * 20] * 2, color=C[k], lw=3); ax.text(lx + 50, ly + 26 + i * 20, t, fontsize=10, va="center")

fig.savefig("rig_wiring_diagram.png", dpi=100)
print("ok")
