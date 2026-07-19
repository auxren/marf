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
DRY_RUN = "--dry-run" in sys.argv
ASSUME_YES = "--yes" in sys.argv

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

def flash_command(firmware, cfg):
    tmpl = cfg.get("command")
    if not tmpl:
        say(RED + "This device is set to 'command' mode but no command was "
                  "configured in flash.config.json. Poke the maintainer." + RESET)
        return False

    # The config may list SEVERAL command templates (different tools do the
    # same job and different users have different ones installed). The Weasel
    # runs the first one whose tool actually exists on this machine.
    templates = tmpl if isinstance(tmpl, list) else [tmpl]
    chosen, missing = None, []
    for t in templates:
        tool = (t.split() or [""])[0]
        if shutil.which(tool):
            chosen = t
            break
        missing.append(tool)
    if chosen is None:
        say(RED + "\nNone of the tools that can flash this device are "
                  "installed on this computer:" + RESET)
        for tool in missing:
            say(f"  {BOLD}•{RESET} {tool}")
        say("\nInstall any ONE of them (the README in this folder says how), "
            "then run me again. The Weasel will wait by the modular. 🎛️")
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
        elif not (ASSUME_YES or DRY_RUN):
            # No human at the keyboard and no explicit consent: a command-mode
            # flash writes to WHATEVER is attached to the programmer. Refuse.
            # (Learned the hard way: a "dry run" with stdin redirected once
            # flashed a real, different board sitting on the bench.)
            say(YELLOW + "\nNo interactive terminal, so the Weasel won't "
                "fire a flashing tool blind." + RESET)
            say("Re-run with --yes to flash anyway, or --dry-run to preview.")
            return False

    cmd = chosen.replace("{firmware}", _quote_path(firmware))
    say()
    say(CYAN + BOLD + "Step 2: the Weasel patches it in" + RESET)
    say(DIM + "  " + cmd + RESET)
    say()
    if DRY_RUN:
        say(YELLOW + "(--dry-run: stopping right here — nothing was "
            "flashed. The Weasel mimes the motions.)" + RESET)
        return True
    try:
        result = subprocess.run(cmd, shell=True)
        return result.returncode == 0
    except Exception as e:
        say(RED + f"That command didn't want to run: {e}" + RESET)
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

def commiserate():
    say()
    say(YELLOW + BOLD + "Well, that didn't work — but nothing's broken." + RESET)
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
        if DRY_RUN:
            say(YELLOW + "(--dry-run: would wait for a UF2 drive and copy "
                + os.path.basename(firmware) + " onto it. Stopping here.)" + RESET)
            ok = True
        else:
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
        commiserate()

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
