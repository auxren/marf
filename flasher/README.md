# ⚡ Easel Weasel — the stupidly-simple firmware flasher

A goggle-wearing, headphone-clad, banana-cable-patching weasel flashes your
device for you. No drivers to hunt down, no command line to fear, no cursing.
Double-click one file, follow one friendly instruction, done.

Themed after the **Easel Weasel** — goggles, headphones, banana cables — because
flashing firmware should feel like patching a synth, not defusing a bomb.

```
        _______
     .-'  ^   ^ '-.        .-""-.
    /    (o) (o)    \      ( ((•)) )   « headphones on »
   |    __,---,__    |      '-..-'
   |   (  goggles )==|=======\\           patched in,
    \   '-.___.-'   /         \\          goggled up,
     '-._________.-'           o          ready to flash
        E A S E L   W E A S E L
```

---

## 👉 For people flashing their device (end users)

You only need to do **one** thing: double-click the file for your computer.

| Your computer | Double-click this |
|---|---|
| 🍎 Mac | `Flash-on-Mac.command` |
| 🪟 Windows | `Flash-on-Windows.bat` |
| 🐧 Linux | `Flash-on-Linux.sh` |

Then the Weasel tells you exactly what to do (usually: *hold the little button
and plug in the USB cable*). That's it. Really.

**If your Mac says the file "cannot be opened"** — right-click it → **Open** →
**Open** once. macOS just wants you to confirm. You won't have to do it again.

**If nothing happens on Windows** and it mentions Python — install it from
<https://www.python.org/downloads/> (tick *"Add Python to PATH"*), then
double-click again. Two minutes, one time.

**One extra thing for the MARF** (it flashes through a plug-in programmer,
not USB): you need an **ST-Link v2** (cheap, everywhere) and ONE flashing
tool installed — the Weasel finds whichever you have:

- 🍎 Mac: `brew install stlink`
- 🐧 Linux: `sudo apt install stlink-tools`
- 🪟 Windows: install [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html)
  and add its `bin` folder to your PATH (one time)

The Weasel walks you through the module-out / ribbon-on steps when you run it.

That's the whole manual. Go blink some lights. ✨

---

## 🔧 For maintainers (shipping this with your firmware)

1. **Copy this `easel-weasel/` folder into your repo** (or into your release zip).
2. **Put your firmware in `firmware/`** — e.g. `firmware/my-device-v1.2.uf2`.
   If it's the only firmware file there, the Weasel finds it automatically.
3. **Edit `flash.config.json`** to taste:

   ```jsonc
   {
     "product_name": "Easel Weasel X-7",   // shown to the user
     "firmware": null,                      // null = auto-find in ./firmware
     "method": "uf2",                       // "uf2" (drag-drop) or "command"
     "board_drive_names": ["RPI-RP2"],      // optional hint for the drive name
     "enter_bootloader_hint": "Hold BOOTSEL while plugging in USB.",
     "command": null
   }
   ```

4. **Ship it.** Include the folder in your GitHub Release assets.

### Two flashing modes

- **`uf2`** (default, zero dependencies): for RP2040 / RP2350 (Pico), most
  Adafruit & Seeed boards — anything that shows up as a USB drive with an
  `INFO_UF2.TXT` on it. The Weasel just copies the `.uf2` onto it. That's the
  whole trick, and it's why UF2 boards are the *stupidly-simple* case.

- **`command`**: for boards that need a real tool (STM32 DFU/SWD, ESP32, AVR…).
  Set `method` to `"command"` and give a command template; `{firmware}` is
  replaced with the resolved firmware path (safely quoted — spaces in the
  path are fine):

  ```jsonc
  { "method": "command",
    "command": "dfu-util -a 0 -s 0x08000000:leave -D {firmware}" }
  ```

  `command` can also be a **list** of templates. The Weasel runs the first
  one whose tool is actually installed on the user's machine — handy when
  several tools can do the job and you don't know which one the user has:

  ```jsonc
  { "method": "command",
    "command": [
      "STM32_Programmer_CLI -c port=SWD -w {firmware} -v -rst",
      "st-flash --reset --format ihex write {firmware}"
    ] }
  ```

  If none of the tools exist, the Weasel lists them and asks the user to
  install one. And since command-mode boards usually have pre-flight steps
  (attach a programmer, hold a button), `enter_bootloader_hint` is shown
  first with a "press Enter when ready" pause — write real instructions in
  it. (The user needs the tool installed; the Weasel just runs it with a
  smile.)

### Handy flags (for testing)

| Flag | Does |
|---|---|
| `--file path/to/fw.uf2` | flash a specific file, ignore config |
| `--serious` | mute the jokes and fancy animation |
| `--no-color` | plain text, no ANSI colors |

Run directly with `python3 flash.py` while developing.

---

## Why it Just Works™

- **Pure Python standard library** — nothing to `pip install` for UF2 boards.
- **Generic board detection** — finds any UF2 bootloader by its `INFO_UF2.TXT`,
  so it isn't tied to one drive name.
- **Handles the classic UF2 gotcha** — the board reboots mid-copy and the drive
  vanishes; the Weasel treats that as success (because it is) instead of a scary
  error.
- **One brain, three doorknobs** — all the logic and personality live in
  `flash.py`; the launchers just open the door for each OS.
