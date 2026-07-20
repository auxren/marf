#!/usr/bin/env python3
"""
EASEL WEASEL — the stupidly-simple, cross-platform firmware flasher.

A goggle-wearing, headphone-clad, banana-cable-patching weasel flashes your
device for you. Double-click the launcher for your OS, or run:  python3 flash.py

Maintainers: drop this folder in your repo, put your .uf2 in ./firmware/
(or set it in flash.config.json), ship it. End users don't touch anything.

No dependencies. Pure standard library. Python 3.7+.
"""

import json
import os
import random
import shlex
import shutil
import string
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
IS_WIN = os.name == "nt"
IS_MAC = sys.platform == "darwin"

# --------------------------------------------------------------------------- #
#  Colors & tiny UI helpers
# --------------------------------------------------------------------------- #

def _supports_color():
    if "--no-color" in sys.argv or os.environ.get("NO_COLOR"):
        return False
    if not sys.stdout.isatty():
        return False
    if IS_WIN:
        # Try to turn on ANSI (Windows 10+). If it fails, no color.
        try:
            import ctypes
            k = ctypes.windll.kernel32
            k.SetConsoleMode(k.GetStdHandle(-11), 7)
            return True
        except Exception:
            return False
    return True

COLOR = _supports_color()

def _c(code):
    return code if COLOR else ""

RESET  = _c("\033[0m")
BOLD   = _c("\033[1m")
DIM    = _c("\033[2m")
RED    = _c("\033[91m")
GREEN  = _c("\033[92m")
YELLOW = _c("\033[93m")
BLUE   = _c("\033[94m")
PURPLE = _c("\033[95m")
CYAN   = _c("\033[96m")

def say(msg=""):
    print(msg)

def rule():
    say(DIM + "─" * 52 + RESET)

def banner():
    weasel = r"""
        _______
     .-'  ^   ^ '-.        .-""-.
    /    (o) (o)    \      ( ((•)) )   « headphones on »
   |    __,---,__    |      '-..-'
   |   (  goggles )==|=======\\           patched in,
    \   '-.___.-'   /         \\          goggled up,
     '-._________.-'           o          ready to flash
        E A S E L   W E A S E L
   ~ banana cables charged · low-pass gates open ~
"""
    say(PURPLE + BOLD + weasel + RESET)

# --------------------------------------------------------------------------- #
#  Jokes & flavor (skippable via --serious)
# --------------------------------------------------------------------------- #

SERIOUS = "--serious" in sys.argv or os.environ.get("FLASHY_SERIOUS")

WAIT_QUIPS = [
    "The Weasel is patching banana cables into the void... hang tight.",
    "Still scanning. Somewhere, a Music Easel bongos softly.",
    "Fun fact: banana plugs stack so you can patch a cable into a cable. The Weasel approves.",
    "No board yet. The Weasel is noodling on the sequencer to pass the time.",
    "Did you hold the button? The Weasel raises one furry, goggled eyebrow.",
    "Patience — even a slow attack envelope gets there eventually.",
    "Warming up the low-pass gates. West Coast synthesis takes a beat.",
    "Weasel status: goggles on, headphones on, vibes immaculate. Board status: MIA.",
]

SUCCESS_LINES = [
    "The Weasel drops the beat. That patch is clean. 🎛️",
    "Firmware in, gates open, bananas stacked. The Weasel is pleased.",
    "Nailed it. The Easel Weasel tips its little goggles to you.",
    "Boom. Somewhere a modular synth patch nods respectfully.",
    "Patched and playing. Go make some unreasonable noises.",
]

def quip(pool):
    return "" if SERIOUS else random.choice(pool)

# --------------------------------------------------------------------------- #
#  Config
# --------------------------------------------------------------------------- #

DEFAULTS = {
    "product_name": "your device",
    "firmware": None,                # path (rel to this folder) or None = auto-find
    "method": "uf2",                 # "uf2" | "command"
    "board_drive_names": [],         # optional preferred UF2 drive names
    "enter_bootloader_hint": (
        "Hold the little BOOTSEL/BOOT button on the board while you plug it "
        "into USB, then let go."
    ),
    "command": None,                 # for method=="command"; a template string or a
                                     # LIST of templates (first whose tool exists on
                                     # this machine wins); use {firmware} placeholder
}

def load_config():
    cfg = dict(DEFAULTS)
    path = os.path.join(HERE, "flash.config.json")
    if os.path.exists(path):
        try:
            with open(path, "r", encoding="utf-8") as f:
                cfg.update(json.load(f))
        except Exception as e:
            say(YELLOW + f"(Heads up: couldn't read flash.config.json — {e}. "
                         "Using sensible defaults.)" + RESET)
    return cfg

# --------------------------------------------------------------------------- #
#  Firmware resolution
# --------------------------------------------------------------------------- #

FW_EXTS = (".uf2", ".bin", ".hex", ".elf", ".dfu")

def find_firmware(cfg):
    # 1) explicit override from --file
    if "--file" in sys.argv:
        i = sys.argv.index("--file")
        if i + 1 < len(sys.argv):
            return os.path.abspath(sys.argv[i + 1])
    # 2) config
    if cfg.get("firmware"):
        p = cfg["firmware"]
        return p if os.path.isabs(p) else os.path.join(HERE, p)
    # 3) auto-discover: look in ./firmware then ./ for a single match
    search_dirs = [os.path.join(HERE, "firmware"), HERE]
    candidates = []
    for d in search_dirs:
        if os.path.isdir(d):
            for name in sorted(os.listdir(d)):
                if name.lower().endswith(FW_EXTS):
                    candidates.append(os.path.join(d, name))
        if candidates:
            break
    if not candidates:
        return None
    if len(candidates) == 1:
        return candidates[0]
    # multiple — let the human pick
    say(CYAN + "I found a few firmware files. Which one?" + RESET)
    for idx, c in enumerate(candidates, 1):
        say(f"  {BOLD}{idx}{RESET}) {os.path.basename(c)}")
    while True:
        choice = input(CYAN + "Type a number and press Enter: " + RESET).strip()
        if choice.isdigit() and 1 <= int(choice) <= len(candidates):
            return candidates[int(choice) - 1]
        say(YELLOW + "That wasn't one of the numbers. Try again." + RESET)

# --------------------------------------------------------------------------- #
#  UF2 board detection  (any mounted drive containing INFO_UF2.TXT)
# --------------------------------------------------------------------------- #

def candidate_mounts():
    mounts = []
    if IS_WIN:
        for letter in string.ascii_uppercase:
            root = f"{letter}:\\"
            if os.path.exists(root):
                mounts.append(root)
    elif IS_MAC:
        base = "/Volumes"
        if os.path.isdir(base):
            mounts = [os.path.join(base, d) for d in os.listdir(base)]
    else:  # linux
        user = os.environ.get("USER", "")
        for base in (f"/media/{user}", "/media", f"/run/media/{user}", "/mnt"):
            if os.path.isdir(base):
                for d in os.listdir(base):
                    p = os.path.join(base, d)
                    if os.path.isdir(p):
                        # one level deeper for /media/<user>/<label>
                        subs = [os.path.join(p, s) for s in os.listdir(p)] \
                               if os.path.isdir(p) else []
                        mounts.append(p)
                        mounts.extend(s for s in subs if os.path.isdir(s))
    return mounts

def find_uf2_drive(cfg):
    preferred = [n.lower() for n in cfg.get("board_drive_names", [])]
    hits = []
    for m in candidate_mounts():
        try:
            if os.path.exists(os.path.join(m, "INFO_UF2.TXT")):
                hits.append(m)
        except OSError:
            continue
    if not hits:
        return None
    # prefer a name the maintainer told us about
    for m in hits:
        if os.path.basename(m.rstrip("\\/")).lower() in preferred:
            return m
    return hits[0]

# --------------------------------------------------------------------------- #
#  The waiting spinner
# --------------------------------------------------------------------------- #

# A little banana patch-cable snaking left to right while we wait.
SPIN = ["●○○○○", "○●○○○", "○○●○○", "○○○●○", "○○○○●", "○○○●○", "○○●○○", "○●○○○"]

def wait_for_uf2(cfg):
    # already in bootloader?
    drive = find_uf2_drive(cfg)
    if drive:
        say(GREEN + "Oh nice, the board's already in flashing mode. "
                    "The Weasel respects your preparation." + RESET)
        return drive

    say()
    say(CYAN + BOLD + "Step 1: get the board ready" + RESET)
    say("  " + cfg["enter_bootloader_hint"])
    say(DIM + "  (The Weasel is watching your USB ports and will pounce the "
              "moment it's ready — no rush.)" + RESET)
    say()

    i = 0
    last_quip = time.time()
    quip_line = quip(WAIT_QUIPS)
    try:
        while True:
            drive = find_uf2_drive(cfg)
            if drive:
                sys.stdout.write("\r" + " " * 70 + "\r")
                sys.stdout.flush()
                say(GREEN + "Found it! The board is ready. 🎯" + RESET)
                return drive
            frame = SPIN[i % len(SPIN)] if not SERIOUS else "..."
            sys.stdout.write(f"\r{YELLOW}{frame}{RESET} {DIM}{quip_line}{RESET}   ")
            sys.stdout.flush()
            i += 1
            if time.time() - last_quip > 6:
                quip_line = quip(WAIT_QUIPS)
                last_quip = time.time()
            time.sleep(0.25)
    except KeyboardInterrupt:
        say("\n" + YELLOW + "Okay, stopping. Come back when you're ready. 👋" + RESET)
        sys.exit(130)

# --------------------------------------------------------------------------- #
#  Flashing backends
# --------------------------------------------------------------------------- #

def flash_uf2(firmware, cfg):
    drive = wait_for_uf2(cfg)
    dest = os.path.join(drive, os.path.basename(firmware))
    size = os.path.getsize(firmware)

    say()
    say(CYAN + BOLD + "Step 2: the Weasel patches it in" + RESET)
    say(DIM + f"  {os.path.basename(firmware)} → {drive}  ({size//1024} KB)" + RESET)

    # The classic UF2 gotcha: the board reboots mid-copy, so the drive vanishes
    # and the final flush can raise. That's SUCCESS, not failure. We confirm by
    # watching the drive disappear afterward.
    copy_error = None
    try:
        with open(firmware, "rb") as src, open(dest, "wb") as dst:
            shutil.copyfileobj(src, dst, length=64 * 1024)
            dst.flush()
            try:
                os.fsync(dst.fileno())
            except OSError:
                pass
    except OSError as e:
        copy_error = e  # expected if the board rebooted early

    # Confirm by drive disappearing (board jumped to the new firmware).
    for _ in range(20):
        if find_uf2_drive(cfg) is None and not os.path.exists(drive):
            return True
        time.sleep(0.25)

    if copy_error and os.path.exists(drive):
        say(RED + f"\nHmm, the copy hiccuped and the board's still sitting in "
                  f"flashing mode.\nError was: {copy_error}" + RESET)
        return False

    # Drive still present but no error — file landed; some OSes keep it mounted.
    return True

def _quote_path(p):
    # {firmware} lands inside a shell=True command line; a path with spaces
    # (hello, "Downloads folder") must be quoted or the tool sees two args.
    return f'"{p}"' if IS_WIN else shlex.quote(p)

def _hex_to_bin(hex_path):
    """Minimal Intel-HEX -> raw bin (gaps filled 0xFF). Returns temp path."""
    import tempfile
    segs = {}
    base = 0
    for line in open(hex_path):
        line = line.strip()
        if not line.startswith(":"):
            continue
        b = bytes.fromhex(line[1:])
        cnt, addr, typ = b[0], (b[1] << 8) | b[2], b[3]
        data = b[4:4 + cnt]
        if typ == 0x04:
            base = ((data[0] << 8) | data[1]) << 16
        elif typ == 0x00:
            segs[base + addr] = data
        elif typ == 0x01:
            break
    lo = min(segs)
    hi = max(a + len(d) for a, d in segs.items())
    img = bytearray(b"\xff" * (hi - lo))
    for a, d in segs.items():
        img[a - lo:a - lo + len(d)] = d
    f = tempfile.NamedTemporaryFile(suffix=".bin", delete=False)
    f.write(bytes(img)); f.close()
    return f.name, lo

def flash_command(firmware, cfg, _retried=False):
    tmpl = cfg.get("command")
    if not tmpl:
        say(RED + "This device is set to 'command' mode but no command was "
                  "configured in flash.config.json. Poke the maintainer." + RESET)
        return False

    # The config may list SEVERAL command templates (different tools do the
    # same job and different users have different ones installed). The Weasel
    # runs the first one whose tool actually exists on this machine.
    templates = tmpl if isinstance(tmpl, list) else [tmpl]

    def _tool_available(t):
        """Name the tool and check it exists. '{python} -m mod ...' templates
        are probed by importability (works for --user pip installs whose
        entry-point scripts aren't on PATH)."""
        if t.startswith("{python} -m "):
            mod = t.split()[2]
            r = subprocess.run([sys.executable, "-c", f"import {mod}"],
                               capture_output=True)
            return (f"{mod} (pip)", r.returncode == 0)
        if "{vendor}" in t:
            r = subprocess.run([sys.executable, "-c", "import usb.core"],
                               capture_output=True)
            return ("bundled ST-Link driver (needs: pip install pyusb libusb-package)",
                    r.returncode == 0)
        tool = (t.split() or [""])[0]
        return (tool, shutil.which(tool) is not None)

    chosen, missing = None, []
    for t in templates:
        name, ok = _tool_available(t)
        if ok:
            chosen = t
            break
        missing.append(name)
    if chosen is None:
        say(RED + "\nNone of the tools that can flash this device are "
                  "installed on this computer:" + RESET)
        for tool in missing:
            say(f"  {BOLD}•{RESET} {tool}")

        # Offer to install one right here — musicians shouldn't need to know
        # what openocd is (issue #6). Pick the platform's package manager.
        import shutil as _sh
        offer = None
        if sys.platform == "darwin" and _sh.which("brew"):
            offer = ("openocd via Homebrew", "brew install openocd")
        elif sys.platform.startswith("linux"):
            if _sh.which("apt-get"):
                offer = ("stlink-tools via apt", "sudo apt-get install -y stlink-tools")
            elif _sh.which("dnf"):
                offer = ("stlink via dnf", "sudo dnf install -y stlink")
        elif sys.platform == "win32" and _sh.which("winget"):
            offer = ("STM32CubeProgrammer via winget",
                     "winget install -e STMicroelectronics.STM32CubeProgrammer")
        if offer is None:
            # UNIVERSAL fallback: pip is wherever this script runs, no admin,
            # no package manager needed. pyocd drives the ST-Link directly.
            offer = ("a pip-installed flasher stack (no admin needed)",
                     f"{_quote_path(sys.executable)} -m pip install pyocd pyusb libusb-package")
        if offer and sys.stdin.isatty() and not _retried:
            name, cmd = offer
            say(f"\nI can install {BOLD}{name}{RESET} for you now by running:")
            say(f"  {DIM}{cmd}{RESET}")
            ans = input("Install it? [Y/n] ").strip().lower()
            if ans in ("", "y", "yes"):
                say("Installing (this can take a few minutes)...")
                r = subprocess.run(cmd, shell=True)
                if r.returncode == 0:
                    say(GREEN + "Installed! Continuing..." + RESET)
                    return flash_command(firmware, cfg, _retried=True)
                say(RED + "Install failed." + RESET)
        say("\nManual install — any ONE of these, then run me again:")
        if sys.platform == "darwin":
            say("  macOS:   brew install openocd     (get Homebrew at https://brew.sh)")
        elif sys.platform.startswith("linux"):
            say("  Linux:   sudo apt install stlink-tools   (or: sudo dnf install stlink)")
        else:
            say("  Windows: install STM32CubeProgrammer from st.com (free, needs an email)")
            say("           https://www.st.com/en/development-tools/stm32cubeprog.html")
        say("The Weasel will wait by the modular. 🎛️")
        return False

    # Command-mode boards have pre-flight steps too (attach a programmer,
    # hold a button...). Show the maintainer's hint before firing the tool.
    hint = cfg.get("enter_bootloader_hint")
    if hint:
        say()
        say(CYAN + BOLD + "Step 1: get the board ready" + RESET)
        say("  " + hint)
        if sys.stdin.isatty():
            try:
                input(DIM + "  Press Enter when that's done and the Weasel "
                            "will take it from here... " + RESET)
            except (EOFError, KeyboardInterrupt):
                say("\n" + YELLOW + "Okay, stopping. Come back when you're "
                    "ready. 👋" + RESET)
                return False

    # every template whose tool is present, in config order — if one RUNS but
    # FAILS (e.g. pyocd refusing an old ST-Link's firmware), fall through to
    # the next instead of giving up
    runnable = [chosen] + [t for t in templates[templates.index(chosen)+1:]
                           if _tool_available(t)[1]]
    vendor_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "vendor")

    # STAGE the firmware under a fixed, space-free name in a temp dir and run
    # every tool from there. No user path ever enters a command line — quoting
    # cannot fail because there is nothing to quote (issue #7: "flasher 3"
    # folders from Finder's duplicate naming broke openocd's Tcl parsing).
    import tempfile, shutil as _shcp
    stage = tempfile.mkdtemp(prefix="weasel_")
    staged_hex = os.path.join(stage, "firmware.hex")
    _shcp.copyfile(firmware, staged_hex)
    say()
    say(CYAN + BOLD + "Step 2: the Weasel patches it in" + RESET)
    for i, t in enumerate(runnable):
        cmd = t.replace("{firmware}", "firmware.hex")
        cmd = cmd.replace("{python}", _quote_path(sys.executable))
        cmd = cmd.replace("{vendor}", _quote_path(vendor_dir))
        if "{firmware_bin}" in cmd:
            binp, lo = _hex_to_bin(staged_hex)
            staged_bin = os.path.join(stage, "firmware.bin")
            _shcp.move(binp, staged_bin)
            cmd = cmd.replace("{firmware_bin}", "firmware.bin")
            cmd = cmd.replace("{flash_base}", "0x%08x" % lo)
        say(DIM + "  " + cmd + f"   (in {stage})" + RESET)
        try:
            result = subprocess.run(cmd, shell=True, cwd=stage)
        except Exception as e:
            say(RED + f"That command didn't want to run: {e}" + RESET)
            continue
        if result.returncode == 0:
            return True
        if i + 1 < len(runnable):
            say(YELLOW + "\nThat tool couldn't do it — trying the next one..." + RESET)
    return False

# --------------------------------------------------------------------------- #
#  Celebration / commiseration
# --------------------------------------------------------------------------- #

def celebrate(cfg):
    say()
    say(GREEN + BOLD + r"""
      \(•ᴥ•)/   PATCHED IN!
       ((o))    """ + quip(SUCCESS_LINES) + RESET)
    say()
    say(GREEN + f"Your {cfg['product_name']} is running fresh firmware. "
                "The Easel Weasel wanders back to its synth. 🎹" + RESET)

def commiserate(method="uf2"):
    say()
    say(YELLOW + BOLD + "Well, that didn't work — but nothing's broken." + RESET)
    if method == "command":
        # A programmer (ST-Link/DFU/etc.) failed to talk to the board. The real
        # reason is in the tool's own output ABOVE — don't send UF2 advice here.
        say("""The flashing tool couldn't reach the board. Try this, in order:
  1. The exact reason is in the tool's output just above — the line that starts
     with "Error:" says what went wrong. That's the thing to read (and to send
     the maintainer).
  2. Plug the programmer straight into a USB port on the computer — not through
     a hub or dock — and try a different USB cable.
  3. Check the wiring to the board: ribbon/leads in the right orientation, and
     the board's 3.3V (VREF/VTref) pin reaching the programmer so it can sense
     target power. A powered board with no VREF connection still fails.
  4. Make sure the board is actually powered on, and not held in reset.
  5. Still stuck? Send the maintainer a screenshot including the "Error:" line.""")
    else:
        say("""Try this, in order:
  1. Unplug the device, plug it back in, run this again.
  2. Make sure you did the button-hold to enter flashing mode.
  3. Try a different USB cable (some cables are charge-only liars).
  4. Still stuck? Send the maintainer a screenshot of this window.""")

# --------------------------------------------------------------------------- #
#  Main
# --------------------------------------------------------------------------- #

def pause_before_exit():
    # So double-click users can read the result before the window closes.
    if not sys.stdin.isatty():
        return
    try:
        input("\n" + DIM + "Press Enter to close this window." + RESET)
    except (EOFError, KeyboardInterrupt):
        pass

def main():
    banner()
    cfg = load_config()
    rule()
    say(f"About to flash: {BOLD}{cfg['product_name']}{RESET}")

    firmware = find_firmware(cfg)
    if not firmware or not os.path.exists(firmware):
        say(RED + "\nI couldn't find any firmware to flash. 😬" + RESET)
        say("Maintainer: drop a .uf2 in the 'firmware' folder next to this "
            "script, or set \"firmware\" in flash.config.json.")
        pause_before_exit()
        sys.exit(1)

    say(f"Firmware file: {BOLD}{os.path.basename(firmware)}{RESET}")
    rule()

    method = cfg.get("method", "uf2").lower()
    if method == "uf2":
        ok = flash_uf2(firmware, cfg)
    elif method == "command":
        ok = flash_command(firmware, cfg)
    else:
        say(RED + f"Unknown method '{method}' in config. Use 'uf2' or 'command'."
            + RESET)
        ok = False

    if ok:
        celebrate(cfg)
    else:
        commiserate(cfg.get("method", "uf2"))

    pause_before_exit()
    sys.exit(0 if ok else 1)

if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        say(RED + f"\nUnexpected gremlin: {e}" + RESET)
        say("This isn't your fault. Screenshot it and send it to the maintainer.")
        pause_before_exit()
        sys.exit(1)
