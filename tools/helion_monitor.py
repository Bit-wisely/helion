#!/usr/bin/env python3
"""Helion serial monitor: live text view of the tracker on the laptop, with CSV logging.

    pip install pyserial
    python helion_monitor.py --list                 # show serial ports
    python helion_monitor.py COM5                   # Windows
    python helion_monitor.py /dev/ttyUSB0 --csv log.csv
    python helion_monitor.py --replay sample.txt    # parse a saved capture, no hardware

While running, type MODE, CAL, CLEAR or STATUS + Enter to send a command.
"""
import argparse, csv, sys, threading, time

KEYS = ["t", "mode", "st", "flt", "az", "pan", "sum", "lim", "ina", "tv", "ti", "tp",
        "fv", "fi", "fp", "sv", "si", "sp", "gain", "et", "ef"]


def parse_line(line):
    """'$HEL,k=v,...*HH' -> dict, or None if it is not a valid telemetry line."""
    line = line.strip()
    if not line.startswith("$HEL,"):
        return None
    star = line.rfind("*")
    if star < 0 or len(line) - star != 3:
        return None
    body = line[1:star]
    x = 0
    for ch in body:
        x ^= ord(ch)
    try:
        if x != int(line[star + 1:], 16):
            return None
    except ValueError:
        return None
    out = {}
    for kv in body.split(",")[1:]:
        k, sep, v = kv.partition("=")
        if not sep:
            return None
        out[k] = int(v) if v.lstrip("-").isdigit() else v
    return out if all(k in out for k in KEYS) else None


def describe(o):
    flt = [n for b, n in ((1, "pan runaway"), (2, "LDR")) if o["flt"] & b]
    ina = [n for b, n in ((1, "tracked"), (2, "system"), (4, "fixed")) if not o["ina"] & b]
    gain = "n/a" if o["gain"] == "na" else "%+.1f %%" % (o["gain"] / 10)
    return (
        "[%6ds] %-5s %-6s%s | tracked %7.1f mW (%.2f V %.1f mA) | fixed %7.1f mW | gain %s | "
        "energy T %.1f F %.1f mWh | sys %.2f W | pan %d us err %+.1f %%%s%s"
        % (o["t"], o["mode"], o["st"], (" [" + ", ".join(flt) + "]") if flt else "",
           o["tp"], o["tv"] / 1000, o["ti"] / 10, o["fp"], gain, o["et"] / 10, o["ef"] / 10,
           o["sp"] / 1000, o["pan"], o["az"] / 10,
           " LIMIT" if o["lim"] else "", ("  OFFLINE: " + ", ".join(ina)) if ina else ""))


def handle(line, writer):
    line = line.strip()
    if not line:
        return
    if line.startswith("$"):
        o = parse_line(line)
        if o is None:
            print("! corrupt line ignored")
            return
        print(describe(o))
        if writer:
            writer[0].writerow([time.strftime("%Y-%m-%dT%H:%M:%S")] + [o[k] for k in KEYS])
            writer[1].flush()
    elif line.startswith("#"):
        print(line)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", nargs="?", help="serial port, e.g. COM5 or /dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--csv", help="append every telemetry line to this CSV file")
    ap.add_argument("--list", action="store_true", help="list serial ports and exit")
    ap.add_argument("--replay", help="parse a text capture instead of a serial port")
    a = ap.parse_args()

    writer = None
    if a.csv:
        f = open(a.csv, "a", newline="")
        w = csv.writer(f)
        if f.tell() == 0:
            w.writerow(["pc_time"] + KEYS)
        writer = (w, f)

    if a.replay:
        with open(a.replay) as fh:
            for ln in fh:
                handle(ln, writer)
        return

    try:
        import serial
        from serial.tools import list_ports
    except ImportError:
        sys.exit("pyserial is missing: pip install pyserial")
    if a.list or not a.port:
        for p in list_ports.comports():
            print(p.device, "-", p.description)
        return

    ser = serial.Serial(a.port, a.baud, timeout=0.2)
    print("Connected to %s at %d baud. Type MODE, CAL, CLEAR or STATUS + Enter. Ctrl+C quits." % (a.port, a.baud))

    def typer():
        for cmd in sys.stdin:
            cmd = cmd.strip().upper()
            if cmd:
                ser.write((cmd + "\n").encode())
    threading.Thread(target=typer, daemon=True).start()

    buf = b""
    try:
        while True:
            buf += ser.read(256)
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                handle(raw.decode("ascii", "replace"), writer)
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()


if __name__ == "__main__":
    main()
