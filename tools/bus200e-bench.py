#!/usr/bin/env python3
"""200e preset-bus acceptance test for the MARF.

Drives a Studio H WPM over HTTP, captures the bus on a Saleae, decodes it with
the built-in I2C analyzer and reports how many command frames survived intact.
This is the acceptance test for the bus slave: the number to beat is the
control, taken with the MARF on stock firmware (electrically passive), which
measured 24/24 complete frames with zero dead gaps on 2026-09-06.

  venv:     ~/.venvs/logic2   (pip install logic2-automation)
  analyzer: Logic 2 automation enabled, port 10430, ch0 = SCL, ch1 = SDA
  WPM:      reachable at the base URL below (the WPM's own AP answers at
            192.168.0.1, which a typical house router ALSO answers to -- go
            through a host that is actually on the WPM's network)

Uses /remoteenable and /remotedisable only: those put a real general-call frame
on the wire and change no module's state. Do NOT switch this to
/recallpreset -- that reloads every 200e module in the case.

  python3 tools/bus200e-bench.py [--frames 24] [--base URL]
"""
import argparse, csv, os, subprocess, sys, tempfile, threading, time

CONTROL = "MARF passive (stock firmware): 24/24 complete, 0 dead gaps, 595-616 us"


def drive(base, n, delay):
    time.sleep(1.2)
    for i in range(n):
        cmd = "remoteenable" if i % 2 == 0 else "remotedisable"
        subprocess.run(["curl", "-s", "-m", "8", "-o", "/dev/null", base + cmd],
                       check=False)
        time.sleep(delay)


def capture(out, n, base, delay):
    from saleae import automation
    from saleae.automation import (LogicDeviceConfiguration, CaptureConfiguration,
                                   TimedCaptureMode)
    dur = 1.5 + n * delay + 1.0
    with automation.Manager.connect(port=10430) as m:
        dev = [d for d in m.get_devices() if not d.is_simulation][0]
        cfg = LogicDeviceConfiguration(enabled_digital_channels=[0, 1],
                                       digital_sample_rate=12_500_000,
                                       digital_threshold_volts=1.2)
        t = threading.Thread(target=drive, args=(base, n, delay))
        t.start()
        with m.start_capture(device_id=dev.device_id, device_configuration=cfg,
                             capture_configuration=CaptureConfiguration(
                                 capture_mode=TimedCaptureMode(duration_seconds=dur))) as cap:
            an = cap.add_analyzer('I2C', label='bus', settings={'SDA': 1, 'SCL': 0})
            cap.wait()
            cap.export_raw_data_csv(directory=out, digital_channels=[0, 1])
            cap.export_data_table(filepath=os.path.join(out, "i2c.csv"), analyzers=[an])
        t.join()


def report(out):
    rows = list(csv.DictReader(open(os.path.join(out, "i2c.csv"))))
    dig = [(float(r[0]), int(float(r[1])), int(float(r[2])))
           for r in list(csv.reader(open(os.path.join(out, "digital.csv"))))[1:]]
    txn, cur = [], None
    for r in rows:
        ty, st = r["type"], float(r["start_time"])
        du = float(r["duration"] or 0)
        if ty == "start":
            cur = {"start": st, "n": 0, "stop": None, "nack": 0}
            txn.append(cur)
        elif cur is None:
            continue
        elif ty in ("address", "data"):
            if ty == "data":
                cur["n"] += 1
            if r["ack"] != "true":
                cur["nack"] += 1
        elif ty == "stop":
            cur["stop"] = st + du
    ok = [x for x in txn if x["n"] == 5 and x["nack"] == 0 and x["stop"]]
    spans = [(x["stop"] - x["start"]) * 1e6 for x in txn if x["stop"]]
    gaps = []
    for x in txn:
        if not x["stop"]:
            continue
        w = [e for e in dig if x["start"] - 1e-5 <= e[0] <= x["stop"] + 1e-5]
        gaps += [(w[i][0] - w[i - 1][0]) * 1e6 for i in range(1, len(w))
                 if w[i][0] - w[i - 1][0] > 50e-6]
    print("transactions            : %d" % len(txn))
    print("complete, all ACKed     : %d  (%.0f%%)"
          % (len(ok), 100.0 * len(ok) / max(len(txn), 1)))
    print("truncated               : %d" % len([x for x in txn if x["n"] != 5]))
    print("dead gaps >50us         : %d" % len(gaps))
    if spans:
        print("transaction span        : %.0f - %.0f us" % (min(spans), max(spans)))
    print("\ncontrol to beat: " + CONTROL)
    return len(ok) == len(txn) and not gaps


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames", type=int, default=24)
    ap.add_argument("--delay", type=float, default=0.4)
    ap.add_argument("--base", default="http://192.168.0.122:8080/wpm/")
    ap.add_argument("--keep", metavar="DIR", help="keep the CSVs here")
    a = ap.parse_args()
    out = a.keep or tempfile.mkdtemp(prefix="bus200e-")
    os.makedirs(out, exist_ok=True)
    capture(out, a.frames, a.base, a.delay)
    good = report(out)
    if not a.keep:
        for f in ("i2c.csv", "digital.csv"):
            try:
                os.remove(os.path.join(out, f))
            except OSError:
                pass
    return 0 if good else 1


if __name__ == "__main__":
    sys.exit(main())
